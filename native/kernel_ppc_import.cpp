#include "ppc_recomp_shared.h" // Private XenonRecomp header; public CI uses a synthetic fixture.
#include "kernel_guest_memory.hpp"
#include <cstdint>

// Xbox 360 kernel ABI: five 32-bit PPC register arguments, NTSTATUS in r3.
PPC_FUNC(__imp__NtAllocateVirtualMemory) {
    ctx.r3.u64 = dotm::nt_bound_allocate_virtual_memory(
        static_cast<std::uint32_t>(ctx.r3.u64),
        static_cast<std::uint32_t>(ctx.r4.u64),
        static_cast<std::uint32_t>(ctx.r5.u64),
        static_cast<std::uint32_t>(ctx.r6.u64),
        static_cast<std::uint32_t>(ctx.r7.u64));
}
// Xbox 360 kernel ABI: four 32-bit PPC register arguments, NTSTATUS in r3.
PPC_FUNC(__imp__NtFreeVirtualMemory) {
    ctx.r3.u64 = dotm::nt_bound_free_virtual_memory(
        static_cast<std::uint32_t>(ctx.r3.u64),
        static_cast<std::uint32_t>(ctx.r4.u64),
        static_cast<std::uint32_t>(ctx.r5.u64),
        static_cast<std::uint32_t>(ctx.r6.u64));
}

// The original title runs in Xbox 360 user process type 1.
PPC_FUNC(__imp__KeGetCurrentProcessType) {
    ctx.r3.u64 = 1U;
}
