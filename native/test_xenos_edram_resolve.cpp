#include "xenos_edram_resolve.hpp"
#include "xenos_edram_color_lifetime.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
std::array<std::uint8_t, 24> vf0{};
std::vector<std::uint8_t> read_guest(std::uint32_t address, std::uint32_t size) {
    if (address != 0x1000U || size != vf0.size())
        throw std::runtime_error("Unexpected original resolve VF0 allocation");
    return {vf0.begin(), vf0.end()};
}
void check(bool yes, const char* message) {
    if (!yes) throw std::runtime_error(message);
}
template<class F> void rejects(F task) {
    bool threw = false;
    try { task(); } catch (const std::runtime_error&) { threw = true; }
    check(threw, "Unsupported original EDRAM mode must fail closed");
}
}
int main() {
    try {
        dotm::GpuDrawCommand draw;
        draw.register_words.resize(0x5003U);
        draw.register_words[0x2208U] = 6U; // Actual RB_MODECONTROL copy mode.
        draw.register_words[0x2000U] = 32U; // Guest RB_SURFACE_INFO pitch.
        draw.register_words[0x2001U] = 0U; // RT0 base0, color RGBA8.
        draw.register_words[0x2318U] = 0U; // Resolve from color RT0, sample0.
        draw.register_words[0x2319U] = 0x4000U; // Original guest physical destination.
        draw.register_words[0x231AU] = 32U | (32U << 16U);
        draw.register_words[0x231BU] = 6U << 7U; // Xenos ColorFormat RGBA8.
        draw.register_words[0x2302U] = 1U; // OGL center, no half-pixel shift.
        draw.index_count = 3U;
        draw.vertex_fetch_constants.push_back({0U, 0x1000U, 6U, 0U});
        draw.copy_physical_memory = &read_guest;
        const std::array<float, 6> coords{0.f,0.f,32.f,0.f,0.f,32.f};
        std::memcpy(vf0.data(), coords.data(), sizeof(coords));
        const auto copy = dotm::prepare_xenos_full_surface_resolve(draw, 32, 32);
        check(copy.has_value() && copy->physical_address == 0x4000U &&
              copy->tiled_layout.pitch_elements == 32U &&
              copy->required_physical_bytes == 4096U,
              "Original full-surface EDRAM copy points to the guest tiled allocation");
        std::vector<std::uint8_t> resolved(copy->required_physical_bytes, 0xAA);
        std::vector<std::uint8_t> original(32U * 32U * 4U);
        for (std::uint32_t y = 0; y < 32U; ++y)
            for (std::uint32_t x = 0; x < 32U; ++x)
                for (std::uint32_t channel = 0; channel < 4U; ++channel) {
                    const auto data = static_cast<std::uint8_t>(
                        (x * 17U + y * 13U + channel * 31U) & 255U);
                    original[(y * 32U + x) * 4U + channel] = data;
                    resolved[dotm::xenos_tiled_2d_offset(x, y, 32U, 4U) +
                             channel] = data;
                }
        check(dotm::detile_xenos_2d_elements(resolved, copy->tiled_layout) ==
                  original, "Guest physical tiled EDRAM resolve preserves every color texel");
        draw.register_words[0x2208U] = 4U; // Ordinary color/depth DRAW.
        check(!dotm::prepare_xenos_full_surface_resolve(draw, 32, 32),
              "Normal original PM4 raster draw is not an EDRAM resolve");
        draw.register_words[0x2208U] = 6U;
        const std::array<float,6> partial{0.f,0.f,16.f,0.f,0.f,32.f};
        std::memcpy(vf0.data(), partial.data(), sizeof(partial));
        rejects([&] { (void)dotm::prepare_xenos_full_surface_resolve(draw,32,32); });
        std::memcpy(vf0.data(), coords.data(), sizeof(coords));
        draw.register_words[0x2000U] |= 1U << 16U; // 2x MSAA.
        rejects([&] { (void)dotm::prepare_xenos_full_surface_resolve(draw,32,32); });
        draw.register_words[0x2000U] = 32U;
        draw.register_words[0x231BU] = 7U << 7U; // 10:10:10:2 not RGBA8.
        rejects([&] { (void)dotm::prepare_xenos_full_surface_resolve(draw,32,32); });
        draw.register_words[0x231BU] = 6U << 7U;
        draw.register_words[0x2319U] = 0x4004U; // Unaligned destination.
        rejects([&] { (void)dotm::prepare_xenos_full_surface_resolve(draw,32,32); });
        draw.register_words[0x2319U] = 0x4000U;
        draw.register_words[0x2318U] = 1U << 8U; // Guest color clear needs separate pass.
        rejects([&] { (void)dotm::prepare_xenos_full_surface_resolve(draw,32,32); });
        draw.register_words[0x2318U] = 0U;
        draw.vertex_fetch_constants.clear();
        rejects([&] { (void)dotm::prepare_xenos_full_surface_resolve(draw,32,32); });
        dotm::XenosEdramColorLifetime lifetime;
        check(!lifetime.can_resolve(), "Uninitialized host EDRAM must not resolve");
        lifetime.on_original_draw_submitted();
        lifetime.on_vd_swap();
        lifetime.on_vd_swap();
        check(lifetime.can_resolve(),
              "Guest EDRAM must survive VdSwap even with zero new draws");
        lifetime.on_render_target_created();
        check(!lifetime.can_resolve(),
              "Resized/recreated host EDRAM must not expose stale color");
        lifetime.on_original_draw_submitted();
        check(lifetime.can_resolve(),
              "New guest draw enables full-surface EDRAM resolve again");
        std::cout << "Original PM4 EDRAM full color resolve plan and tiled data PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
