#pragma once
#include "gpu_engines.hpp"
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace dotm {
// A validated host upload, derived exclusively from the active PM4 fetch
// descriptor and guest physical memory. No substitute image is generated.
struct XenosTextureUpload {
    std::uint32_t slot = 0, width = 0, height = 0, row_pitch = 0;
    std::uint32_t format = 0;
    std::vector<std::uint8_t> bytes;
};
inline XenosTextureUpload prepare_xenos_texture_2d(
    const GpuDrawCommand& draw, std::uint32_t slot) {
    const auto it = std::find_if(draw.texture_fetch_constants.begin(),
        draw.texture_fetch_constants.end(),
        [slot](const XenosTextureFetchConstant& b) { return b.slot == slot; });
    if (it == draw.texture_fetch_constants.end())
        throw std::runtime_error("Original Xenos texture fetch slot is unbound");
    const auto& b = *it;
    if (b.dimension != 1 || b.stacked || b.packed_mips ||
        b.mip_min_level != 0 || b.mip_max_level != 0)
        throw std::runtime_error("Unsupported Xenos texture dimension or mip chain");
    std::uint32_t block = 1, element_bytes = 0;
    switch (b.format) {
        case 6: case 54: element_bytes = 4; break;  // RGBA8 / packed RGB10A2
        case 18: block = 4; element_bytes = 8; break; // DXT1
        case 19: case 20: block = 4; element_bytes = 16; break; // DXT3/5
        case 49: block = 4; element_bytes = 16; break; // DXN
        default: throw std::runtime_error("Unsupported Xenos texture upload format");
    }
    if (!b.width || !b.height || b.width > 8192 || b.height > 8192)
        throw std::runtime_error("Invalid original Xenos texture dimensions");
    const std::uint32_t w = (b.width + block - 1) / block;
    const std::uint32_t h = (b.height + block - 1) / block;
    const std::uint32_t pitch_pixels = b.pitch_pixels ? b.pitch_pixels : b.width;
    const std::uint32_t pitch = (pitch_pixels + block - 1) / block;
    if (pitch < w || std::uint64_t(w) * h * element_bytes > 64ULL * 1024 * 1024)
        throw std::runtime_error("Invalid original Xenos texture footprint");
    XenosTextureUpload out;
    out.slot = slot; out.width = b.width; out.height = b.height;
    out.row_pitch = w * element_bytes; out.format = b.format;
    if (b.tiled) {
        out.bytes = draw.copy_tiled_texture_2d_elements(
            slot, {w, h, pitch, element_bytes});
    } else {
        const std::uint64_t guest_bytes =
            (std::uint64_t(h - 1) * pitch + w) * element_bytes;
        if (guest_bytes > 64ULL * 1024 * 1024)
            throw std::runtime_error("Oversized linear Xenos texture");
        const auto guest = draw.copy_texture_fetch_range(
            slot, 0, static_cast<std::uint32_t>(guest_bytes));
        out.bytes.resize(std::size_t(w) * h * element_bytes);
        for (std::uint32_t y = 0; y < h; ++y)
            std::copy_n(guest.begin() + std::size_t(y) * pitch * element_bytes,
                out.row_pitch, out.bytes.begin() + std::size_t(y) * out.row_pitch);
    }
    return out;
}
} // namespace dotm
