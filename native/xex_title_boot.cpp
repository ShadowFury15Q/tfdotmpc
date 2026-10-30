#include "xex_title_boot.hpp"
#include "guest_memory.hpp"
#include "xex_optional_header.hpp"
#include <array>
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
// The original DOTM sub_827813F8 dereferences [0x82000998] then its handle,
// then reads +0x58 for the XEX header supplied to RtlImageXexHeaderField.
constexpr std::uint32_t kOriginalModuleImport = 0x82000998U;
constexpr std::uint32_t kXexEntryKey = 0x00010100U;
constexpr std::uint32_t kExecutionIdKey = 0x00040006U;
constexpr std::uint32_t kDotmTitleId = 0x415608A4U;
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
    if (!memory.committed_range(kOriginalModuleImport, 4))
        throw std::runtime_error("Load original decrypted PE before XEX2 module wiring");
    if (memory.any_committed_range(kHeaderAddress, kMaximumHeaderSize) ||
        memory.any_committed_range(kModuleHandleAddress, 4096) ||
        memory.any_committed_range(kModuleAddress, 4096))
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
    put32(memory, kModuleHandleAddress, kModuleAddress);
    put32(memory, kModuleAddress + 0x58, kHeaderAddress);
    put32(memory, kOriginalModuleImport, kModuleHandleAddress);
    if (xex_optional_header_field(memory, kHeaderAddress, kXexEntryKey) != expected_entry)
        throw std::runtime_error("Mapped original XEX2 entry failed readback");
    return kHeaderAddress;
}
}
