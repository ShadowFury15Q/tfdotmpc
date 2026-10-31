#include "ppc_recomp_shared.h"
#include "kernel_title_termination.hpp"

PPC_FUNC(__imp__ExRegisterTitleTerminateNotification) {
    (void)base;
    dotm::ex_bound_register_title_termination(ctx.r3.u32, ctx.r4.u32);
}

// Original Xbox 360 XAM termination import, reached from DOTM _xstart.
PPC_FUNC(__imp__XamLoaderTerminateTitle) {
    (void)base;
    dotm::xam_bound_loader_terminate_title(ctx);
}

// Xbox 360 kernel HAL firmware transition. Keep the host OS running while
// dispatching original title shutdown callbacks and unwinding the guest.
PPC_FUNC(__imp__HalReturnToFirmware) {
    (void)base;
    dotm::hal_bound_return_to_firmware(ctx, ctx.r3.u32);
}
