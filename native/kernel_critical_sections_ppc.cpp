#include "ppc_recomp_shared.h"
#include "kernel_critical_sections.hpp"
PPC_FUNC(__imp__RtlInitializeCriticalSection) {
    (void)base;
    dotm::kernel_initialize_critical_section(ctx.r3.u32, 0);
}
PPC_FUNC(__imp__RtlInitializeCriticalSectionAndSpinCount) {
    (void)base;
    dotm::kernel_initialize_critical_section(ctx.r3.u32, ctx.r4.u32);
    ctx.r3.u64 = 0;
}
PPC_FUNC(__imp__RtlEnterCriticalSection) {
    (void)base;
    dotm::kernel_enter_critical_section(ctx.r3.u32);
}
PPC_FUNC(__imp__RtlTryEnterCriticalSection) {
    (void)base;
    ctx.r3.u64 = dotm::kernel_try_enter_critical_section(ctx.r3.u32) ? 1 : 0;
}
PPC_FUNC(__imp__RtlLeaveCriticalSection) {
    (void)base;
    dotm::kernel_leave_critical_section(ctx.r3.u32);
}
