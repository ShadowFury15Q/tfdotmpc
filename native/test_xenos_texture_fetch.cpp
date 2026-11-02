#include "xenos_texture_fetch.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>
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
    std::cout << "Xenos texture fetch descriptor decode PASS\n";
    return 0;
}
