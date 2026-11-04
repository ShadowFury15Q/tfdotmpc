#include "xenos_shader_constants.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool value, const char* description) {
    if (!value) throw std::runtime_error(description);
}
template<class F> void rejects(F task, const char* description) {
    bool rejected = false;
    try { task(); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, description);
}
}
int main() {
    try {
        dotm::GpuDrawCommand draw;
        draw.register_words.resize(0x4800, 0);
        for (std::uint32_t i = 0; i < 512U * 4U; ++i)
            draw.register_words[0x4000U + i] = 0x3F000000U + i;
        // VS base 6, window 8 vec4; PS base 201, window 4 vec4.
        draw.register_words[0x2307U] = 6U | (7U << 12U);
        draw.register_words[0x2308U] = 201U | (3U << 12U);
        const auto vs = dotm::xenos_draw_float_constants(
            draw, dotm::XenosShaderStage::vertex, 3U * 16U);
        const auto ps = dotm::xenos_draw_float_constants(
            draw, dotm::XenosShaderStage::pixel, 4U * 16U);
        require(vs.size() == 12U && ps.size() == 16U,
                "Original SQ stage sizes");
        for (std::uint32_t i = 0; i < vs.size(); ++i)
            require(vs[i] == draw.register_words[0x4000U + 6U * 4U + i],
                    "Original VS constant bit patterns and base");
        for (std::uint32_t i = 0; i < ps.size(); ++i)
            require(ps[i] == draw.register_words[0x4000U + 201U * 4U + i],
                    "Original PS constant bit patterns and base");
        rejects([&] { (void)dotm::xenos_draw_float_constants(
                    draw, dotm::XenosShaderStage::pixel, 5U * 16U); },
                "Original SQ PS window overread");
        rejects([&] { (void)dotm::xenos_draw_float_constants(
                    draw, dotm::XenosShaderStage::vertex, 17U); },
                "Constant buffer width must be float4-aligned");
        draw.register_words[0x2307U] = 511U | (3U << 12U);
        rejects([&] { (void)dotm::xenos_draw_float_constants(
                    draw, dotm::XenosShaderStage::vertex, 2U * 16U); },
                "Original SQ constant window cannot wrap physical register bank");
        draw.register_words.resize(0x2300);
        rejects([&] { (void)dotm::xenos_draw_float_constants(
                    draw, dotm::XenosShaderStage::vertex, 16U); },
                "Incomplete PM4 register snapshots cannot bind shader constants");
        std::cout << "Original per-draw Xenos VS/PS float constants PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
