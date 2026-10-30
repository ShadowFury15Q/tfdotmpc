#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>

namespace dotm::ppc {
inline void store_be64(std::uint8_t* base, std::uint32_t address, std::uint64_t value) {
    for (unsigned i = 0; i < 8; ++i)
        base[address + i] = static_cast<std::uint8_t>(value >> (56 - 8 * i));
}

inline std::uint64_t load_be64(const std::uint8_t* base, std::uint32_t address) {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i)
        value = (value << 8) | base[address + i];
    return value;
}

// Xbox 360 XenonRecomp savegprlr_N: r14 starts at SP-0x98,
// successive GPRs occupy eight-byte slots, and the original LR is at SP-8.
// The save helper receives the original LR in r12 (not ctx.lr, which holds
// the call's return address). Restore reloads the original LR.
template<class Context>
void save_gprlr(Context& ctx, std::uint8_t* base, unsigned first) {
    if (first < 14 || first > 31)
        throw std::invalid_argument("Invalid PPC GPR save start register");
    const auto regs = std::array{&ctx.r14, &ctx.r15, &ctx.r16, &ctx.r17, &ctx.r18, &ctx.r19, &ctx.r20, &ctx.r21, &ctx.r22, &ctx.r23, &ctx.r24, &ctx.r25, &ctx.r26, &ctx.r27, &ctx.r28, &ctx.r29, &ctx.r30, &ctx.r31};
    const auto sp = ctx.r1.u32;
    for (unsigned reg = first; reg <= 31; ++reg) {
        const auto offset = static_cast<std::uint32_t>(-0x98 + 8 * static_cast<int>(reg - 14));
        store_be64(base, sp + offset, regs[reg - 14]->u64);
    }
    store_be64(base, sp - 8U, ctx.r12.u64);
}

template<class Context>
void restore_gprlr(Context& ctx, const std::uint8_t* base, unsigned first) {
    if (first < 14 || first > 31)
        throw std::invalid_argument("Invalid PPC GPR restore start register");
    const auto regs = std::array{&ctx.r14, &ctx.r15, &ctx.r16, &ctx.r17, &ctx.r18, &ctx.r19, &ctx.r20, &ctx.r21, &ctx.r22, &ctx.r23, &ctx.r24, &ctx.r25, &ctx.r26, &ctx.r27, &ctx.r28, &ctx.r29, &ctx.r30, &ctx.r31};
    const auto sp = ctx.r1.u32;
    for (unsigned reg = first; reg <= 31; ++reg) {
        const auto offset = static_cast<std::uint32_t>(-0x98 + 8 * static_cast<int>(reg - 14));
        regs[reg - 14]->u64 = load_be64(base, sp + offset);
    }
    ctx.lr = load_be64(base, sp - 8U);
}
} // namespace dotm::ppc
