#include "ppc_recomp_shared.h"
#include "xam_launch_data.hpp"
PPC_FUNC(__imp__XamLoaderSetLaunchData) {
    (void)base;
    ctx.r3.u64 = dotm::xam_bound_launch_set(ctx.r3.u32, ctx.r4.u32);
}
PPC_FUNC(__imp__XamLoaderGetLaunchDataSize) {
    (void)base;
    ctx.r3.u64 = dotm::xam_bound_launch_size(ctx.r3.u32);
}
PPC_FUNC(__imp__XamLoaderGetLaunchData) {
    (void)base;
    ctx.r3.u64 = dotm::xam_bound_launch_get(ctx.r3.u32, ctx.r4.u32);
}
