#include "ppc_recomp_shared.h"
#include "kernel_file_io.hpp"
// Xbox 360 NtCreateFile has nine arguments; the ninth is at PPC SP+84.
PPC_FUNC(__imp__NtCreateFile) {
    ctx.r3.u64 = dotm::nt_bound_create_file(ctx.r3.u32, ctx.r4.u32,
        ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, ctx.r8.u32,
        ctx.r9.u32, ctx.r10.u32, ctx.r1.u32);
}
// NtReadFile also has nine arguments; asynchronous APC/event I/O is rejected.
// NtOpenFile has six PPC register arguments (no stack-spilled arguments).
PPC_FUNC(__imp__NtOpenFile) {
    ctx.r3.u64 = dotm::nt_bound_open_file(ctx.r3.u32, ctx.r4.u32,
        ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, ctx.r8.u32);
}
PPC_FUNC(__imp__NtReadFile) {
    ctx.r3.u64 = dotm::nt_bound_read_file(ctx.r3.u32, ctx.r4.u32,
        ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, ctx.r8.u32,
        ctx.r9.u32, ctx.r10.u32, ctx.r1.u32);
}

 // NtQueryInformationFile: handle, IO_STATUS_BLOCK*, buffer, size, class.
PPC_FUNC(__imp__NtQueryInformationFile) {
    ctx.r3.u64 = dotm::nt_bound_query_information_file(ctx.r3.u32,
        ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32);
}
