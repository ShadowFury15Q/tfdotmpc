#include "ppc_recomp_shared.h"
// Minimal synthetic recompiled game function, never original game code.
PPC_FUNC_IMPL(_xstart) {
    ++ctx.calls;
    (void)base;
}
