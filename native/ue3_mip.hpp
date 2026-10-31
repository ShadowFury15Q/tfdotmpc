#pragma once
#include "ue3_package.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dotm {
enum class Ue3MipStorage : std::uint8_t {
    Embedded,      // flags 0x00: Xbox 360 GPU-formatted data in the UE3 package
    LzoEmbedded,   // flags 0x10: independently LZO-compressed data in the package
    External,      // flags 0x11: externally streamed data, not stored here
    Absent         // flags 0x21: no resident bytes in this package
};
struct Ue3Mip {
    Ue3MipStorage storage = Ue3MipStorage::Absent;
    std::uint32_t flags=0;
    std::uint32_t element_count=0;
    std::uint32_t stored_size=0;
    std::uint32_t source_offset=0;
    std::uint32_t opaque_word=0; // preserve unknown four bytes; not presumed to be CRC
    std::uint32_t width=0;
    std::uint32_t height=0;
    std::size_t object_data_offset=0; // only valid for Embedded and LzoEmbedded
};
// Parse only the platform-specific FTexture2DMipMap serialization.
// No Xbox 360 GPU detiling, word swapping or DDS conversion is performed here.
std::vector<Ue3Mip> parse_ue3_texture_mips(const Ue3Package& pkg, std::size_t export_index);
std::vector<std::uint8_t> read_ue3_texture_mip_bytes(
    const Ue3Package& pkg, std::size_t export_index, const Ue3Mip& mip);
}