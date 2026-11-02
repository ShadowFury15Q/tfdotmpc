#include "xenos_texture_tiling.hpp"
#include "gpu_engines.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
std::vector<std::uint8_t> test_tiled_guest_bytes;
std::vector<std::uint8_t> test_tiled_reader(std::uint32_t physical,
                                             std::uint32_t length) {
    if (physical != 0x2000U || length > test_tiled_guest_bytes.size())
        throw std::runtime_error("Invalid test tiled physical allocation");
    return {test_tiled_guest_bytes.begin(),
            test_tiled_guest_bytes.begin() + length};
}
void check(bool result, const char* why) {
    if (!result) throw std::runtime_error(why);
}
template <typename F> void rejects(F fn, const char* why) {
    bool threw = false;
    try { fn(); } catch (const std::runtime_error&) { threw = true; }
    check(threw, why);
}
}
int main() {
    try {
        // Xenos address landmarks: macro-tile boundaries and mixed pitches.
        check(dotm::xenos_tiled_2d_offset(0, 0, 64, 4) == 0, "tile origin");
        check(dotm::xenos_tiled_2d_offset(8, 0, 64, 4) == 64, "tile pipe");
        check(dotm::xenos_tiled_2d_offset(0, 1, 64, 4) == 16, "tile y low bit");
        check(dotm::xenos_tiled_2d_offset(0, 32, 64, 4) == 8192, "macro row");
        check(dotm::xenos_tiled_2d_offset(32, 0, 64, 4) == 4096, "macro column");
        check(dotm::xenos_tiled_2d_offset(32, 32, 64, 8) == 24576, "BC1 tile");
        check(dotm::xenos_tiled_2d_offset(32, 32, 64, 16) == 49152, "BC3 tile");
        for (const auto bpp : {4U, 8U, 16U}) {
            for (const auto shape : {dotm::XenosTiled2DLayout{32, 32, 32, bpp},
                                     dotm::XenosTiled2DLayout{37, 35, 64, bpp},
                                     dotm::XenosTiled2DLayout{64, 64, 64, bpp}}) {
                const auto needed = dotm::xenos_tiled_2d_required_bytes(shape);
                std::vector<std::uint8_t> memory(needed, 0xDD);
                std::vector<std::uint8_t> visited(needed / bpp, 0);
                for (std::uint32_t y = 0; y < shape.height_elements; ++y) {
                    for (std::uint32_t x = 0; x < shape.width_elements; ++x) {
                        const auto offset = dotm::xenos_tiled_2d_offset(
                            x, y, shape.pitch_elements, bpp);
                        check(offset + bpp <= needed && offset % bpp == 0,
                              "valid aligned tiled element");
                        check(!visited[offset / bpp], "two texels overlap in memory");
                        visited[offset / bpp] = 1;
                        for (std::uint32_t lane = 0; lane < bpp; ++lane)
                            memory[offset + lane] = static_cast<std::uint8_t>(
                                (x * 3U + y * 17U + lane * 19U) & 255U);
                    }
                }
                const auto decoded = dotm::detile_xenos_2d_elements(memory, shape);
                check(decoded.size() == std::size_t(shape.width_elements) *
                                             shape.height_elements * bpp,
                      "linear image dimensions");
                for (std::uint32_t y = 0; y < shape.height_elements; ++y) {
                    for (std::uint32_t x = 0; x < shape.width_elements; ++x) {
                        for (std::uint32_t lane = 0; lane < bpp; ++lane) {
                            const auto target = (std::size_t(y) * shape.width_elements + x) * bpp + lane;
                            check(decoded[target] == static_cast<std::uint8_t>(
                                      (x * 3U + y * 17U + lane * 19U) & 255U),
                                  "tiled 2D RGBA/BC element decodes exactly");
                        }
                    }
                }
                memory.pop_back();
                rejects([&] { (void)dotm::detile_xenos_2d_elements(memory, shape); },
                        "truncated GPU allocation must be rejected");
            }
        }
        rejects([] { (void)dotm::xenos_tiled_2d_required_bytes({33, 32, 32, 4}); },
                "pitch cannot be less than image width");
        rejects([] { (void)dotm::xenos_tiled_2d_required_bytes({0, 4, 32, 4}); },
                "zero width rejected");
        rejects([] { (void)dotm::xenos_tiled_2d_required_bytes({128, 128, 128, 2}); },
                "unsupported 2-byte elements rejected");
        rejects([] { (void)dotm::xenos_tiled_2d_required_bytes({8192, 8192, 8192, 16}); },
                "oversized texture rejected");
        rejects([] { (void)dotm::xenos_tiled_2d_offset(32, 0, 32, 4); },
                "out-of-pitch access rejected");
        // Drive the actual GPU submission interface: fetch through the
        // allocated physical-reader callback, endian-normalize, then detile.
        const dotm::XenosTiled2DLayout layout{32, 32, 32, 4};
        test_tiled_guest_bytes.assign(
            dotm::xenos_tiled_2d_required_bytes(layout), 0xEE);
        for (std::uint32_t y = 0; y < 32U; ++y) {
            for (std::uint32_t x = 0; x < 32U; ++x) {
                const auto i = dotm::xenos_tiled_2d_offset(x, y, 32, 4);
                for (std::uint32_t lane = 0; lane < 4U; ++lane)
                    test_tiled_guest_bytes[i + lane] =
                        static_cast<std::uint8_t>((x + y * 3U + lane) & 255U);
            }
        }
        dotm::GpuDrawCommand draw;
        dotm::XenosTextureFetchConstant binding{};
        binding.slot = 11U;
        binding.base_physical_address = 0x2000U;
        binding.format = 6U; // Xenos RGBA8, 4 bytes per pixel.
        binding.tiled = true;
        binding.dimension = 1U;
        binding.width = 32U;
        binding.height = 32U;
        draw.texture_fetch_constants.push_back(binding);
        draw.copy_physical_memory = &test_tiled_reader;
        const auto host = draw.copy_tiled_texture_2d_elements(11U, layout);
        check(host.size() == 32U * 32U * 4U,
              "draw-scoped native tiled texture upload dimensions");
        for (std::uint32_t y = 0; y < 32U; ++y) {
            for (std::uint32_t x = 0; x < 32U; ++x) {
                for (std::uint32_t lane = 0; lane < 4U; ++lane) {
                    const auto expected = static_cast<std::uint8_t>(
                        (x + y * 3U + lane) & 255U);
                    check(host[(y * 32U + x) * 4U + lane] == expected,
                          "draw-scoped Xenos DMA texture detiled exactly");
                }
            }
        }
        // A BC1 fetch is measured in pixels (128x128) while the physical
        // storage layout is measured in 4x4 blocks (32x32, 8 bytes each).
        // BC3 and DXN use the same block geometry with 16-byte blocks.
        for (const auto format : {18U, 20U, 49U}) {
            const std::uint32_t block_bytes = format == 18U ? 8U : 16U;
            const dotm::XenosTiled2DLayout blocks{32U, 32U, 32U, block_bytes};
            test_tiled_guest_bytes.assign(
                dotm::xenos_tiled_2d_required_bytes(blocks), 0xCC);
            for (std::uint32_t y = 0; y < 32U; ++y) {
                for (std::uint32_t x = 0; x < 32U; ++x) {
                    const auto offset = dotm::xenos_tiled_2d_offset(
                        x, y, 32U, block_bytes);
                    for (std::uint32_t lane = 0; lane < block_bytes; ++lane)
                        test_tiled_guest_bytes[offset + lane] =
                            static_cast<std::uint8_t>((x * 7U + y * 11U + lane) & 255U);
                }
            }
            auto& texture = draw.texture_fetch_constants[0];
            texture.format = format;
            texture.width = 128U;
            texture.height = 128U;
            texture.pitch_pixels = 128U;
            const auto decoded = draw.copy_tiled_texture_2d_elements(11U, blocks);
            check(decoded.size() == 32U * 32U * block_bytes,
                  "BC texture DMA preserves compressed block dimensions");
            for (std::uint32_t y = 0; y < 32U; ++y) {
                for (std::uint32_t x = 0; x < 32U; ++x) {
                    for (std::uint32_t lane = 0; lane < block_bytes; ++lane)
                        check(decoded[(y * 32U + x) * block_bytes + lane] ==
                                  static_cast<std::uint8_t>(
                                      (x * 7U + y * 11U + lane) & 255U),
                              "BC1/BC3/DXN draw-scoped block DMA");
                }
            }
            rejects([&] {
                (void)draw.copy_tiled_texture_2d_elements(
                    11U, {32U, 32U, 32U, 4U});
            }, "BC texture cannot be interpreted as RGBA8 pixels");
            texture.pitch_pixels = 256U;
            rejects([&] {
                (void)draw.copy_tiled_texture_2d_elements(11U, blocks);
            }, "BC texture cannot ignore original fetch pitch");
        }
        auto& texture = draw.texture_fetch_constants[0];
        texture.format = 6U;
        texture.width = 32U;
        texture.height = 32U;
        texture.pitch_pixels = 0U;
        rejects([&] {
            (void)draw.copy_tiled_texture_2d_elements(11U, {8U, 8U, 32U, 4U});
        }, "RGBA8 layout cannot silently crop bound texture");
        texture.format = 0U;
        rejects([&] {
            (void)draw.copy_tiled_texture_2d_elements(11U, layout);
        }, "unknown Xenos format cannot be interpreted as RGBA8");
        texture.format = 6U;
        draw.texture_fetch_constants[0].stacked = true;
        rejects([&] { (void)draw.copy_tiled_texture_2d_elements(11U, layout); },
                "stacked Xenos texture must not be guessed as 2D");
        draw.texture_fetch_constants[0].stacked = false;
        draw.texture_fetch_constants[0].tiled = false;
        rejects([&] { (void)draw.copy_tiled_texture_2d_elements(11U, layout); },
                "linear textures must not be detiled");
        draw.texture_fetch_constants[0].tiled = true;
        draw.copy_physical_memory = nullptr;
        rejects([&] { (void)draw.copy_tiled_texture_2d_elements(11U, layout); },
                "texture DMA requires active native graphics submission");
        std::cout << "Xenos 2D tiled RGBA/BC block detiling PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
