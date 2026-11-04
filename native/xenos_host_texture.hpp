#pragma once
#include "gpu_engines.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace dotm {

// GPU-backend-independent, draw-scoped upload of ACTUAL Xbox 360 texture DMA.
// Format remains native (including BC blocks); no synthetic image or texels.
// The caller must create a host texture while the original draw is active.
enum class XenosHostTextureFormat { rgba8, bc1, bc2, bc3, bc5 };
struct XenosHostTextureUpload {
    std::uint32_t fetch_slot = 0;
    std::uint32_t width = 0, height = 0, row_pitch_bytes = 0;
    XenosHostTextureFormat format{};
    std::vector<std::uint8_t> texels;
};

inline XenosHostTextureUpload prepare_xenos_host_texture(
    const GpuDrawCommand& draw, std::uint32_t slot) {
    const auto it = std::find_if(draw.texture_fetch_constants.begin(),
        draw.texture_fetch_constants.end(),
        [slot](const XenosTextureFetchConstant& t) { return t.slot == slot; });
    if (it == draw.texture_fetch_constants.end())
        throw std::runtime_error("Original Xenos texture fetch slot is unbound");
    const auto& t = *it;
    if (t.dimension != 1U || t.stacked || t.packed_mips)
        throw std::runtime_error("Xenos host upload requires non-stacked 2D base image");
    if (!t.width || !t.height || t.width > 8192U || t.height > 8192U)
        throw std::runtime_error("Invalid Xenos 2D texture dimensions");
    // A base-level upload cannot silently pretend to provide the original
    // mip chain or render-target feedback. Fail rather than sample wrong LOD.
    if (t.mip_min_level)
        throw std::runtime_error("Original Xenos sampler requires missing base mip level");

    XenosHostTextureUpload result;
    result.fetch_slot = slot;
    result.width = t.width;
    result.height = t.height;
    std::uint32_t block_size = 1U, bytes_per_block = 4U;
    switch (t.format) {
        case 6U: result.format = XenosHostTextureFormat::rgba8; break;
        case 18U: case 51U:
            result.format = XenosHostTextureFormat::bc1;
            block_size = 4U; bytes_per_block = 8U; break;
        case 19U: case 52U:
            result.format = XenosHostTextureFormat::bc2;
            block_size = 4U; bytes_per_block = 16U; break;
        case 20U: case 53U:
            result.format = XenosHostTextureFormat::bc3;
            block_size = 4U; bytes_per_block = 16U; break;
        case 49U:
            result.format = XenosHostTextureFormat::bc5;
            block_size = 4U; bytes_per_block = 16U; break;
        default:
            throw std::runtime_error("Original Xenos texture format not mapped to host GPU");
    }
    const std::uint32_t w = (t.width + block_size - 1U) / block_size;
    const std::uint32_t h = (t.height + block_size - 1U) / block_size;
    const std::uint32_t pitch_pixels =
        t.pitch_pixels ? t.pitch_pixels : t.width;
    if (pitch_pixels < t.width)
        throw std::runtime_error("Original Xenos texture pitch smaller than width");
    const std::uint32_t pitch = (pitch_pixels + block_size - 1U) / block_size;
    const auto packed_pitch = std::uint64_t(w) * bytes_per_block;
    const auto packed_size = packed_pitch * h;
    if (packed_size > 64ULL * 1024U * 1024U ||
        packed_pitch > std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error("Original Xenos texture exceeds GPU upload limit");
    result.row_pitch_bytes = static_cast<std::uint32_t>(packed_pitch);
    if (t.tiled) {
        result.texels = draw.copy_tiled_texture_2d_elements(
            slot, XenosTiled2DLayout{w, h, pitch, bytes_per_block});
    } else {
        const auto physical_pitch = std::uint64_t(pitch) * bytes_per_block;
        const auto required = physical_pitch * (h - 1U) + packed_pitch;
        if (required > 64ULL * 1024U * 1024U ||
            required > std::numeric_limits<std::uint32_t>::max())
            throw std::runtime_error("Original linear Xenos texture footprint invalid");
        const auto guest = draw.copy_texture_fetch_range(
            slot, 0U, static_cast<std::uint32_t>(required));
        result.texels.resize(static_cast<std::size_t>(packed_size));
        for (std::uint32_t row = 0; row < h; ++row) {
            const auto begin = guest.begin() +
                static_cast<std::size_t>(row) * physical_pitch;
            std::copy_n(begin, result.row_pitch_bytes,
                result.texels.begin() + static_cast<std::size_t>(row) * result.row_pitch_bytes);
        }
    }
    if (result.texels.size() != packed_size)
        throw std::runtime_error("Incomplete original Xenos GPU texture upload");
    return result;
}
}  // namespace dotm
