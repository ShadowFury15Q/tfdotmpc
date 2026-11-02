#pragma once
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace dotm {
// Xenos index DMA endian modes permute byte lanes within 32-bit words.
// Return host-native indices for real PC draw calls, not a synthetic draw.
// The caller must provide complete physical DWORDs for modes 2 and 3.
inline std::vector<std::uint32_t> decode_xenos_index_buffer(
    const std::vector<std::uint8_t>& bytes, std::uint32_t index_count,
    std::uint32_t index_size_bytes, std::uint32_t endian_mode) {
    if ((index_size_bytes != 2U && index_size_bytes != 4U) || endian_mode > 3U)
        throw std::runtime_error("Invalid Xenos DMA index format");
    const auto required = std::uint64_t(index_count) * index_size_bytes;
    const auto padded = endian_mode >= 2U ? (required + 3U) & ~std::uint64_t(3U)
                                          : required;
    if (bytes.size() != padded)
        throw std::runtime_error("Incomplete Xenos DMA index dword");
    const std::uint32_t byte_xor = endian_mode == 0U ? 0U :
                                   endian_mode == 1U ? 1U :
                                   endian_mode == 2U ? 3U : 2U;
    std::vector<std::uint32_t> indices;
    indices.reserve(index_count);
    for (std::uint32_t i = 0; i < index_count; ++i) {
        const auto start = std::size_t(i) * index_size_bytes;
        std::uint32_t value = 0U;
        for (std::uint32_t b = 0; b < index_size_bytes; ++b)
            value |= std::uint32_t(bytes[(start + b) ^ byte_xor]) << (8U * b);
        indices.push_back(value);
    }
    return indices;
}
} // namespace dotm
