#include "gpu_engines.hpp"
#include <cstdint>
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>
static std::vector<std::uint8_t> test_read_texture_physical(
    std::uint32_t address, std::uint32_t count) {
    if (address < 0x1000U || std::uint64_t(address) + count > 0x1010U)
        throw std::runtime_error("Unallocated test texture memory");
    std::vector<std::uint8_t> bytes(count);
    for (std::uint32_t i = 0; i < count; ++i)
        bytes[i] = static_cast<std::uint8_t>(address - 0x1000U + i);
    return bytes;
}

int main() {
    std::vector<std::uint32_t> regs(0x5003U);
    const auto off = 0x4800U + 6U * 7U;
    regs[off] = 2U | (10U << 22U) | (1U << 31U);
    regs[off + 1] = 0x01234000U | (2U << 6U) | 0x31U;
    regs[off + 2] = 127U | (63U << 13U);
    regs[off + 3] = 0xB59U << 1U;
    regs[off + 4] = (3U << 2U) | (8U << 6U);
    regs[off + 5] = 0x45678000U | (1U << 9U) | (1U << 11U);
    regs[0x4800U] = 3U; // Vertex fetch group must not become a texture.
    auto out = dotm::decode_xenos_texture_fetch_constants(regs);
    if (out.size() != 1U) return 1;
    const auto& t = out[0];
    if (t.slot != 7U || t.base_physical_address != 0x01234000U ||
        t.mip_physical_address != 0x45678000U || t.format != 0x31U ||
        t.endian_mode != 2U || t.dimension != 1U || t.width != 128U ||
        t.height != 64U || t.depth_or_faces != 1U || t.pitch_pixels != 320U ||
        !t.tiled || t.stacked || !t.packed_mips || t.swizzle != 0xB59U ||
        t.mip_min_level != 3U || t.mip_max_level != 8U) return 2;
    regs[off + 5] = (regs[off + 5] & ~(3U << 9U)) | (2U << 9U);
    regs[off + 2] = 3U | (7U << 11U) | (15U << 22U);
    out = dotm::decode_xenos_texture_fetch_constants(regs);
    if (out[0].width != 4U || out[0].height != 8U ||
        out[0].depth_or_faces != 16U) return 3;
    regs[off + 5] = (regs[off + 5] & ~(3U << 9U)) | (3U << 9U);
    regs[off + 2] = 31U | (15U << 13U);
    out = dotm::decode_xenos_texture_fetch_constants(regs);
    if (out[0].width != 32U || out[0].height != 16U ||
        out[0].depth_or_faces != 6U) return 4;
    regs[off + 1] |= 1U << 10U;
    regs[off + 2] |= 2U << 26U;
    out = dotm::decode_xenos_texture_fetch_constants(regs);
    if (out[0].depth_or_faces != 3U) return 5;
    try {
        dotm::decode_xenos_texture_fetch_constants(
            std::vector<std::uint32_t>(0x48BFU));
        return 6;
    } catch (const std::runtime_error&) {}
    // Real DMA lanes must be aligned to physical DWORDs even for partial
    // shader reads; this is not a tiled texture decoder or a renderer.
    dotm::GpuDrawCommand draw;
    draw.texture_fetch_constants = out;
    draw.texture_fetch_constants[0].base_physical_address = 0x1000U;
    draw.copy_physical_memory = &test_read_texture_physical;
    const std::array<std::array<std::uint8_t, 6>, 4> expected = {{
        {{1, 2, 3, 4, 5, 6}}, {{0, 3, 2, 5, 4, 7}},
        {{2, 1, 0, 7, 6, 5}}, {{3, 0, 1, 6, 7, 4}}
    }};
    for (std::uint32_t mode = 0; mode < 4U; ++mode) {
        draw.texture_fetch_constants[0].endian_mode = mode;
        const auto bytes = draw.copy_texture_fetch_range(7U, 1U, 6U);
        if (bytes != std::vector<std::uint8_t>(
                expected[mode].begin(), expected[mode].end())) return 7;
    }
    if (!draw.copy_texture_fetch_range(7U, 0U, 0U).empty()) return 8;
    const auto rejects = [](auto&& operation) {
        try { operation(); } catch (const std::runtime_error&) { return true; }
        return false;
    };
    if (!rejects([&] { draw.copy_texture_fetch_range(7U, 15U, 2U); })) return 9;
    if (!rejects([&] { draw.copy_texture_fetch_range(3U, 0U, 4U); })) return 10;
    draw.texture_fetch_constants[0].base_physical_address = 0xFFFFF000U;
    if (!rejects([&] { draw.copy_texture_fetch_range(7U, 4096U, 4U); })) return 11;
    draw.copy_physical_memory = nullptr;
    if (!rejects([&] { draw.copy_texture_fetch_range(7U, 0U, 4U); })) return 12;
    std::cout << "Xenos texture fetch descriptor decode PASS\n";
    return 0;
}
