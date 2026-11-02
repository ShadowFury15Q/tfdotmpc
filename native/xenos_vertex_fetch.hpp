#pragma once
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace dotm {
// Xenos SQ fetch constants occupy 32 six-DWORD groups at 0x4800.
// Each group can hold three independent two-DWORD vertex buffer bindings.
// A binding is only active when its low two type bits equal 3 (vertex).
struct XenosVertexFetchConstant {
    std::uint32_t slot = 0;
    std::uint32_t physical_address = 0;
    std::uint32_t size_dwords = 0;
    std::uint32_t endian_mode = 0;
};

inline std::vector<XenosVertexFetchConstant> decode_xenos_vertex_fetch_constants(
    const std::vector<std::uint32_t>& registers) {
    constexpr std::size_t kFetchBase = 0x4800U;
    constexpr std::size_t kVertexFetchCount = 96U;
    if (registers.size() < kFetchBase + kVertexFetchCount * 2U)
        throw std::runtime_error("Incomplete Xenos vertex fetch register bank");
    std::vector<XenosVertexFetchConstant> bindings;
    for (std::uint32_t slot = 0; slot < kVertexFetchCount; ++slot) {
        const auto word0 = registers[kFetchBase + slot * 2U];
        const auto word1 = registers[kFetchBase + slot * 2U + 1U];
        if ((word0 & 3U) != 3U) continue;
        bindings.push_back({slot, (word0 >> 2U) * 4U,
                            (word1 >> 2U) & 0xFFFFFFU, word1 & 3U});
    }
    // This is metadata only. A renderer must validate and copy the physical
    // allocation through the draw-scoped reader before using any buffer.
    return bindings;
}
} // namespace dotm
