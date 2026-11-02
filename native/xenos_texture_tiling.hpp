#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace dotm {
// Xenos 2D texture coordinates are storage elements: pixels for RGBA8,
// compressed 4x4 blocks for BC1/BC3. Returns the physical byte offset.
// The 1/2-byte element and 3D/stacked texture address modes differ and are
// deliberately rejected until their separate storage layouts are validated.
inline std::uint64_t xenos_tiled_2d_offset(
    std::uint32_t x, std::uint32_t y, std::uint32_t pitch_elements,
    std::uint32_t bytes_per_element) {
    if (!pitch_elements || x >= pitch_elements ||
        (bytes_per_element != 4U && bytes_per_element != 8U &&
         bytes_per_element != 16U))
        throw std::runtime_error("Unsupported Xenos tiled 2D coordinates or element size");
    const std::uint32_t log_bpp = bytes_per_element == 4U ? 2U :
                                  bytes_per_element == 8U ? 3U : 4U;
    // Use 64-bit offsets so malformed textures cannot wrap into guest RAM.
    const std::uint64_t row_macro =
        (std::uint64_t(y >> 5U) * ((std::uint64_t(pitch_elements) + 31U) >> 5U))
        << (log_bpp + 7U);
    const std::uint64_t row_micro = std::uint64_t((y & 6U) << 2U) << log_bpp;
    const std::uint64_t row = row_macro + ((row_micro & ~15ULL) << 1U) +
                              (row_micro & 15U) +
                              (std::uint64_t(y & 8U) << (3U + log_bpp)) +
                              (std::uint64_t(y & 1U) << 4U);
    const std::uint64_t col_micro = std::uint64_t(x & 7U) << log_bpp;
    const std::uint64_t mixed = row +
        (std::uint64_t(x >> 5U) << (log_bpp + 7U)) +
        ((col_micro & ~15ULL) << 1U) + (col_micro & 15U);
    return ((mixed & ~511ULL) << 3U) + ((mixed & 448U) << 2U) +
           (mixed & 63U) + (std::uint64_t(y & 16U) << 7U) +
           (std::uint64_t((((y & 8U) >> 2U) + (x >> 3U)) & 3U) << 6U);
}

struct XenosTiled2DLayout {
    std::uint32_t width_elements;
    std::uint32_t height_elements;
    std::uint32_t pitch_elements;
    std::uint32_t bytes_per_element;
};

inline std::size_t xenos_tiled_2d_required_bytes(const XenosTiled2DLayout& layout) {
    constexpr std::uint64_t kMaxTextureBytes = 64ULL * 1024U * 1024U;
    if (!layout.width_elements || !layout.height_elements ||
        layout.pitch_elements < layout.width_elements ||
        (layout.bytes_per_element != 4U && layout.bytes_per_element != 8U &&
         layout.bytes_per_element != 16U) ||
        std::uint64_t(layout.width_elements) * layout.height_elements *
                layout.bytes_per_element > kMaxTextureBytes)
        throw std::runtime_error("Invalid or oversized Xenos tiled 2D layout");
    std::uint64_t last = 0;
    for (std::uint32_t y = 0; y < layout.height_elements; ++y) {
        for (std::uint32_t x = 0; x < layout.width_elements; ++x) {
            const auto offset = xenos_tiled_2d_offset(
                x, y, layout.pitch_elements, layout.bytes_per_element);
            if (offset > kMaxTextureBytes - layout.bytes_per_element)
                throw std::runtime_error("Xenos tiled texture exceeds bounded guest copy");
            if (offset + layout.bytes_per_element > last)
                last = offset + layout.bytes_per_element;
        }
    }
    return static_cast<std::size_t>(last);
}

// Input must already be fetched from allocated Xbox physical memory and
// transformed from its Xenos DMA endian mode. Compressed blocks are preserved.
inline std::vector<std::uint8_t> detile_xenos_2d_elements(
    const std::vector<std::uint8_t>& tiled, const XenosTiled2DLayout& layout) {
    const auto required = xenos_tiled_2d_required_bytes(layout);
    if (tiled.size() < required)
        throw std::runtime_error("Incomplete Xenos tiled 2D physical allocation");
    const std::size_t bpp = layout.bytes_per_element;
    std::vector<std::uint8_t> linear(
        std::size_t(layout.width_elements) * layout.height_elements * bpp);
    for (std::uint32_t y = 0; y < layout.height_elements; ++y) {
        for (std::uint32_t x = 0; x < layout.width_elements; ++x) {
            const auto source = static_cast<std::size_t>(xenos_tiled_2d_offset(
                x, y, layout.pitch_elements, layout.bytes_per_element));
            const auto target = (std::size_t(y) * layout.width_elements + x) * bpp;
            for (std::size_t lane = 0; lane < bpp; ++lane)
                linear[target + lane] = tiled[source + lane];
        }
    }
    return linear;
}
} // namespace dotm
