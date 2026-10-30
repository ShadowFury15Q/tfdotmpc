#include "ppc_nonvolatile_gpr.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <stdexcept>

struct Register {
    union {
        std::uint64_t u64;
        std::uint32_t u32;
    };
    Register() : u64(0) {}
};
struct Context {
    Register r1, r12;
    Register r14, r15, r16, r17, r18, r19, r20, r21, r22, r23, r24, r25, r26, r27, r28, r29, r30, r31;
    std::uint64_t lr = 0;
};

int main() {
    std::array<std::uint8_t, 2048> memory{};
    Context ctx{};
    ctx.r1.u32 = 0x400;
    ctx.r12.u64 = 0x1122334455667788ULL;
    const auto regs = std::array{&ctx.r14, &ctx.r15, &ctx.r16, &ctx.r17, &ctx.r18, &ctx.r19, &ctx.r20, &ctx.r21, &ctx.r22, &ctx.r23, &ctx.r24, &ctx.r25, &ctx.r26, &ctx.r27, &ctx.r28, &ctx.r29, &ctx.r30, &ctx.r31};
    for (unsigned i = 0; i < regs.size(); ++i)
        regs[i]->u64 = 0xABCDEF0000000000ULL + i;

    // First exercise the exact helper invoked by DOTM's original _xstart.
    dotm::ppc::save_gprlr(ctx, memory.data(), 28);
    assert(dotm::ppc::load_be64(memory.data(), 0x400 - 0x98 + 14 * 8) ==
           0xABCDEF000000000EULL);
    assert(memory[0x400 - 8] == 0x11 && memory[0x400 - 1] == 0x88);
    for (auto* reg : regs) reg->u64 = 0;
    ctx.lr = 0;
    dotm::ppc::restore_gprlr(ctx, memory.data(), 28);
    for (unsigned i = 0; i < regs.size(); ++i)
        assert(regs[i]->u64 == (i < 14 ? 0ULL : 0xABCDEF0000000000ULL + i));
    assert(ctx.lr == 0x1122334455667788ULL);

    // Full nonvolatile range, including r14 at SP-0x98.
    for (unsigned i = 0; i < regs.size(); ++i)
        regs[i]->u64 = 0x1234000000000000ULL + i;
    dotm::ppc::save_gprlr(ctx, memory.data(), 14);
    assert(memory[0x400 - 0x98] == 0x12);
    for (auto* reg : regs) reg->u64 = 0;
    dotm::ppc::restore_gprlr(ctx, memory.data(), 14);
    for (unsigned i = 0; i < regs.size(); ++i)
        assert(regs[i]->u64 == 0x1234000000000000ULL + i);

    bool rejected = false;
    try { dotm::ppc::save_gprlr(ctx, memory.data(), 13); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
    rejected = false;
    try { dotm::ppc::restore_gprlr(ctx, memory.data(), 32); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
}
