#include "ppc_recomp_shared.h"
#include <cstdint>
#include <stdexcept>
PPC_EXTERN_FUNC(__imp__KeQueryPerformanceFrequency);
int main() {
    PPCContext ctx{};
    __imp__KeQueryPerformanceFrequency(ctx, nullptr);
    const auto frequency = ctx.r3.u64;
    if (frequency < 1000000ULL || frequency > 20000000000ULL)
        throw std::runtime_error("PPC 64-bit timebase frequency is invalid");
    ctx.r3.u64 = 0;
    __imp__KeQueryPerformanceFrequency(ctx, nullptr);
    if (ctx.r3.u64 != frequency)
        throw std::runtime_error("PPC timebase frequency must be stable");
    return 0;
}
