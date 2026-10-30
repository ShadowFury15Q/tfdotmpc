#include "ppc_recomp_shared.h"
#include "xex_optional_header.hpp"
// PPC r3 points to the actual guest XEX2 header; r4 is the requested key.
PPC_FUNC(__imp__RtlImageXexHeaderField) {
    (void)base;
    ctx.r3.u64 = dotm::bound_xex_optional_header_field(ctx.r3.u32, ctx.r4.u32);
}
