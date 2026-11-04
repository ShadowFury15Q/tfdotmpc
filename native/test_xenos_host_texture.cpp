#include "xenos_host_texture.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
std::vector<std::uint8_t> guest;
std::vector<std::uint8_t> reader(std::uint32_t physical, std::uint32_t count) {
    if (physical != 0x4000U || count > guest.size())
        throw std::runtime_error("Out-of-bounds original texture DMA test");
    return {guest.begin(), guest.begin() + count};
}
void require(bool valid, const char* message) {
    if (!valid) throw std::runtime_error(message);
}
template <class F> void must_reject(F operation, const char* message) {
    bool rejected = false;
    try { operation(); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, message);
}
}  // namespace

int main() {
    try {
        dotm::GpuDrawCommand draw;
        draw.copy_physical_memory = &reader;
        dotm::XenosTextureFetchConstant tex{};
        tex.slot = 3;
        tex.base_physical_address = 0x4000U;
        tex.dimension = 1;
        tex.format = 6;  // Original Xenos 8_8_8_8.
        tex.width = 5;
        tex.height = 3;
        tex.pitch_pixels = 8;
        draw.texture_fetch_constants.push_back(tex);
        guest.resize(8U * 3U * 4U, 0xDD);
        for (std::uint32_t y = 0; y < 3; ++y)
            for (std::uint32_t x = 0; x < 5; ++x)
                for (std::uint32_t c = 0; c < 4; ++c)
                    guest[(y * 8U + x) * 4U + c] =
                        static_cast<std::uint8_t>(y * 40U + x * 4U + c);
        auto upload = dotm::prepare_xenos_host_texture(draw, 3);
        require(upload.format == dotm::XenosHostTextureFormat::rgba8 &&
                upload.row_pitch_bytes == 20 && upload.texels.size() == 60,
                "Linear Xenos RGBA8 rows must be packed without guest pitch");
        for (std::uint32_t y = 0; y < 3; ++y)
            for (std::uint32_t x = 0; x < 5; ++x)
                for (std::uint32_t c = 0; c < 4; ++c)
                    require(upload.texels[(y * 5U + x) * 4U + c] ==
                                y * 40U + x * 4U + c,
                            "Linear guest DMA texels must survive pitch removal");

        auto& binding = draw.texture_fetch_constants[0];
        binding.format = 18;  // Compressed DXT1 / BC1, 4x4 blocks.
        binding.width = 128;
        binding.height = 128;
        binding.pitch_pixels = 128;
        binding.tiled = true;
        const dotm::XenosTiled2DLayout tiles{32, 32, 32, 8};
        guest.assign(dotm::xenos_tiled_2d_required_bytes(tiles), 0);
        for (std::uint32_t y = 0; y < 32; ++y)
            for (std::uint32_t x = 0; x < 32; ++x) {
                const auto pos = dotm::xenos_tiled_2d_offset(x, y, 32, 8);
                for (std::uint32_t c = 0; c < 8; ++c)
                    guest[pos + c] = static_cast<std::uint8_t>((x + y * 3U + c * 7U) & 255U);
            }
        upload = dotm::prepare_xenos_host_texture(draw, 3);
        require(upload.format == dotm::XenosHostTextureFormat::bc1 &&
                upload.row_pitch_bytes == 256 &&
                upload.texels.size() == 32U * 32U * 8U,
                "Native tiled BC1 upload keeps compressed 4x4 units");
        for (std::uint32_t y = 0; y < 32; ++y)
            for (std::uint32_t x = 0; x < 32; ++x)
                for (std::uint32_t c = 0; c < 8; ++c)
                    require(upload.texels[(y * 32U + x) * 8U + c] ==
                                static_cast<std::uint8_t>((x + y * 3U + c * 7U) & 255U),
                            "Tiled Xenos BC1 compressed bytes must be detiled exactly");

        for (const auto [format, expected] :
             {std::pair{19U, dotm::XenosHostTextureFormat::bc2},
              std::pair{20U, dotm::XenosHostTextureFormat::bc3},
              std::pair{49U, dotm::XenosHostTextureFormat::bc5}}) {
            binding.format = format;
            const dotm::XenosTiled2DLayout larger{32, 32, 32, 16};
            guest.assign(dotm::xenos_tiled_2d_required_bytes(larger), 0xAB);
            upload = dotm::prepare_xenos_host_texture(draw, 3);
            require(upload.format == expected &&
                    upload.row_pitch_bytes == 512 &&
                    upload.texels.size() == 32U * 32U * 16U,
                    "Native tiled 16-byte BC block format");
        }
        must_reject([&] { (void)dotm::prepare_xenos_host_texture(draw, 4); },
                    "Unbound fetch slots cannot upload");
        binding.format = 7;
        must_reject([&] { (void)dotm::prepare_xenos_host_texture(draw, 3); },
                    "Unsupported texture format cannot upload as RGBA");
        binding.format = 20;
        binding.mip_min_level = 1;
        must_reject([&] { (void)dotm::prepare_xenos_host_texture(draw, 3); },
                    "Missing required base mip must not be fabricated");
        binding.mip_min_level = 0;
        binding.stacked = true;
        must_reject([&] { (void)dotm::prepare_xenos_host_texture(draw, 3); },
                    "Stacked texture cannot be treated as ordinary 2D");
        binding.stacked = false;
        binding.pitch_pixels = 64;
        must_reject([&] { (void)dotm::prepare_xenos_host_texture(draw, 3); },
                    "Guest pitch smaller than width is invalid");
        std::cout << "Original Xenos host RGBA8 and BC1/2/3/5 texture DMA PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
