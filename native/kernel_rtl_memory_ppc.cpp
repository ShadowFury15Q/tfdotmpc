#include "ppc_recomp_shared.h"
#include "kernel_rtl_memory.hpp"

// Xbox 360 PowerPC calling convention: r3=guest pointer, r4=byte length,
// r5=32-bit pattern. RtlFillMemoryUlong returns void; preserve r3.
PPC_FUNC(__imp__RtlFillMemoryUlong) {
    dotm::rtl_bound_fill_memory_ulong(ctx.r3.u32, ctx.r4.u32, ctx.r5.u32);
}

// Returns the length in bytes of the leading matching DWORD sequence in r3.
PPC_FUNC(__imp__RtlCompareMemoryUlong) {
    ctx.r3.u64 = dotm::rtl_bound_compare_memory_ulong(
        ctx.r3.u32, ctx.r4.u32, ctx.r5.u32);
}
