#include "ppc_recomp_shared.h"
// Minimal synthetic recompiled game function, never original game code.
PPC_FUNC_IMPL(_xstart) {
    ++ctx.calls;
    if (ctx.r1.u64 != 0) base[ctx.r1.u64 - 16] = 0x42;
}
