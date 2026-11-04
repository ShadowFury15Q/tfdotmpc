#include "xenos_texture_upload.hpp"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
std::vector<std::uint8_t> guest;
std::vector<std::uint8_t> read(std::uint32_t address, std::uint32_t size) {
    if (address < 0x2000U || std::uint64_t(address - 0x2000U) + size > guest.size())
        throw std::runtime_error("Unallocated guest physical texture");
    return {guest.begin() + (address - 0x2000U),
            guest.begin() + (address - 0x2000U) + size};
}
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template <typename F> void reject(F&& fn) {
    bool caught = false;
    try { fn(); } catch (const std::runtime_error&) { caught = true; }
    require(caught, "Invalid original texture was accepted");
}
}
int main() {
    dotm::GpuDrawCommand draw;
    draw.copy_physical_memory = &read;
    dotm::XenosTextureFetchConstant binding{};
    binding.slot = 4;
    binding.base_physical_address = 0x2000;
    binding.dimension = 1;
    binding.format = 6;
    binding.width = 8;
    binding.height = 3;
    binding.pitch_pixels = 16;
    draw.texture_fetch_constants.push_back(binding);
    guest.resize(16 * 3 * 4);
    for (std::size_t i = 0; i < guest.size(); ++i) guest[i] = std::uint8_t(i);
    auto linear = dotm::prepare_xenos_texture_2d(draw, 4);
    require(linear.bytes.size() == 8U * 3U * 4U && linear.row_pitch == 32,
            "Linear RGBA guest pitch");
    for (std::uint32_t y = 0; y < 3; ++y)
        require(std::equal(linear.bytes.begin() + y * 32,
                           linear.bytes.begin() + (y + 1) * 32,
                           guest.begin() + y * 64), "Linear Xenos pitch conversion");
    reject([&] { (void)dotm::prepare_xenos_texture_2d(draw, 5); });
    draw.texture_fetch_constants[0].mip_max_level = 2;
    reject([&] { (void)dotm::prepare_xenos_texture_2d(draw, 4); });
    draw.texture_fetch_constants[0].mip_max_level = 0;
    draw.texture_fetch_constants[0].format = 18;
    draw.texture_fetch_constants[0].width = 128;
    draw.texture_fetch_constants[0].height = 128;
    draw.texture_fetch_constants[0].pitch_pixels = 128;
    draw.texture_fetch_constants[0].tiled = true;
    const dotm::XenosTiled2DLayout blocks{32, 32, 32, 8};
    guest.assign(dotm::xenos_tiled_2d_required_bytes(blocks), 0);
    for (std::uint32_t y = 0; y < 32; ++y)
        for (std::uint32_t x = 0; x < 32; ++x) {
            const auto offset = dotm::xenos_tiled_2d_offset(x, y, 32, 8);
            for (std::uint32_t i = 0; i < 8; ++i)
                guest[offset + i] = std::uint8_t((x + y * 17 + i) & 255);
        }
    auto bc1 = dotm::prepare_xenos_texture_2d(draw, 4);
    require(bc1.width == 128 && bc1.height == 128 &&
            bc1.row_pitch == 256 && bc1.bytes.size() == 8192,
            "BC1 original block footprint");
    for (std::uint32_t y = 0; y < 32; ++y)
        for (std::uint32_t x = 0; x < 32; ++x)
            for (std::uint32_t i = 0; i < 8; ++i)
                require(bc1.bytes[(y * 32 + x) * 8 + i] ==
                        std::uint8_t((x + y * 17 + i) & 255),
                        "Original tiled BC1 bytes");
    draw.texture_fetch_constants[0].format = 55;
    reject([&] { (void)dotm::prepare_xenos_texture_2d(draw, 4); });
    std::cout << "Original PM4 RGBA/BC1 texture uploads PASS\n";
}
