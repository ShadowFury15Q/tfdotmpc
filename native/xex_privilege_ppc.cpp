#include "ppc_recomp_shared.h"
#include "xex_privilege.hpp"
PPC_FUNC(__imp__XexCheckExecutablePrivilege) {
    (void)base;
    ctx.r3.u64 = dotm::bound_xex_check_executable_privilege(ctx.r3.u32);
}

// The native display presents an Xbox 360 HDMI AV pack (value 8).
PPC_FUNC(__imp__XGetAVPack) {
    ctx.r3.u64 = 8U;
}
