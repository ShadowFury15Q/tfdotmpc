#include "ppc_recomp_shared.h"
#include "kernel_file_cache.hpp"
// Verified from original DOTM sub_8351DC60: r3=0 (cache selector),
// r4=ceil(requested_bytes/4096); the return is tested as signed NTSTATUS.
PPC_FUNC(__imp__FscSetCacheElementCount) {
    (void)base;
    ctx.r3.u64 = dotm::fsc_bound_set_cache_element_count(ctx.r3.u32, ctx.r4.u32);
}
PPC_FUNC(__imp__FscGetCacheElementCount) {
    (void)base;
    ctx.r3.u64 = dotm::fsc_bound_get_cache_element_count(ctx.r3.u32);
}
