#include "kernel_file_io.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace dotm {
namespace {
constexpr std::uint32_t kSuccess = 0;
constexpr std::uint32_t kInvalidHandle = 0xC0000008U;
constexpr std::uint32_t kInvalidParameter = 0xC000000DU;
constexpr std::uint32_t kEndOfFile = 0xC0000011U;
constexpr std::uint32_t kAccessDenied = 0xC0000022U;
constexpr std::uint32_t kNameNotFound = 0xC0000034U;
constexpr std::uint32_t kPathNotFound = 0xC000003AU;
constexpr std::uint32_t kNotSupported = 0xC00000BBU;
constexpr std::uint32_t kFirstHandle = 0xF9000000U;
struct FileState {
    std::ifstream stream;
    std::uint64_t position = 0;
};
std::mutex mutex;
GuestMemory* memory = nullptr;
std::filesystem::path mount_root;
std::unordered_map<std::uint32_t, std::shared_ptr<FileState>> files;
std::uint32_t next_handle = kFirstHandle;

bool valid(std::uint32_t address, std::uint32_t size, bool write) {
    if (!memory || address < 0x10000U) return false;
    const auto begin = std::uint64_t(address);
    const auto end = begin + size;
    const auto table = GuestMemory::kFunctionTableOffset;
    return end <= GuestMemory::kAddressSpaceSize &&
        !(write && begin < table + GuestMemory::kFunctionTableSize && end > table) &&
        memory->committed_range(begin, size);
}
std::uint16_t read16(std::uint32_t address) {
    const auto* p = memory->address(address);
    return std::uint16_t((std::uint16_t(p[0]) << 8) | p[1]);
}
std::uint32_t read32(std::uint32_t address) {
    const auto* p = memory->address(address);
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
           (std::uint32_t(p[2]) << 8) | p[3];
}
std::uint64_t read64(std::uint32_t address) {
    return (std::uint64_t(read32(address)) << 32) | read32(address + 4);
}
void write32(std::uint32_t address, std::uint32_t value) {
    auto* p = memory->address(address);
    for (int i = 0; i < 4; ++i)
        p[i] = std::uint8_t(value >> (24 - 8*i));
}
void set_io_status(std::uint32_t address, std::uint32_t status, std::uint32_t bytes) {
    write32(address, status);
    write32(address + 4, bytes);
}
std::string ascii_lower(std::string value) {
    for (auto& c : value)
        if (c >= 'A' && c <= 'Z') c = char(c + ('a' - 'A'));
    return value;
}
// Reject traversal and symlinks. Match disc paths case-insensitively on Linux
// just as the Xbox filesystem does on Windows; never access host absolute paths.
std::optional<std::filesystem::path> resolve_disc_path(const std::string& guest) {
    if (mount_root.empty()) return std::nullopt;
    auto name = guest;
    std::replace(name.begin(), name.end(), '\\', '/');
    if (name.size() >= 3 && (name[0] == 'D' || name[0] == 'd') &&
        name[1] == ':' && name[2] == '/') name.erase(0, 3);
    else if (name.size() >= 6 && ascii_lower(name.substr(0, 6)) == "game:/")
        name.erase(0, 6);
    else return std::nullopt;
    if (name.empty() || name.find(':') != std::string::npos) return std::nullopt;
    auto current = mount_root;
    std::size_t pos = 0;
    while (pos < name.size()) {
        const auto next = name.find('/', pos);
        const auto part = name.substr(pos, next == std::string::npos ?
            std::string::npos : next - pos);
        if (part.empty() || part == "." || part == "..") return std::nullopt;
        std::error_code ec;
        if (!std::filesystem::is_directory(current, ec)) return std::nullopt;
        bool found = false;
        for (std::filesystem::directory_iterator it(current, ec), end;
             !ec && it != end; it.increment(ec)) {
            if (ascii_lower(it->path().filename().string()) == ascii_lower(part)) {
                if (it->is_symlink(ec) || ec) return std::nullopt;
                current = it->path();
                found = true;
                break;
            }
        }
        if (ec || !found) return std::nullopt;
        if (next == std::string::npos) break;
        pos = next + 1;
    }
    return current;
}
}
void bind_kernel_file_io(GuestMemory* bound, const std::filesystem::path& root) noexcept {
    std::lock_guard lock(mutex);
    files.clear();
    memory = bound;
    mount_root.clear();
    next_handle = kFirstHandle;
    if (!bound || root.empty()) return;
    std::error_code ec;
    const auto canonical = std::filesystem::canonical(root, ec);
    if (!ec && std::filesystem::is_directory(canonical, ec) && !ec)
        mount_root = canonical;
}
std::uint32_t nt_bound_create_file(std::uint32_t handle_out,
    std::uint32_t access, std::uint32_t attributes, std::uint32_t io_status,
    std::uint32_t allocation_size, std::uint32_t file_attributes,
    std::uint32_t share_access, std::uint32_t disposition,
    std::uint32_t guest_stack) {
    std::lock_guard lock(mutex);
    if (guest_stack > 0xFFFFFFABU || !valid(handle_out, 4, true) || (handle_out & 3U) ||
        !valid(io_status, 8, true) || (io_status & 3U) ||
        !valid(attributes, 12, false) || (attributes & 3U) ||
        !valid(guest_stack + 84U, 4, false) || share_access > 7 ||
        (allocation_size && (!valid(allocation_size, 8, false) || (allocation_size & 7U))))
        return kInvalidParameter;
    const auto options = read32(guest_stack + 84U);
    if (disposition != 1 || (options & ~0x00000064U) ||
        (options & 0x40U) == 0 ||
        (file_attributes != 0 && file_attributes != 1 && file_attributes != 0x80U))
        return kNotSupported;
    // No host writes, creates, deletes, or file-system mutation are allowed.
    if (access & (0x40000000U | 0x10000000U | 0x00010000U |
                  0x00040000U | 0x00080000U | 0x00000116U))
        return kAccessDenied;
    const auto root = read32(attributes);
    const auto name = read32(attributes + 4);
    const auto flags = read32(attributes + 8);
    if ((root != 0 && root != 0xFFFFFFFDU && root != 0xFFFFFFFCU) ||
        (flags & ~0xC0U) || !name || !valid(name, 8, false) || (name & 3U))
        return kInvalidParameter;
    const auto length = read16(name);
    const auto capacity = read16(name + 2);
    const auto text = read32(name + 4);
    if (!length || length > capacity || length > 4096 || !valid(text, length, false))
        return kInvalidParameter;
    const std::string guest_path(reinterpret_cast<const char*>(memory->address(text)), length);
    if (guest_path.find('\0') != std::string::npos) return kInvalidParameter;
    if (mount_root.empty()) return kPathNotFound;
    const auto resolved = resolve_disc_path(guest_path);
    if (!resolved) return kNameNotFound;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(*resolved, ec) || ec) return kAccessDenied;
    if (next_handle >= 0xF9FFFFFCU) return kNotSupported;
    auto file = std::make_shared<FileState>();
    file->stream.open(*resolved, std::ios::binary);
    if (!file->stream.is_open()) return kAccessDenied;
    const auto handle = next_handle;
    files.emplace(handle, std::move(file));
    next_handle += 4;
    write32(handle_out, handle);
    set_io_status(io_status, kSuccess, 1U); // FILE_OPENED
    return kSuccess;
}
std::uint32_t nt_bound_read_file(std::uint32_t handle, std::uint32_t event,
    std::uint32_t apc_routine, std::uint32_t apc_context,
    std::uint32_t io_status, std::uint32_t buffer,
    std::uint32_t length, std::uint32_t offset,
    std::uint32_t guest_stack) {
    std::lock_guard lock(mutex);
    const auto it = files.find(handle);
    if (it == files.end()) return kInvalidHandle;
    if (event || apc_routine || apc_context) return kNotSupported;
    if (guest_stack > 0xFFFFFFABU || !valid(io_status, 8, true) || (io_status & 3U) ||
        (length && !valid(buffer, length, true)) ||
        !valid(guest_stack + 84U, 4, false) || read32(guest_stack + 84U))
        return kInvalidParameter;
    if (offset && (!valid(offset, 8, false) || (offset & 7U)))
        return kInvalidParameter;
    auto& file = *it->second;
    const auto pos = offset ? read64(offset) : file.position;
    if (pos > std::uint64_t(std::numeric_limits<std::streamoff>::max()))
        return kInvalidParameter;
    file.stream.clear();
    file.stream.seekg(static_cast<std::streamoff>(pos));
    if (!file.stream) return kInvalidParameter;
    if (length)
        file.stream.read(reinterpret_cast<char*>(memory->address(buffer)), length);
    const auto count = length ? static_cast<std::uint32_t>(file.stream.gcount()) : 0U;
    if (length && file.stream.bad()) return kAccessDenied;
    file.position = pos + count;
    const auto status = (length && !count) ? kEndOfFile : kSuccess;
    set_io_status(io_status, status, count);
    return status;
}
std::uint32_t nt_bound_close_file(std::uint32_t handle) {
    std::lock_guard lock(mutex);
    return files.erase(handle) ? kSuccess : kInvalidHandle;
}
}
