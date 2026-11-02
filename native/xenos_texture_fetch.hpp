#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace dotm {
// A Xenos fetch register group is six DWORDs. Texture bindings use one
// whole group, while vertex bindings use three two-DWORD entries per group.
// These are decoded descriptors, not host textures or decoded texels.
struct XenosTextureFetchConstant {
    std::uint32_t slot = 0;
    std::uint32_t base_physical_address = 0;
    std::uint32_t mip_physical_address = 0;
    std::uint32_t format = 0;
    std::uint32_t endian_mode = 0;
    std::uint32_t dimension = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth_or_faces = 0;
    std::uint32_t pitch_pixels = 0;
    std::uint32_t swizzle = 0;
    std::uint32_t mip_min_level = 0;
    std::uint32_t mip_max_level = 0;
    bool tiled = false;
    bool stacked = false;
    bool packed_mips = false;
    std::array<std::uint32_t, 6> raw_words{};
};

inline std::vector<XenosTextureFetchConstant> decode_xenos_texture_fetch_constants(
    const std::vector<std::uint32_t>& registers) {
    constexpr std::size_t kFetchBase = 0x4800U;
    constexpr std::size_t kFetchGroups = 32U;
    if (registers.size() < kFetchBase + kFetchGroups * 6U)
        throw std::runtime_error("Incomplete Xenos texture fetch register bank");
    std::vector<XenosTextureFetchConstant> bindings;
    for (std::uint32_t slot = 0; slot < kFetchGroups; ++slot) {
        const auto offset = kFetchBase + slot * 6U;
        const auto word0 = registers[offset];
        // 2 = texture; 3 = vertex. Do not reinterpret vertex pairs as textures.
        if ((word0 & 3U) != 2U) continue;
        const auto word1 = registers[offset + 1U];
        const auto word2 = registers[offset + 2U];
        const auto word3 = registers[offset + 3U];
        const auto word4 = registers[offset + 4U];
        const auto word5 = registers[offset + 5U];
        XenosTextureFetchConstant binding;
        binding.slot = slot;
        binding.base_physical_address = word1 & 0xFFFFF000U;
        binding.mip_physical_address = word5 & 0xFFFFF000U;
        binding.format = word1 & 0x3FU;
        binding.endian_mode = (word1 >> 6U) & 3U;
        binding.dimension = (word5 >> 9U) & 3U;
        binding.pitch_pixels = ((word0 >> 22U) & 0x1FFU) * 32U;
        binding.tiled = (word0 >> 31U) != 0U;
        binding.stacked = ((word1 >> 10U) & 1U) != 0U;
        binding.packed_mips = ((word5 >> 11U) & 1U) != 0U;
        binding.swizzle = (word3 >> 1U) & 0xFFFU;
        binding.mip_min_level = (word4 >> 2U) & 0xFU;
        binding.mip_max_level = (word4 >> 6U) & 0xFU;
        switch (binding.dimension) {
        case 0U: // 1D: 24-bit width minus one.
            binding.width = (word2 & 0xFFFFFFU) + 1U;
            binding.height = 1U;
            binding.depth_or_faces = 1U;
            break;
        case 1U: // 2D / stacked: 13:13:6 bits, each minus one.
        case 3U: // Cube uses the 2D size layout.
            binding.width = (word2 & 0x1FFFU) + 1U;
            binding.height = ((word2 >> 13U) & 0x1FFFU) + 1U;
            binding.depth_or_faces = binding.stacked
                ? ((word2 >> 26U) & 0x3FU) + 1U
                : (binding.dimension == 3U ? 6U : 1U);
            break;
        case 2U: // 3D: 11:11:10 bits, each minus one.
            binding.width = (word2 & 0x7FFU) + 1U;
            binding.height = ((word2 >> 11U) & 0x7FFU) + 1U;
            binding.depth_or_faces = ((word2 >> 22U) & 0x3FFU) + 1U;
            break;
        }
        binding.raw_words = {word0, word1, word2, word3, word4, word5};
        bindings.push_back(binding);
    }
    return bindings;
}
} // namespace dotm
