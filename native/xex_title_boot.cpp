#include "xex_title_boot.hpp"
#include "guest_memory.hpp"
#include "xex_optional_header.hpp"
#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>
namespace dotm {
namespace {
constexpr std::uint32_t kHeaderAddress = 0x81000000U;
constexpr std::uint32_t kMaximumHeaderSize = 8U * 1024U * 1024U;
constexpr std::uint32_t kModuleHandleAddress = 0x81900000U;
constexpr std::uint32_t kModuleAddress = 0x81901000U;
constexpr std::uint32_t kXamModuleAddress = 0x81905000U;
constexpr std::uint32_t kXamModuleNameAddress = 0x81906000U;
constexpr std::uint32_t kNtNoSuchFile = 0xC000000FU;
constexpr std::uint32_t kNtInvalidHandle = 0xC0000008U;
constexpr std::uint32_t kNtInvalidParameter = 0xC000000DU;
constexpr std::uint32_t kNtNotSupported = 0xC00000BBU;
constexpr std::uint32_t kNtDriverEntryNotFound = 0xC0000263U;
constexpr std::uint32_t kNtNotFound = 0xC0000225U;
constexpr std::uint32_t kXexResourceInfoKey = 0x000002FFU;
std::atomic<GuestMemory*> bound_system_memory{nullptr};
std::mutex module_mutex;
// XboxHardwareInfo and KeDebugMonitorData are kernel-exported data imports.
constexpr std::uint32_t kTimeStampBundleImport = 0x8200085CU;
constexpr std::uint32_t kTimeStampBundleAddress = 0x81904000U;
constexpr std::uint32_t kHardwareInfoImport = 0x82000978U;
constexpr std::uint32_t kHardwareInfoAddress = 0x81902000U;
constexpr std::uint32_t kDebugMonitorImport = 0x820009A0U;
constexpr std::uint32_t kDebugMonitorAddress = 0x81903000U;
// Retail kernel export ordinal 430 (0x1AE) is ExLoadedCommandLine.
// Original DOTM sub_8277F340 returns this import as an ANSI char*.
constexpr std::uint32_t kCommandLineImport = 0x820009D8U;
constexpr std::uint32_t kCommandLineAddress = 0x81907000U;
// Retail xboxkrnl export ordinal 27 is ExThreadObjectType, a data import.
constexpr std::uint32_t kThreadTypeImport = 0x820009B0U;
constexpr std::uint32_t kThreadTypeAddress = 0x81908000U;
constexpr std::uint32_t kRetailHardwareFlags = 0x20U;
constexpr std::uint8_t kHardwareCpuCount = 6U;
// The original DOTM sub_827813F8 dereferences [0x82000998] then its handle,
// then reads +0x58 for the XEX header supplied to RtlImageXexHeaderField.
constexpr std::uint32_t kOriginalModuleImport = 0x82000998U;
constexpr std::uint32_t kXexEntryKey = 0x00010100U;
constexpr std::uint32_t kExecutionIdKey = 0x00040006U;
constexpr std::uint32_t kDotmTitleId = 0x415608A4U;
std::uint16_t be16(const std::uint8_t* ptr) {
    return (std::uint16_t(ptr[0]) << 8) | ptr[1];
}
void put16(GuestMemory& memory, std::uint32_t address, std::uint16_t value) {
    auto* ptr = memory.address(address);
    ptr[0] = std::uint8_t(value >> 8);
    ptr[1] = std::uint8_t(value);
}
bool valid_output(const GuestMemory& memory, std::uint32_t address) {
    const auto begin = std::uint64_t(address);
    const auto end = begin + 4;
    const auto table = GuestMemory::kFunctionTableOffset;
    return begin >= 0x10000U && end <= GuestMemory::kAddressSpaceSize &&
        !(begin < table + GuestMemory::kFunctionTableSize && end > table) &&
        memory.committed_range(begin, 4);
}
std::string read_module_name(const GuestMemory& memory, std::uint32_t address) {
    if (!address) return {};
    std::string name;
    for (std::uint32_t i = 0; i < 64; ++i) {
        const auto guest = std::uint64_t(address) + i;
        if (guest >= GuestMemory::kAddressSpaceSize ||
            !memory.committed_range(guest, 1)) return {};
        const auto ch = memory.address(static_cast<std::uint32_t>(guest))[0];
        if (!ch) return name;
        if (ch >= 0x80U) return {};
        name.push_back(char(ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch));
    }
    return {};
}
std::uint32_t be32(const std::uint8_t* ptr) {
    return (std::uint32_t(ptr[0]) << 24) |
           (std::uint32_t(ptr[1]) << 16) |
           (std::uint32_t(ptr[2]) << 8) |
           std::uint32_t(ptr[3]);
}
void put32(GuestMemory& memory, std::uint32_t address, std::uint32_t value) {
    auto* ptr = memory.address(address);
    ptr[0] = std::uint8_t(value >> 24);
    ptr[1] = std::uint8_t(value >> 16);
    ptr[2] = std::uint8_t(value >> 8);
    ptr[3] = std::uint8_t(value);
}
}
void bind_xex_system_modules(GuestMemory* memory) noexcept {
    bound_system_memory.store(memory, std::memory_order_release);
}
std::uint32_t bound_xex_load_image(
    std::uint32_t name_pointer, std::uint32_t flags,
    std::uint32_t minimum_version, std::uint32_t output_pointer) {
    std::lock_guard lock(module_mutex);
    auto* memory = bound_system_memory.load(std::memory_order_acquire);
    if (!memory) return kNtNotSupported;
    if (!valid_output(*memory, output_pointer)) return kNtInvalidParameter;
    const auto name = read_module_name(*memory, name_pointer);
    if (name != "xam.xex") {
        put32(*memory, output_pointer, 0U);
        return name.empty() ? kNtInvalidParameter : kNtNoSuchFile;
    }
    if (minimum_version != 0U || (flags != 0U && flags != 9U))
        return kNtNotSupported;
    if (!memory->committed_range(kXamModuleAddress, 0x64U))
        return kNtNotSupported;
    const auto count = be16(memory->address(kXamModuleAddress + 0x40U));
    if (count == 0xFFFFU) return kNtNotSupported;
    put16(*memory, kXamModuleAddress + 0x40U, std::uint16_t(count + 1));
    put32(*memory, output_pointer, kXamModuleAddress);
    return 0U;
}
std::uint32_t bound_xex_get_module_handle(
    std::uint32_t name_pointer, std::uint32_t output_pointer) {
    std::lock_guard lock(module_mutex);
    auto* memory = bound_system_memory.load(std::memory_order_acquire);
    if (!memory) return kNtNotSupported;
    if (!valid_output(*memory, output_pointer)) return kNtInvalidParameter;
    if (!name_pointer) {
        put32(*memory, output_pointer, kModuleAddress);
        return 0U;
    }
    if (read_module_name(*memory, name_pointer) == "xam.xex") {
        put32(*memory, output_pointer, kXamModuleAddress);
        return 0U;
    }
    put32(*memory, output_pointer, 0U);
    return 1168U; // Xbox ERROR_NOT_FOUND.
}
// XexGetModuleSection uses XEX2 RESOURCE_INFO (key 0x000002FF), not PE
// section headers. Original DOTM queries its 7-byte "HashSec" resource.
std::uint32_t bound_xex_get_module_section(
    std::uint32_t module_handle, std::uint32_t section_name,
    std::uint32_t output_address, std::uint32_t output_size) {
    std::lock_guard lock(module_mutex);
    auto* memory = bound_system_memory.load(std::memory_order_acquire);
    if (!memory) return kNtNotSupported;
    if (module_handle != kModuleAddress && module_handle != kXamModuleAddress)
        return kNtInvalidHandle;
    const auto name = read_module_name(*memory, section_name);
    if (name.empty() || name.size() > 8 ||
        !valid_output(*memory, output_address) ||
        !valid_output(*memory, output_size) ||
        (std::uint64_t(output_address) < std::uint64_t(output_size) + 4 &&
         std::uint64_t(output_size) < std::uint64_t(output_address) + 4))
        return kNtInvalidParameter;
    // The native XAM handle has no proprietary XEX resource section table.
    if (module_handle == kXamModuleAddress) return kNtNotFound;
    if (!memory->committed_range(kHeaderAddress, 24)) return kNtNotFound;
    const auto* header = memory->address(kHeaderAddress);
    const auto header_size = be32(header + 8);
    const auto header_count = be32(header + 20);
    if (be32(header) != 0x58455832U || header_size < 24 ||
        header_size > kMaximumHeaderSize ||
        !memory->committed_range(kHeaderAddress, header_size) ||
        header_count > (header_size - 24) / 8)
        return kNtInvalidParameter;
    for (std::uint32_t i = 0; i < header_count; ++i) {
        const auto* field = header + 24 + i * 8;
        if (be32(field) != kXexResourceInfoKey) continue;
        const auto offset = be32(field + 4);
        if (offset > header_size || header_size - offset < 4)
            return kNtInvalidParameter;
        const auto bytes = be32(header + offset);
        if (bytes < 4 || (bytes - 4) % 16 || bytes > header_size - offset)
            return kNtInvalidParameter;
        for (std::uint32_t pos = offset + 4; pos < offset + bytes; pos += 16) {
            const auto* resource = header + pos;
            std::string resource_name;
            for (int j = 0; j < 8 && resource[j]; ++j) {
                const auto ch = resource[j];
                if (ch >= 0x80) return kNtInvalidParameter;
                resource_name.push_back(char(ch >= 'A' && ch <= 'Z'
                    ? ch + ('a' - 'A') : ch));
            }
            if (resource_name != name) continue;
            const auto address = be32(resource + 8);
            const auto size = be32(resource + 12);
            if (!size || !memory->committed_range(address, size))
                return kNtInvalidParameter;
            put32(*memory, output_address, address);
            put32(*memory, output_size, size);
            return 0U;
        }
        return kNtNotFound;
    }
    return kNtNotFound;
}
std::uint32_t bound_xex_get_procedure_address(
    std::uint32_t module_handle, std::uint32_t ordinal,
    std::uint32_t output_pointer) {
    std::lock_guard lock(module_mutex);
    auto* memory = bound_system_memory.load(std::memory_order_acquire);
    if (!memory) return kNtNotSupported;
    if (!valid_output(*memory, output_pointer)) return kNtInvalidParameter;
    if (module_handle != kXamModuleAddress && module_handle != kModuleAddress)
        return kNtInvalidHandle;
    // The original title queries XAM Party ordinals 2815, 2816, 2827,
    // 2832, 773 and 779. No native Party implementation is available yet:
    // report missing exports, never invent callable guest PPC addresses.
    (void)ordinal;
    put32(*memory, output_pointer, 0U);
    return kNtDriverEntryNotFound;
}
std::uint32_t bound_xex_unload_image(std::uint32_t module_handle) {
    std::lock_guard lock(module_mutex);
    auto* memory = bound_system_memory.load(std::memory_order_acquire);
    if (!memory) return kNtNotSupported;
    if (module_handle != kXamModuleAddress) return kNtInvalidHandle;
    if (!memory->committed_range(kXamModuleAddress + 0x40U, 2U))
        return kNtInvalidHandle;
    const auto count = be16(memory->address(kXamModuleAddress + 0x40U));
    if (count) put16(*memory, kXamModuleAddress + 0x40U, std::uint16_t(count - 1));
    return 0U; // Kernel XAM module stays loaded even at reference count zero.
}
std::uint32_t install_original_xex_title_header(
    GuestMemory& memory, const std::filesystem::path& original_xex,
    std::uint32_t expected_entry) {
    std::ifstream input(original_xex, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("Cannot open private original XEX2 file");
    const auto length = input.tellg();
    if (length < 24) throw std::runtime_error("XEX2 file truncated before header");
    input.seekg(0);
    std::array<std::uint8_t, 24> fixed{};
    if (!input.read(reinterpret_cast<char*>(fixed.data()), fixed.size()))
        throw std::runtime_error("Cannot read XEX2 fixed header");
    if (be32(fixed.data()) != 0x58455832U)
        throw std::runtime_error("Expected original Xbox 360 XEX2 file");
    const auto header_size = be32(fixed.data() + 8);
    const auto header_count = be32(fixed.data() + 20);
    if (header_size < fixed.size() || header_size > kMaximumHeaderSize ||
        static_cast<std::uint64_t>(header_size) >
            static_cast<std::uint64_t>(length) ||
        header_count > (header_size - fixed.size()) / 8)
        throw std::runtime_error("Invalid XEX2 header size or optional field count");
    std::vector<std::uint8_t> bytes(header_size);
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), bytes.size()))
        throw std::runtime_error("Failed to read complete private XEX2 header");
    if (!memory.committed_range(kTimeStampBundleImport, 4) ||
        !memory.committed_range(kOriginalModuleImport, 4) ||
        !memory.committed_range(kHardwareInfoImport, 4) ||
        !memory.committed_range(kDebugMonitorImport, 4) ||
        !memory.committed_range(kCommandLineImport, 4) ||
        !memory.committed_range(kThreadTypeImport, 4))
        throw std::runtime_error("Load original decrypted PE before XEX2 module wiring");
    const auto timestamp_import_tag = be32(memory.address(kTimeStampBundleImport));
    if (timestamp_import_tag != 0 && timestamp_import_tag != 0x000100ADU)
        throw std::runtime_error("Unexpected KeTimeStampBundle import ordinal");
    const auto hardware_import_tag = be32(memory.address(kHardwareInfoImport));
    if (hardware_import_tag != 0 && hardware_import_tag != 0x00010156U)
        throw std::runtime_error("Unexpected XboxHardwareInfo import ordinal");
    const auto debug_import_tag = be32(memory.address(kDebugMonitorImport));
    if (debug_import_tag != 0 && debug_import_tag != 0x00010059U)
        throw std::runtime_error("Unexpected KeDebugMonitorData import ordinal");
    const auto command_line_import_tag = be32(memory.address(kCommandLineImport));
    if (command_line_import_tag != 0 && command_line_import_tag != 0x000101AEU)
        throw std::runtime_error("Unexpected ExLoadedCommandLine import ordinal");
    const auto thread_type_tag = be32(memory.address(kThreadTypeImport));
    if (thread_type_tag != 0 && thread_type_tag != 0x0001001BU)
        throw std::runtime_error("Unexpected ExThreadObjectType import ordinal");
    if (memory.any_committed_range(kHeaderAddress, kMaximumHeaderSize) ||
        memory.any_committed_range(kModuleHandleAddress, 4096) ||
        memory.any_committed_range(kModuleAddress, 4096) ||
        memory.any_committed_range(kXamModuleAddress, 4096) ||
        memory.any_committed_range(kXamModuleNameAddress, 4096) ||
        memory.any_committed_range(kTimeStampBundleAddress, 4096) ||
        memory.any_committed_range(kHardwareInfoAddress, 4096) ||
        memory.any_committed_range(kDebugMonitorAddress, 4096) ||
        memory.any_committed_range(kCommandLineAddress, 4096) ||
        memory.any_committed_range(kThreadTypeAddress, 4096))
        throw std::runtime_error("XEX2 module boot memory overlaps existing guest state");
    // Validate the executable entry BEFORE touching any guest memory.
    bool matched_entry = false;
    bool matched_title = false;
    for (std::uint32_t i = 0; i < header_count; ++i) {
        const auto* field = bytes.data() + 24 + i * 8;
        const auto key = be32(field);
        const auto value = be32(field + 4);
        if (key == kXexEntryKey) {
            if (matched_entry || value != expected_entry)
                throw std::runtime_error("Original XEX2 entry does not match linked _xstart");
            matched_entry = true;
        } else if (key == kExecutionIdKey) {
            // XEX execution ID contains the title ID at byte offset 12.
            // Validate all bytes before committing any private header to guest RAM.
            if (matched_title || value > header_size ||
                header_size - value < 24 ||
                be32(bytes.data() + value + 12) != kDotmTitleId)
                throw std::runtime_error("XEX2 execution ID is invalid or not DOTM");
            matched_title = true;
        }
    }
    if (!matched_entry || !matched_title)
        throw std::runtime_error("Original DOTM XEX2 entry or execution ID missing");
    memory.copy_to_guest(kHeaderAddress, bytes);
    memory.commit(kModuleHandleAddress, 4096);
    memory.commit(kModuleAddress, 4096);
    memory.commit(kXamModuleAddress, 4096);
    memory.commit(kXamModuleNameAddress, 4096);
    memory.commit(kTimeStampBundleAddress, 4096);
    memory.commit(kHardwareInfoAddress, 4096);
    memory.commit(kDebugMonitorAddress, 4096);
    memory.commit(kCommandLineAddress, 4096);
    memory.commit(kThreadTypeAddress, 4096);
    // System XAM module uses an Xbox 360 LDR_DATA_TABLE_ENTRY as HMODULE.
    // No proprietary xam.xex is loaded: only the built-in native service handle.
    std::memset(memory.address(kXamModuleAddress), 0, 4096);
    std::memset(memory.address(kXamModuleNameAddress), 0, 4096);
    constexpr char kXamName[] = "xam.xex";
    for (std::uint32_t i = 0; i < sizeof(kXamName) - 1; ++i)
        memory.address(kXamModuleNameAddress + 2 * i)[1] =
            static_cast<std::uint8_t>(kXamName[i]);
    put16(memory, kXamModuleAddress + 0x24U, 14U);
    put16(memory, kXamModuleAddress + 0x26U, 16U);
    put32(memory, kXamModuleAddress + 0x28U, kXamModuleNameAddress);
    put16(memory, kXamModuleAddress + 0x2CU, 14U);
    put16(memory, kXamModuleAddress + 0x2EU, 16U);
    put32(memory, kXamModuleAddress + 0x30U, kXamModuleNameAddress);
    // Xbox 360 kernel KeTimeStampBundle is a 24-byte data export.
    // The 32-bit millisecond uptime at +16 is updated during title execution.
    std::memset(memory.address(kTimeStampBundleAddress), 0, 4096);
    put32(memory, kTimeStampBundleImport, kTimeStampBundleAddress);
    // Retail hardware flags and six logical processors; no active debug
    // monitor callback. These are real guest-visible global data imports.
    std::memset(memory.address(kHardwareInfoAddress), 0, 4096);
    put32(memory, kHardwareInfoAddress, kRetailHardwareFlags);
    memory.address(kHardwareInfoAddress + 4)[0] = kHardwareCpuCount;
    put32(memory, kHardwareInfoImport, kHardwareInfoAddress);
    std::memset(memory.address(kDebugMonitorAddress), 0, 4096);
    put32(memory, kDebugMonitorImport, kDebugMonitorAddress);
    std::memset(memory.address(kCommandLineAddress), 0, 4096);
    constexpr char command_line[] = "\"default.xex\"";
    std::memcpy(memory.address(kCommandLineAddress), command_line, sizeof(command_line));
    put32(memory, kCommandLineImport, kCommandLineAddress);
    // Native kernel object type identity, used for checked handle references.
    std::memset(memory.address(kThreadTypeAddress), 0, 4096);
    put32(memory, kThreadTypeAddress, 27U);
    constexpr char thread_name[] = "Thread";
    std::memcpy(memory.address(kThreadTypeAddress + 4U), thread_name, sizeof(thread_name));
    put32(memory, kThreadTypeImport, kThreadTypeAddress);
    put32(memory, kModuleHandleAddress, kModuleAddress);
    put32(memory, kModuleAddress + 0x58, kHeaderAddress);
    put32(memory, kOriginalModuleImport, kModuleHandleAddress);
    if (xex_optional_header_field(memory, kHeaderAddress, kXexEntryKey) != expected_entry)
        throw std::runtime_error("Mapped original XEX2 entry failed readback");
    return kHeaderAddress;
}
}
