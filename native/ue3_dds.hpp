#pragma once
#include "ue3_package.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace dotm {
// Reads GPU-tiled Xbox 360 BC1/BC3 mip data and converts to a standard
// single-mip DDS file. Does not implement rendering or UE3 material graph logic.
std::vector<std::uint8_t> convert_xbox_tiled_bc_to_dds(
    std::uint32_t width, std::uint32_t height, bool bc3,
    std::span<const std::uint8_t> xenos_payload);
// Returns nullopt when the Texture2D is not DXT1/DXT5 or has no local mip.
// Selection is the first available embedded or LZO mip (may be lower resolution
// than original if the higher mips are streamed externally).
std::optional<std::vector<std::uint8_t>> export_ue3_texture_dds(
    const Ue3Package& package, std::size_t export_index);
}