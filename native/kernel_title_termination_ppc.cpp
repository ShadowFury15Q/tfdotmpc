#include "ppc_recomp_shared.h"
#include "kernel_title_termination.hpp"

PPC_FUNC(__imp__ExRegisterTitleTerminateNotification) {
    (void)base;
    dotm::ex_bound_register_title_termination(ctx.r3.u32, ctx.r4.u32);
}
