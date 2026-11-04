#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace dotm {

// Exact R400/Xenos RT0 RB_COLOR_MASK and RB_BLENDCONTROL0 register fields.
// RGB and alpha factors are independent and change on every PM4 draw.
struct XenosBlendState {
    std::uint32_t raw_control = 0;
    std::uint8_t write_mask = 0;
    std::uint32_t rgb_source = 0, rgb_destination = 0, rgb_operation = 0;
    std::uint32_t alpha_source = 0, alpha_destination = 0, alpha_operation = 0;
    bool enabled = false;
    std::array<float, 4> constant_factor{1.f, 1.f, 1.f, 1.f};
};

inline XenosBlendState decode_xenos_blend_state(
        const std::vector<std::uint32_t>& registers) {
    constexpr std::uint32_t kMask = 0x2104U;
    constexpr std::uint32_t kBlendRed = 0x2105U;
    constexpr std::uint32_t kBlendControl0 = 0x2201U;
    if (registers.size() <= kBlendControl0)
        throw std::runtime_error("Incomplete original Xenos RB blend registers");

    XenosBlendState state;
    state.write_mask = static_cast<std::uint8_t>(registers[kMask] & 15U);
    state.raw_control = registers[kBlendControl0];
    const auto raw = state.raw_control;
    state.rgb_source = raw & 31U;
    state.rgb_operation = (raw >> 5U) & 7U;
    state.rgb_destination = (raw >> 8U) & 31U;
    state.alpha_source = (raw >> 16U) & 31U;
    state.alpha_operation = (raw >> 21U) & 7U;
    state.alpha_destination = (raw >> 24U) & 31U;
    const auto supported = [](std::uint32_t value) {
        return value == 0U || value == 1U ||
            (value >= 4U && value <= 16U);
    };
    for (auto factor : {state.rgb_source, state.rgb_destination,
                        state.alpha_source, state.alpha_destination})
        if (!supported(factor))
            throw std::runtime_error("Unsupported original Xenos RB blend factor");
    if (state.rgb_destination == 16U ||
        state.alpha_source == 16U || state.alpha_destination == 16U)
        throw std::runtime_error("Original Xenos alpha-saturate factor has no safe DX11 target");
    if (state.rgb_operation > 4U || state.alpha_operation > 4U)
        throw std::runtime_error("Unsupported original Xenos RB blend operation");
    // A source=ONE, dest=ZERO, ADD pair is byte-exact without blending.
    state.enabled = !(state.rgb_source == 1U && state.rgb_destination == 0U &&
                      state.rgb_operation == 0U &&
                      state.alpha_source == 1U && state.alpha_destination == 0U &&
                      state.alpha_operation == 0U);
    const auto uses_rgb_alpha = [](std::uint32_t f) {
        return f == 14U || f == 15U;
    };
    const auto uses_rgb_color = [](std::uint32_t f) {
        return f == 12U || f == 13U;
    };
    const bool rgb_constant_alpha =
        uses_rgb_alpha(state.rgb_source) || uses_rgb_alpha(state.rgb_destination);
    if (rgb_constant_alpha &&
        (uses_rgb_color(state.rgb_source) || uses_rgb_color(state.rgb_destination)))
        throw std::runtime_error(
            "Xenos simultaneously uses constant RGB and constant alpha in RGB blend");
    const bool constants_used =
        ((state.rgb_source >= 12U && state.rgb_source <= 15U) ||
         (state.rgb_destination >= 12U && state.rgb_destination <= 15U) ||
         (state.alpha_source >= 12U && state.alpha_source <= 15U) ||
         (state.alpha_destination >= 12U && state.alpha_destination <= 15U));
    if (constants_used) {
        for (unsigned i = 0U; i < 4U; ++i) {
            const float value = std::bit_cast<float>(registers[kBlendRed + i]);
            if (!std::isfinite(value))
                throw std::runtime_error("Non-finite original Xenos RB blend constant");
            state.constant_factor[i] = std::clamp(value, 0.0f, 1.0f);
        }
        if (rgb_constant_alpha) {
            for (unsigned i = 0U; i < 3U; ++i)
                state.constant_factor[i] = state.constant_factor[3];
        }
    }
    return state;
}

} // namespace dotm
