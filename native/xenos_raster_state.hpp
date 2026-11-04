#pragma once
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace dotm {

enum class XenosEdramDrawMode {
    no_operation, color_depth, depth_only, copy
};

inline XenosEdramDrawMode decode_xenos_edram_draw_mode(
        const std::vector<std::uint32_t>& regs) {
    if (regs.size() <= 0x2208U)
        throw std::runtime_error("Incomplete original Xenos RB_MODECONTROL");
    switch (regs[0x2208U] & 7U) {
        case 0U: return XenosEdramDrawMode::no_operation;
        case 4U: return XenosEdramDrawMode::color_depth;
        case 5U: return XenosEdramDrawMode::depth_only;
        case 6U: return XenosEdramDrawMode::copy;
    }
    throw std::runtime_error("Unsupported original Xenos RB_MODECONTROL EDRAM mode");
}

struct XenosScissor {
    std::int32_t left = 0, top = 0, right = 0, bottom = 0;
    bool empty() const noexcept { return right <= left || bottom <= top; }
};

// Signed 15-bit screen scissor and window offset coordinates are independent
// of the unsigned 14-bit window rectangle endpoints.
inline std::int32_t xenos_signed_15(std::uint32_t raw) noexcept {
    const std::uint32_t value = raw & 0x7FFFU;
    return (value & 0x4000U) != 0U
        ? static_cast<std::int32_t>(value) - 0x8000
        : static_cast<std::int32_t>(value);
}

inline XenosScissor decode_xenos_scissor(
    const std::vector<std::uint32_t>& regs,
    std::uint32_t width, std::uint32_t height) {
    constexpr std::uint32_t kScreenTL = 0x200EU;
    constexpr std::uint32_t kScreenBR = 0x200FU;
    constexpr std::uint32_t kOffset = 0x2080U;
    constexpr std::uint32_t kWindowTL = 0x2081U;
    constexpr std::uint32_t kWindowBR = 0x2082U;
    if (regs.size() <= kWindowBR)
        throw std::runtime_error("Incomplete original Xenos scissor registers");
    if (!width || !height || width > 8192U || height > 8192U)
        throw std::runtime_error("Invalid Xenos raster attachment for scissor");
    const auto tl = regs[kWindowTL];
    const auto br = regs[kWindowBR];
    std::int32_t left = static_cast<std::int32_t>(tl & 0x3FFFU);
    std::int32_t top = static_cast<std::int32_t>((tl >> 16U) & 0x3FFFU);
    std::int32_t right = static_cast<std::int32_t>(br & 0x3FFFU);
    std::int32_t bottom = static_cast<std::int32_t>((br >> 16U) & 0x3FFFU);
    if (!(tl & (1U << 31U))) {
        const auto offset = regs[kOffset];
        const auto offset_x = xenos_signed_15(offset);
        const auto offset_y = xenos_signed_15(offset >> 16U);
        left += offset_x;
        right += offset_x;
        top += offset_y;
        bottom += offset_y;
    }
    const auto screen_tl = regs[kScreenTL];
    const auto screen_br = regs[kScreenBR];
    left = std::max(left, xenos_signed_15(screen_tl));
    top = std::max(top, xenos_signed_15(screen_tl >> 16U));
    right = std::min(right, xenos_signed_15(screen_br));
    bottom = std::min(bottom, xenos_signed_15(screen_br >> 16U));
    const auto limit_x = static_cast<std::int32_t>(width);
    const auto limit_y = static_cast<std::int32_t>(height);
    left = std::clamp(left, 0, limit_x);
    top = std::clamp(top, 0, limit_y);
    right = std::clamp(right, left, limit_x);
    bottom = std::clamp(bottom, top, limit_y);
    return {left, top, right, bottom};
}

} // namespace dotm
