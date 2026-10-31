#include "xex_optional_header.hpp"
#include "guest_memory.hpp"
#include <atomic>
#include <cstdint>
#include <stdexcept>
namespace dotm {
namespace {
constexpr std::uint32_t kXex2Magic = 0x58455832U; // "XEX2"
constexpr std::uint32_t kFixedHeaderSize = 24;
constexpr std::uint32_t kMaximumHeaderSize = 8U * 1024U * 1024U;
std::atomic<GuestMemory*> bound_memory{nullptr};
std::uint32_t read_be32(GuestMemory& m, std::uint32_t addr) {
    if (!m.committed_range(addr, 4))
        throw std::runtime_error("Unmapped XEX2 header field");
    const auto* p = m.address(addr);
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
           (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
}
}
std::uint32_t xex_optional_header_field(GuestMemory& memory, std::uint32_t header_address,
                                        std::uint32_t key) {
    if (!header_address) return 0;
    if (!memory.committed_range(header_address, kFixedHeaderSize))
        throw std::runtime_error("Unmapped XEX2 header");
    if (read_be32(memory, header_address) != kXex2Magic)
        throw std::runtime_error("Expected original XEX2 header");
    const std::uint32_t header_bytes = read_be32(memory, header_address + 8);
    const std::uint32_t count = read_be32(memory, header_address + 20);
    if (header_bytes < kFixedHeaderSize || header_bytes > kMaximumHeaderSize ||
        count > (header_bytes - kFixedHeaderSize) / 8 ||
        std::uint64_t(header_address) + header_bytes > GuestMemory::kAddressSpaceSize ||
        !memory.committed_range(header_address, header_bytes))
        throw std::runtime_error("Invalid XEX2 optional header table");
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t entry = header_address + kFixedHeaderSize + i * 8;
        if (read_be32(memory, entry) != key) continue;
        const std::uint32_t value = read_be32(memory, entry + 4);
        switch (key & 0xFFU) {
        case 0x00: // Immediate field, exactly as encoded by XEX2.
            return value;
        case 0x01: // Guest pointer to the inline 32-bit value.
            return entry + 4;
        default:   // Guest pointer to data stored elsewhere in the XEX2 header.
            if (value >= header_bytes)
                throw std::runtime_error("XEX2 optional header offset outside header");
            return header_address + value;
        }
    }
    return 0; // Optional field absent in the actual input header.
}
void bind_xex_optional_header(GuestMemory* memory) noexcept {
    bound_memory.store(memory, std::memory_order_release);
}
std::uint32_t bound_xex_optional_header_field(std::uint32_t header_address,
                                              std::uint32_t key) {
    GuestMemory* memory = bound_memory.load(std::memory_order_acquire);
    if (!memory) throw std::runtime_error("XEX2 header service not bound");
    return xex_optional_header_field(*memory, header_address, key);
}
}
