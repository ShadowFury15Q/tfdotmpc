#include "xenos_depth_state.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void check(bool valid, const char* message) {
    if (!valid) throw std::runtime_error(message);
}
template<class F> void rejects(F work) {
    bool caught = false;
    try { work(); } catch (const std::runtime_error&) { caught = true; }
    check(caught, "Unsupported original Xenos depth state must fail closed");
}
}
int main() {
    try {
        std::vector<std::uint32_t> regs(0x2300);
        regs[0x2002U] = 41U; // Original guest EDRAM depth base, in tiles.
        regs[0x2200U] = (1U << 1U) | (1U << 2U) | (3U << 4U);
        auto state = dotm::decode_xenos_depth_state(regs);
        check(state.edram_base_tiles == 41U && state.depth_enable &&
                state.depth_write && state.depth_compare == 3U &&
                !state.stencil_enable, "Original depth enable/write/LEQUAL");
        regs[0x2200U] = 1U | (1U << 1U) | (1U << 7U) |
            (2U << 8U) | (3U << 11U) | (4U << 14U) | (5U << 17U) |
            (6U << 20U) | (1U << 23U) | (2U << 26U) | (7U << 29U);
        regs[0x210DU] = 0x007F3B09U;
        regs[0x210CU] = 0x007F3B09U;
        state = dotm::decode_xenos_depth_state(regs);
        check(state.stencil_enable && state.front.compare == 2U &&
                state.front.fail == 3U && state.front.pass == 4U &&
                state.front.depth_fail == 5U && state.back.compare == 6U &&
                state.back.fail == 1U && state.back.pass == 2U &&
                state.back.depth_fail == 7U &&
                state.stencil_ref == 9U && state.stencil_read_mask == 0x3BU &&
                state.stencil_write_mask == 0x7FU,
                "Original front/back stencil compare and ops with shared reference");
        regs[0x210CU] = 0x007F3B08U;
        rejects([&] { (void)dotm::decode_xenos_depth_state(regs); });
        regs[0x210CU] = regs[0x210DU];
        regs[0x2002U] |= (1U << 16U);
        rejects([&] { (void)dotm::decode_xenos_depth_state(regs); });
        regs.resize(0x21FFU);
        rejects([&] { (void)dotm::decode_xenos_depth_state(regs); });
        std::cout << "Original Xenos depth/stencil PM4 register decode PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
