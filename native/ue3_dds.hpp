#pragma once
#include "ue3_package.hpp"
#include "ue3_mip.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace dotm {
// A non-owning view of one GPU-tiled Xbox 360 BC1/BC3 mip level.
struct XboxTiledBcMip {
    std::uint32_t width=0;
    std::uint32_t height=0;
    std::span<const std::uint8_t> payload;
};
// Converts a contiguous top-to-bottom mip chain to a standard DDS, including
// DDSD_MIPMAPCOUNT and DDSCAPS_COMPLEX/MIPMAP when more than one mip is present.
// All mip payloads must remain alive until the conversion returns.
std::vector<std::uint8_t> convert_xbox_tiled_bc_mips_to_dds(
    std::span<const XboxTiledBcMip> mips, bool bc3);
// Legacy single-mip conversion; retained for existing texture exports.
std::vector<std::uint8_t> convert_xbox_tiled_bc_to_dds(
    std::uint32_t width, std::uint32_t height, bool bc3,
    std::span<const std::uint8_t> xenos_payload);
// Selects the first contiguous resident mip run, skipping missing higher mips.
// Stops at the first missing or dimension-discontinuous level after it starts.
std::vector<Ue3Mip> select_resident_ue3_mip_chain(std::span<const Ue3Mip> mips);
// Returns nullopt when the Texture2D is not DXT1/DXT5 or has no local mip.
// Exports the contiguous embedded/LZO mip chain from the first available mip;
// higher streamed levels may be absent from the resulting DDS.
std::optional<std::vector<std::uint8_t>> export_ue3_texture_dds(
    const Ue3Package& package, std::size_t export_index);
}
