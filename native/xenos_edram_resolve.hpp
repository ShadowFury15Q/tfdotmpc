#pragma once
#include "gpu_engines.hpp"
#include "xenos_texture_tiling.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <vector>

namespace dotm {

// Validated Xenos RT0 color resolve from genuine GPU output, not a replacement pass.
// Caller must supply a DX11 source that actually contains color RT0 and
// write through GpuDrawCommand::write_physical_memory before ACKing PM4.
// Guest VF0 subrectangles are supported when source/destination offsets are zero.
// Other EDRAM bases, MSAA, depth and format conversions fail closed.
struct XenosColorSurfaceResolve {
    std::uint32_t physical_address = 0;
    XenosTiled2DLayout tiled_layout{};
    std::size_t required_physical_bytes = 0;
    std::uint32_t left = 0, top = 0, right = 0, bottom = 0;
};
inline std::optional<XenosColorSurfaceResolve> prepare_xenos_surface_resolve(
    const GpuDrawCommand& draw, std::uint32_t target_width,
    std::uint32_t target_height) {
    constexpr std::uint32_t kSurfaceInfo = 0x2000U;
    constexpr std::uint32_t kColorInfo = 0x2001U;
    constexpr std::uint32_t kModeControl = 0x2208U;
    constexpr std::uint32_t kCopyControl = 0x2318U;
    constexpr std::uint32_t kCopyBase = 0x2319U;
    constexpr std::uint32_t kCopyPitch = 0x231AU;
    constexpr std::uint32_t kCopyInfo = 0x231BU;
    constexpr std::uint32_t kPixelCenter = 0x2302U;
    constexpr std::uint32_t kScMode = 0x2205U;
    if (draw.register_words.size() <= kCopyInfo)
        throw std::runtime_error("Missing original PM4 RB resolve registers");
    const auto& r = draw.register_words;
    const auto mode = r[kModeControl] & 7U;
    if (mode != 6U) return std::nullopt;  // Not an EDRAM copy draw.
    if (!target_width || !target_height || target_width > 8192U ||
        target_height > 8192U)
        throw std::runtime_error("Invalid original Xenos resolve dimensions");
    const auto ctrl = r[kCopyControl];
    const auto source_index = ctrl & 7U;
    const auto samples = (ctrl >> 4U) & 7U;
    const auto command = (ctrl >> 20U) & 3U;
    // RT0, sample0, raw/convert of identical unorm RGBA8 bytes; no clear.
    if (source_index || samples || command > 1U ||
        (ctrl & ((1U << 8U) | (1U << 9U))) ||
        (r[kSurfaceInfo] & 0x3FFFU) != target_width ||
        ((r[kSurfaceInfo] >> 16U) & 3U) != 0U ||
        (r[kColorInfo] & 0xFFFU) != 0U ||
        ((r[kColorInfo] >> 16U) & 15U) != 0U ||
        ((r[kColorInfo] >> 20U) & 63U) != 0U)
        throw std::runtime_error(
            "Original resolve needs separate RT source, MSAA or clear support");
    const auto dst_info = r[kCopyInfo];
    // Xenos ColorFormat 6 = RGBA8, unsigned repeating fraction. The native
    // D3D11 source is the same storage format. No exponent/swizzle/endian.
    if (((dst_info >> 7U) & 63U) != 6U ||
        (dst_info & ~(63U << 7U)) != 0U)
        throw std::runtime_error(
            "Original EDRAM resolve requires unsupported destination conversion");
    const auto pitch = r[kCopyPitch] & 0x3FFFU;
    const auto height = (r[kCopyPitch] >> 16U) & 0x3FFFU;
    const auto physical = r[kCopyBase];
    if (pitch < target_width || height < target_height ||
        !physical || (physical & 0xFFFU))
        throw std::runtime_error("Invalid original tiled EDRAM resolve destination");
    // Xenia derives the copy coverage from three original VF0 XY vertices,
    // not from the PM4 DRAW ordinal or a synthetic fullscreen rectangle.
    // Nonzero copy-window offsets require separate source/destination mapping.
    if ((r[kScMode] & (1U << 16U)) || draw.index_count != 3U)
        throw std::runtime_error("Original resolve requires unsupported window offset or vertex count");
    const auto slot = std::find_if(
        draw.vertex_fetch_constants.begin(), draw.vertex_fetch_constants.end(),
        [](const XenosVertexFetchConstant& v) { return v.slot == 0U; });
    if (slot == draw.vertex_fetch_constants.end() || slot->size_dwords != 6U)
        throw std::runtime_error("Original resolve VF0 must be exactly 3 float2 vertices");
    const auto bytes = draw.copy_vertex_fetch_range(0U, 0U, 24U);
    if (bytes.size() != 24U)
        throw std::runtime_error("Incomplete Xenos resolve VF0 physical data");
    float positions[6]{};
    std::memcpy(positions, bytes.data(), sizeof(positions));
    const float offset = (r[kPixelCenter] & 1U) == 0U ? 0.5f : 0.0f;
    std::int32_t coords[6]{};
    for (unsigned i = 0; i < 6U; ++i) {
        const float shifted = positions[i] + offset;
        if (!std::isfinite(shifted) ||
            std::fabs(shifted) > 16384.0f ||
            std::fabs(shifted - std::round(shifted)) > 0.002f)
            throw std::runtime_error("Original resolve vertex is not pixel-aligned");
        coords[i] = static_cast<std::int32_t>(std::round(shifted));
    }
    const auto x0 = std::min({coords[0], coords[2], coords[4]});
    const auto y0 = std::min({coords[1], coords[3], coords[5]});
    const auto x1 = std::max({coords[0], coords[2], coords[4]});
    const auto y1 = std::max({coords[1], coords[3], coords[5]});
    if (x0 < 0 || y0 < 0 || x1 <= x0 || y1 <= y0 ||
        x1 > static_cast<std::int32_t>(target_width) ||
        y1 > static_cast<std::int32_t>(target_height))
        throw std::runtime_error("Original EDRAM resolve rectangle exceeds color attachment");
    // Three unique axis-aligned corners, rather than a made-up bounding box.
    bool seen[4]{};
    for (unsigned i = 0; i < 3; ++i) {
        const auto x = coords[i * 2U], y = coords[i * 2U + 1U];
        if ((x != x0 && x != x1) || (y != y0 && y != y1))
            throw std::runtime_error("Non-rectangular original EDRAM resolve vertices");
        const auto corner = (x == x1 ? 1U : 0U) | (y == y1 ? 2U : 0U);
        if (seen[corner]) throw std::runtime_error("Repeated original resolve corner");
        seen[corner] = true;
    }
    const XenosTiled2DLayout layout{target_width, target_height, pitch, 4U};
    const auto required = xenos_tiled_2d_required_bytes(layout);
    if (std::uint64_t(physical) + required > 0x100000000ULL)
        throw std::runtime_error("Original EDRAM tiled resolve wraps physical address");
    return XenosColorSurfaceResolve{
        physical, layout, required,
        static_cast<std::uint32_t>(x0), static_cast<std::uint32_t>(y0),
        static_cast<std::uint32_t>(x1), static_cast<std::uint32_t>(y1)};
}

// Copy the mapped actual RGBA8 GPU surface to a real physical tiled buffer.
// Preserve all texels/padding outside the guest PM4 resolve rectangle.
inline void write_xenos_color_resolve_rgba8(
    const XenosColorSurfaceResolve& operation,
    const std::uint8_t* source, std::size_t row_pitch, std::uint32_t rows,
    std::vector<std::uint8_t>& destination) {
    if (!source || destination.size() < operation.required_physical_bytes ||
        operation.tiled_layout.bytes_per_element != 4U ||
        operation.left >= operation.right || operation.top >= operation.bottom ||
        operation.right > operation.tiled_layout.width_elements ||
        operation.bottom > operation.tiled_layout.height_elements ||
        rows < operation.bottom ||
        std::uint64_t(row_pitch) < std::uint64_t(operation.right) * 4U)
        throw std::runtime_error("Invalid original partial EDRAM resolve writeback");
    for (auto y = operation.top; y < operation.bottom; ++y) {
        for (auto x = operation.left; x < operation.right; ++x) {
            const auto target = xenos_tiled_2d_offset(
                x, y, operation.tiled_layout.pitch_elements, 4U);
            if (target + 4U > destination.size())
                throw std::runtime_error("Original resolve exceeds guest physical tiles");
            std::memcpy(destination.data() + target,
                        source + std::size_t(y) * row_pitch +
                                 std::size_t(x) * 4U, 4U);
        }
    }
}
} // namespace dotm
