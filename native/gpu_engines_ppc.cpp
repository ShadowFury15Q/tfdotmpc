#include "ppc_recomp_shared.h"
#include "gpu_engines.hpp"

PPC_FUNC(__imp__VdInitializeEngines) {
    (void)base;
    ctx.r3.u64 = dotm::vd_bound_initialize_engines(
        ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32);
}
PPC_FUNC(__imp__VdShutdownEngines) {
    (void)base;
    dotm::vd_bound_shutdown_engines();
}
PPC_FUNC(__imp__VdSetGraphicsInterruptCallback) {
    (void)base;
    dotm::vd_bound_set_graphics_interrupt_callback(ctx.r3.u32, ctx.r4.u32);
}
PPC_FUNC(__imp__VdInitializeRingBuffer) {
    (void)base;
    dotm::vd_bound_initialize_ring_buffer(ctx.r3.u32, ctx.r4.u32);
}
PPC_FUNC(__imp__VdEnableRingBufferRPtrWriteBack) {
    (void)base;
    dotm::vd_bound_enable_ring_buffer_read_pointer_writeback(
        ctx.r3.u32, ctx.r4.u32);
}
PPC_FUNC(__imp__VdSetSystemCommandBufferGpuIdentifierAddress) {
    (void)base;
    dotm::vd_bound_set_system_command_buffer_gpu_identifier_address(ctx.r3.u32);
}

// The private XenonRecomp import name is wrong: xboxkrnl ordinal 0x1DF
// is VdVerifyMEInitCommand, not KiApcNormalRoutineNop (ordinal 0x19D).
// Keep the generated symbol for ABI compatibility without changing or
// publishing any original recompiled game code.
PPC_FUNC(__imp__KiApcNormalRoutineNop) {
    (void)base;
    dotm::vd_bound_verify_me_init_command(ctx.r3.u32, ctx.r4.u32);
}
