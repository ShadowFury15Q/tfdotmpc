#include "xenos_raster_state.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void check(bool valid, const char* message) {
    if (!valid) throw std::runtime_error(message);
}
template <typename F> void rejects(F f) {
    bool rejected = false;
    try { f(); } catch (const std::runtime_error&) { rejected = true; }
    check(rejected, "Invalid Xenos PM4 raster state must fail closed");
}
std::uint32_t pair(std::uint32_t x, std::uint32_t y) {
    return x | (y << 16U);
}
}
int main() {
    try {
        std::vector<std::uint32_t> regs(0x2300);
        regs[0x2208U] = 4U;
        check(dotm::decode_xenos_edram_draw_mode(regs) ==
                  dotm::XenosEdramDrawMode::color_depth,
              "Guest PM4 color-depth mode");
        regs[0x2208U] = 0U;
        check(dotm::decode_xenos_edram_draw_mode(regs) ==
                  dotm::XenosEdramDrawMode::no_operation,
              "No-operation PM4 cannot invent a color draw");
        regs[0x2208U] = 5U;
        check(dotm::decode_xenos_edram_draw_mode(regs) ==
                  dotm::XenosEdramDrawMode::depth_only,
              "Depth-only mode cannot be treated as a color draw");
        regs[0x2208U] = 6U;
        check(dotm::decode_xenos_edram_draw_mode(regs) ==
                  dotm::XenosEdramDrawMode::copy,
              "Actual RB_COPY must resolve, not submit a raster draw");
        regs[0x2208U] = 7U;
        rejects([&] { (void)dotm::decode_xenos_edram_draw_mode(regs); });
        // Real D3D9 guest sets screen scissor to the full EDRAM extent.
        regs[0x200EU] = pair(0, 0);
        regs[0x200FU] = pair(8191, 8191);
        regs[0x2081U] = pair(0, 0) | (1U << 31U);
        regs[0x2082U] = pair(1280, 720);
        auto rect = dotm::decode_xenos_scissor(regs, 1280, 720);
        check(!rect.empty() && rect.left == 0 && rect.top == 0 &&
              rect.right == 1280 && rect.bottom == 720,
              "Original full-surface PM4 screen/window scissor");
        regs[0x2081U] = pair(10, 20);
        regs[0x2082U] = pair(100, 150);
        regs[0x2080U] = pair(5, 7);
        rect = dotm::decode_xenos_scissor(regs, 1280, 720);
        check(rect.left == 15 && rect.top == 27 &&
              rect.right == 105 && rect.bottom == 157,
              "Guest window offset applied to unsigned scissor endpoints");
        regs[0x2080U] = pair(0x7FF8U, 0x7FFBU); // Signed -8, -5.
        rect = dotm::decode_xenos_scissor(regs, 1280, 720);
        check(rect.left == 2 && rect.top == 15 &&
              rect.right == 92 && rect.bottom == 145,
              "Signed original window offset does not wrap to 32-bit unsigned");
        regs[0x200EU] = pair(16, 40);
        regs[0x200FU] = pair(80, 110);
        rect = dotm::decode_xenos_scissor(regs, 1280, 720);
        check(rect.left == 16 && rect.top == 40 &&
              rect.right == 80 && rect.bottom == 110,
              "Screen scissor intersects offset guest window scissor");
        regs[0x200EU] = pair(500, 600);
        rect = dotm::decode_xenos_scissor(regs, 1280, 720);
        check(rect.empty() && rect.right == rect.left &&
              rect.bottom == rect.top,
              "Nonoverlapping guest scissors clip every color sample");
        regs[0x200EU] = pair(0, 0);
        regs[0x200FU] = pair(8191, 8191);
        regs[0x2081U] = pair(0, 0) | (1U << 31U);
        regs[0x2082U] = pair(8191, 8191);
        rect = dotm::decode_xenos_scissor(regs, 640, 480);
        check(rect.left == 0 && rect.top == 0 &&
              rect.right == 640 && rect.bottom == 480,
              "Scissor cannot escape the actual DX11 EDRAM attachment");
        rejects([&] { (void)dotm::decode_xenos_scissor(regs, 0, 480); });
        regs.resize(0x2082U);
        rejects([&] { (void)dotm::decode_xenos_scissor(regs, 640, 480); });
        std::cout << "Original Xenos PM4 EDRAM mode and screen/window scissor PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
