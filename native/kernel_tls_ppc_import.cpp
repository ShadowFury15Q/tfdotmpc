#include "ppc_recomp_shared.h"
#include "kernel_tls.hpp"
PPC_FUNC(__imp__KeTlsAlloc) {
    ctx.r3.u64 = dotm::kernel_tls_alloc();
}
PPC_FUNC(__imp__KeTlsFree) {
    ctx.r3.u64 = dotm::kernel_tls_free(ctx.r3.u32) ? 1U : 0U;
}
PPC_FUNC(__imp__KeTlsGetValue) {
    ctx.r3.u64 = dotm::kernel_tls_get_value(ctx.r3.u32);
}
PPC_FUNC(__imp__KeTlsSetValue) {
    ctx.r3.u64 = dotm::kernel_tls_set_value(ctx.r3.u32, ctx.r4.u32) ? 1U : 0U;
}
