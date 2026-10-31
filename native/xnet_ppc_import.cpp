#include "ppc_recomp_shared.h"
#include "xnet_startup.hpp"
#include <cstdint>

// Xbox 360 NetDll_XNetStartup(caller, XNetStartupParams*): r3, r4 -> status r3.
PPC_FUNC(__imp__NetDll_XNetStartup) {
    (void)base;
    ctx.r3.u64 = dotm::xnet_bound_startup(ctx.r3.u32, ctx.r4.u32);
}

// Xbox 360 NetDll_XNetCleanup(caller): r3 -> status r3.
PPC_FUNC(__imp__NetDll_XNetCleanup) {
    (void)base;
    ctx.r3.u64 = dotm::xnet_bound_cleanup(ctx.r3.u32);
}
