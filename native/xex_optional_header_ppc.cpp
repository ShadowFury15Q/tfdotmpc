#include "ppc_recomp_shared.h"
#include "xex_optional_header.hpp"
#include "xex_title_boot.hpp"
// PPC r3 points to the actual guest XEX2 header; r4 is the requested key.
PPC_FUNC(__imp__RtlImageXexHeaderField) {
    (void)base;
    ctx.r3.u64 = dotm::bound_xex_optional_header_field(ctx.r3.u32, ctx.r4.u32);
}

PPC_FUNC(__imp__XexLoadImage) {
    ctx.r3.u64 = dotm::bound_xex_load_image(ctx.r3.u32, ctx.r4.u32,
                                             ctx.r5.u32, ctx.r6.u32);
}
PPC_FUNC(__imp__XexGetModuleHandle) {
    ctx.r3.u64 = dotm::bound_xex_get_module_handle(ctx.r3.u32, ctx.r4.u32);
}
PPC_FUNC(__imp__XexGetModuleSection) {
    ctx.r3.u64 = dotm::bound_xex_get_module_section(
        ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32);
}
PPC_FUNC(__imp__XexGetProcedureAddress) {
    ctx.r3.u64 = dotm::bound_xex_get_procedure_address(
        ctx.r3.u32, ctx.r4.u32, ctx.r5.u32);
}
PPC_FUNC(__imp__XexUnloadImage) {
    ctx.r3.u64 = dotm::bound_xex_unload_image(ctx.r3.u32);
}
