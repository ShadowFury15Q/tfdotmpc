#include "ppc_recomp_shared.h"
#include <cstdint>
#include <stdexcept>
PPC_EXTERN_FUNC(__imp__NtYieldExecution);
int main() {
    PPCContext ctx{};
    ctx.r3.u64 = 0xDEADBEEFU;
    ctx.r4.u64 = 0x12345678U;
    ctx.lr = 0x8351E6A8U;
    __imp__NtYieldExecution(ctx, nullptr);
    if (ctx.r3.u64 != 0U && ctx.r3.u64 != 0x40000024U)
        throw std::runtime_error("NtYieldExecution returned an invalid Xbox NTSTATUS");
    if (ctx.r4.u64 != 0x12345678U || ctx.lr != 0x8351E6A8U)
        throw std::runtime_error("NtYieldExecution corrupted non-result PPC registers");
    return 0;
}
