#pragma once
#include "ppc_nonvolatile_gpr.hpp"
#include <array>
#include <cstdint>
#include <stdexcept>

namespace dotm::ppc {
// Xbox 360 leaf FPR helpers preserve the IEEE-754 bits of f14..f31 in
// big-endian 64-bit slots at r12-0x90, r12-0x88, ... r12-0x08.
// Unlike GPR/LR helpers the pointer is r12, and LR is not modified.
template<class Context>
void save_fpr(Context& ctx, std::uint8_t* base, unsigned first) {
    if (first < 14 || first > 31)
        throw std::invalid_argument("Invalid PPC FPR save start register");
    const auto regs = std::array{&ctx.f14, &ctx.f15, &ctx.f16, &ctx.f17,
                                 &ctx.f18, &ctx.f19, &ctx.f20, &ctx.f21,
                                 &ctx.f22, &ctx.f23, &ctx.f24, &ctx.f25,
                                 &ctx.f26, &ctx.f27, &ctx.f28, &ctx.f29,
                                 &ctx.f30, &ctx.f31};
    const auto frame = ctx.r12.u32;
    for (unsigned reg = first; reg <= 31; ++reg)
        store_be64(base, frame - 0x90U + 8U * (reg - 14U), regs[reg - 14U]->u64);
}

template<class Context>
void restore_fpr(Context& ctx, const std::uint8_t* base, unsigned first) {
    if (first < 14 || first > 31)
        throw std::invalid_argument("Invalid PPC FPR restore start register");
    const auto regs = std::array{&ctx.f14, &ctx.f15, &ctx.f16, &ctx.f17,
                                 &ctx.f18, &ctx.f19, &ctx.f20, &ctx.f21,
                                 &ctx.f22, &ctx.f23, &ctx.f24, &ctx.f25,
                                 &ctx.f26, &ctx.f27, &ctx.f28, &ctx.f29,
                                 &ctx.f30, &ctx.f31};
    const auto frame = ctx.r12.u32;
    for (unsigned reg = first; reg <= 31; ++reg)
        regs[reg - 14U]->u64 = load_be64(base, frame - 0x90U + 8U * (reg - 14U));
}
} // namespace dotm::ppc
