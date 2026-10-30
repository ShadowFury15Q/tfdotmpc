#include "ppc_recomp_shared.h"
#include "kernel_mutant.hpp"
// Verified from original DOTM PPC callsites: r3=PHANDLE, r4=OBJECT_ATTRIBUTES*,
// r5=InitialOwner. The return value is a 32-bit NTSTATUS in r3.
PPC_FUNC(__imp__NtCreateMutant) {
    (void)base;
    ctx.r3.u64 = dotm::nt_bound_create_mutant(ctx.r3.u32, ctx.r4.u32, ctx.r5.u32);
}
PPC_FUNC(__imp__NtReleaseMutant) {
    (void)base;
    ctx.r3.u64 = dotm::nt_bound_release_mutant(ctx.r3.u32, ctx.r4.u32);
}
// Original NtWaitForSingleObjectEx takes Handle, WaitMode, Alertable, Timeout.
PPC_FUNC(__imp__NtWaitForSingleObjectEx) {
    (void)base;
    ctx.r3.u64 = dotm::nt_bound_wait_for_single_object_ex(
        ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32);
}
PPC_FUNC(__imp__NtClose) {
    (void)base;
    ctx.r3.u64 = dotm::nt_bound_close_mutant(ctx.r3.u32);
}
