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