#pragma once
#include "gpu_engines.hpp"
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace dotm {

// SQ_VS_CONST and SQ_PS_CONST identify the actual float4 window for each
// active shader. PM4 can change both windows between draw calls; do not
// assume static 0/256 partitions or reuse a previous draw's constant bytes.
enum class XenosShaderStage { vertex, pixel };
inline std::vector<std::uint32_t> xenos_draw_float_constants(
    const GpuDrawCommand& draw, XenosShaderStage stage,
    std::uint32_t byte_count) {
    constexpr std::uint32_t kRegisterVsConst = 0x2307U;
    constexpr std::uint32_t kRegisterPsConst = 0x2308U;
    constexpr std::uint32_t kFloatBase = 0x4000U;
    constexpr std::uint32_t kFloatCount = 512U;
    const auto config_register = stage == XenosShaderStage::vertex
        ? kRegisterVsConst : kRegisterPsConst;
    if (byte_count % 16U || byte_count > 256U * 16U)
        throw std::runtime_error("Invalid Xenos stage float constant buffer size");
    if (draw.register_words.size() < kFloatBase + kFloatCount * 4U ||
        config_register >= draw.register_words.size())
        throw std::runtime_error("Incomplete original PM4 shader constant register shadow");
    if (!byte_count) return {};
    const auto config = draw.register_words[config_register];
    const auto start = config & 0x1FFU;
    const auto window = ((config >> 12U) & 0x1FFU) + 1U;
    const auto count = byte_count / 16U;
    if (count > window || start + count > kFloatCount)
        throw std::runtime_error("Original shader constant access exceeds SQ stage window");
    std::vector<std::uint32_t> constants(count * 4U);
    for (std::uint32_t i = 0; i < count * 4U; ++i)
        constants[i] = draw.register_words[kFloatBase + start * 4U + i];
    return constants;
}
}  // namespace dotm
