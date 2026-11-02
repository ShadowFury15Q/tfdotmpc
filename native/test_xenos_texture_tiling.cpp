#include "xenos_texture_tiling.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
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
        std::cout << "Xenos 2D tiled RGBA/BC block detiling PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
