#pragma once
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace dotm {

// Original Xenos RB_DEPTHCONTROL, RB_DEPTH_INFO and two stencil reference
// register snapshots. Numeric comparisons and stencil ops follow the R400
// GPU register encodings, not hard-coded host pipeline defaults.
struct XenosStencilFace {
    std::uint32_t compare = 7, fail = 0, depth_fail = 0, pass = 0;
};
struct XenosDepthState {
    std::uint32_t edram_base_tiles = 0;
    bool depth_enable = false, depth_write = false, stencil_enable = false;
    std::uint32_t depth_compare = 7;
    std::uint8_t stencil_ref = 0, stencil_read_mask = 0xFF,
                 stencil_write_mask = 0xFF;
    XenosStencilFace front, back;
};
inline XenosDepthState decode_xenos_depth_state(
    const std::vector<std::uint32_t>& registers) {
    constexpr std::uint32_t kDepthInfo = 0x2002U;
    constexpr std::uint32_t kStencilBack = 0x210CU;
    constexpr std::uint32_t kStencilFront = 0x210DU;
    constexpr std::uint32_t kDepthControl = 0x2200U;
    if (registers.size() <= kDepthControl)
        throw std::runtime_error("Incomplete Xenos original depth PM4 register shadow");
    const auto control = registers[kDepthControl];
    const auto depth_info = registers[kDepthInfo];
    XenosDepthState result{};
    result.edram_base_tiles = depth_info & 0xFFFU;
    result.depth_enable = ((control >> 1U) & 1U) != 0U;
    result.depth_write = ((control >> 2U) & 1U) != 0U;
    result.stencil_enable = (control & 1U) != 0U;
    result.depth_compare = (control >> 4U) & 7U;
    // 20e4 floating-point depth has no bit-exact DX11 D24S8 equivalent.
    if ((result.depth_enable || result.stencil_enable) &&
        (depth_info & (1U << 16U)))
        throw std::runtime_error("Xenos D24FS8 needs native float-depth EDRAM conversion");
    const auto decode_face = [](std::uint32_t value, unsigned shift) {
        XenosStencilFace face;
        face.compare = (value >> shift) & 7U;
        face.fail = (value >> (shift + 3U)) & 7U;
        face.pass = (value >> (shift + 6U)) & 7U;
        face.depth_fail = (value >> (shift + 9U)) & 7U;
        return face;
    };
    result.front = decode_face(control, 8U);
    result.back = result.front;
    if (result.stencil_enable) {
        const auto mask = registers[kStencilFront];
        result.stencil_ref = static_cast<std::uint8_t>(mask);
        result.stencil_read_mask = static_cast<std::uint8_t>(mask >> 8U);
        result.stencil_write_mask = static_cast<std::uint8_t>(mask >> 16U);
        if (control & (1U << 7U)) {
            const auto backmask = registers[kStencilBack];
            if ((mask & 0xFFFFFFU) != (backmask & 0xFFFFFFU))
                throw std::runtime_error(
                    "Xenos two-sided stencil reference/mask mismatch needs separate passes");
            result.back = decode_face(control, 20U);
        }
    }
    return result;
}
}  // namespace dotm
