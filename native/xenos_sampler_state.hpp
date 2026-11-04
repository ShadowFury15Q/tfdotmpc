#pragma once
#include "xenos_texture_fetch.hpp"
#include <cstdint>
#include <stdexcept>

namespace dotm {

// Maps only native-compatible Xenos sampler modes. Modes that need
// per-TFETCH shader instruction overrides, mirror-clamp or anisotropic
// reconstruction must not be silently replaced by D3D11 defaults.
enum class XenosHostAddress { wrap, mirror, clamp };
struct XenosHostSampler {
    XenosHostAddress u{}, v{}, w{};
    bool min_linear = false, mag_linear = false;
};
inline XenosHostSampler decode_xenos_host_sampler(
    const XenosTextureFetchConstant& t) {
    if (t.dimension != 1U)
        throw std::runtime_error("Native DX11 sampler expects original 2D Xenos fetch");
    const auto mode = [](std::uint32_t raw) {
        switch (raw) {
        case 0U: return XenosHostAddress::wrap;
        case 1U: return XenosHostAddress::mirror;
        case 2U: return XenosHostAddress::clamp;
        default:
            throw std::runtime_error(
                "Original Xenos sampler uses unsupported clamp/border mode");
        }
    };
    XenosHostSampler result;
    result.u = mode((t.raw_words[0] >> 10U) & 7U);
    result.v = mode((t.raw_words[0] >> 13U) & 7U);
    result.w = mode((t.raw_words[0] >> 16U) & 7U);
    const auto mag = (t.raw_words[3] >> 19U) & 3U;
    const auto min = (t.raw_words[3] >> 21U) & 3U;
    const auto mip = (t.raw_words[3] >> 23U) & 3U;
    const auto anisotropy = (t.raw_words[3] >> 25U) & 7U;
    // 3 = use filtering from the TFETCH instruction, which is not part of
    // the six-DWORD sampler descriptor and cannot be guessed here.
    if (mag > 1U || min > 1U || mip == 3U ||
        anisotropy > 1U || t.mip_min_level ||
        t.mip_max_level)
        throw std::runtime_error(
            "Original Xenos sampler requires TFETCH or mip/aniso processing");
    result.mag_linear = mag == 1U;
    result.min_linear = min == 1U;
    return result;
}
}  // namespace dotm
