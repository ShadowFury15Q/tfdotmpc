#include "xenos_blend_state.hpp"
#include <bit>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void check(bool value, const char* error) {
    if (!value) throw std::runtime_error(error);
}
template <typename F> void rejects(F fn) {
    bool rejected = false;
    try { fn(); } catch (const std::runtime_error&) { rejected = true; }
    check(rejected, "Unsupported original GPU blending must fail closed");
}
std::uint32_t blend(std::uint32_t src, std::uint32_t dst, std::uint32_t op,
                    std::uint32_t asrc, std::uint32_t adst, std::uint32_t aop) {
    return src | (op << 5U) | (dst << 8U) |
           (asrc << 16U) | (aop << 21U) | (adst << 24U);
}
}

int main() {
    try {
        std::vector<std::uint32_t> regs(0x2300);
        regs[0x2104U] = 0x0000000FU;
        regs[0x2201U] = blend(1, 0, 0, 1, 0, 0);
        auto state = dotm::decode_xenos_blend_state(regs);
        check(!state.enabled && state.write_mask == 15U,
              "Opaque original PM4 color is not blended");
        regs[0x2104U] = 5U;
        regs[0x2201U] = blend(6, 7, 0, 1, 7, 0);
        state = dotm::decode_xenos_blend_state(regs);
        check(state.enabled && state.write_mask == 5U &&
              state.rgb_source == 6U && state.rgb_destination == 7U &&
              state.alpha_source == 1U && state.alpha_destination == 7U,
              "Original alpha-composited color and channel mask");
        regs[0x2201U] = blend(4, 5, 4, 1, 0, 2);
        state = dotm::decode_xenos_blend_state(regs);
        check(state.rgb_operation == 4U && state.alpha_operation == 2U,
              "Separate original color and alpha blend operations");
        regs[0x2201U] = blend(14, 15, 0, 12, 13, 0);
        regs[0x2105U] = std::bit_cast<std::uint32_t>(0.2f);
        regs[0x2106U] = std::bit_cast<std::uint32_t>(0.3f);
        regs[0x2107U] = std::bit_cast<std::uint32_t>(0.4f);
        regs[0x2108U] = std::bit_cast<std::uint32_t>(0.6f);
        state = dotm::decode_xenos_blend_state(regs);
        check(state.constant_factor[0] == 0.6f &&
              state.constant_factor[1] == 0.6f &&
              state.constant_factor[2] == 0.6f &&
              state.constant_factor[3] == 0.6f,
              "Constant alpha RGB factor uses original Xenos alpha");
        regs[0x2201U] = blend(12, 14, 0, 1, 0, 0);
        rejects([&] { (void)dotm::decode_xenos_blend_state(regs); });
        regs[0x2201U] = blend(6, 2, 0, 1, 0, 0);
        rejects([&] { (void)dotm::decode_xenos_blend_state(regs); });
        regs[0x2201U] = blend(6, 7, 5, 1, 0, 0);
        rejects([&] { (void)dotm::decode_xenos_blend_state(regs); });
        regs[0x2201U] = blend(6, 16, 0, 1, 0, 0);
        rejects([&] { (void)dotm::decode_xenos_blend_state(regs); });
        regs[0x2201U] = blend(12, 13, 0, 1, 0, 0);
        regs[0x2105U] = 0x7FC00000U;
        rejects([&] { (void)dotm::decode_xenos_blend_state(regs); });
        regs.resize(0x2104U);
        rejects([&] { (void)dotm::decode_xenos_blend_state(regs); });
        std::cout << "Original live Xenos RGB/alpha blending and RT0 color mask PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
