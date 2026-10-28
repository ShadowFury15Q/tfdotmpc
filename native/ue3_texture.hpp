#pragma once
#include "ue3_package.hpp"
#include <cstddef>
#include <cstdint>
#include <string>

namespace dotm {
// Texture header metadata only; platform-tiled mip data is NOT decoded yet.
struct Ue3TextureInfo {
    std::string object_path;
    std::int32_t width=0;
    std::int32_t height=0;
    std::int32_t original_width=0;
    std::int32_t original_height=0;
    std::int32_t mip_tail_base_index=0;
    std::string format;
    std::size_t native_payload_offset=0;
    std::size_t native_payload_bytes=0;
};
Ue3TextureInfo inspect_ue3_texture2d(const Ue3Package&pkg,std::size_t export_index);
}
