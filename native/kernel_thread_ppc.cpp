#include "ppc_recomp_shared.h"
#include "kernel_thread.hpp"
#include "guest_memory.hpp"

namespace {
std::uint32_t run_guest_thread(dotm::GuestMemory& memory, std::uint32_t pcr,
    std::uint32_t stack_pointer, std::uint32_t api_startup,
    std::uint32_t start_address, std::uint32_t parameter) {
    PPCContext ctx{};
    ctx.r1.u64 = stack_pointer;
    ctx.r13.u64 = pcr;
    ctx.r3.u64 = api_startup ? start_address : parameter;
    ctx.r4.u64 = parameter;
    ctx.lr = 0;
    memory.find_function(api_startup ? api_startup : start_address)(ctx, memory.base());
    return ctx.r3.u32;
}
}

// Original DOTM sub_8351E9D8 passes PHANDLE, stack bytes, thread ID pointer,
// 0x8351F588 (its original CRT thread-start wrapper), routine, argument, flags.
PPC_FUNC(__imp__ExCreateThread) {
    (void)base;
    ctx.r3.u64 = dotm::nt_bound_create_thread(ctx.r3.u32, ctx.r4.u32,
        ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, ctx.r8.u32, ctx.r9.u32,
        run_guest_thread);
}
PPC_FUNC(__imp__NtResumeThread) {
    (void)base;
    ctx.r3.u64 = dotm::nt_bound_resume_thread(ctx.r3.u32, ctx.r4.u32);
}
PPC_FUNC(__imp__KeDelayExecutionThread) {
    (void)base;
    ctx.r3.u64 = dotm::nt_bound_delay_execution_thread(ctx.r3.u32,
        ctx.r4.u32, ctx.r5.u32);
}
PPC_FUNC(__imp__ExTerminateThread) {
    (void)base;
    dotm::nt_bound_terminate_thread(ctx.r3.u32);
}
