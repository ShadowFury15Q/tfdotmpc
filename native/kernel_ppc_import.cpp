#include "ppc_recomp_shared.h" // Private XenonRecomp header; public CI uses a synthetic fixture.
#include "kernel_guest_memory.hpp"
#include <cstdint>
#include <chrono>
#include <stdexcept>
#include <thread>
#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(__i386__) || defined(__x86_64__)
#include <x86intrin.h>
#endif

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

// Xbox 360 MEMORY_BASIC_INFORMATION is seven big-endian 32-bit words.
PPC_FUNC(__imp__NtQueryVirtualMemory) {
    ctx.r3.u64 = dotm::nt_bound_query_virtual_memory(
        static_cast<std::uint32_t>(ctx.r3.u64),
        static_cast<std::uint32_t>(ctx.r4.u64));
}

// The original title runs in Xbox 360 user process type 1.
PPC_FUNC(__imp__KeGetCurrentProcessType) {
    ctx.r3.u64 = 1U;
}

namespace {
std::uint64_t host_timebase_frequency() {
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
    // XenonRecomp emits host __rdtsc() for original mftb instructions.
    // Return the frequency of THAT counter, not the Xbox's nominal 50 MHz;
    // otherwise original game timing divides incompatible time units.
    const auto start = std::chrono::steady_clock::now();
    const auto cycles_start = __rdtsc();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const auto cycles_end = __rdtsc();
    const auto end = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    if (elapsed <= 0 || cycles_end <= cycles_start)
        throw std::runtime_error("Host PPC timebase calibration failed");
    const auto hz = static_cast<long double>(cycles_end - cycles_start) *
                    1000000000.0L / static_cast<long double>(elapsed);
    if (hz < 1000000.0L || hz > 20000000000.0L)
        throw std::runtime_error("Host PPC timebase frequency out of range");
    return static_cast<std::uint64_t>(hz);
#else
    // The original private recompilation currently targets x86/x64 only.
    throw std::runtime_error("Original PPC timebase requires x86 TSC");
#endif
}
}
// Original PPC callers consume the entire 64-bit result from r3 (std r3,...).
PPC_FUNC(__imp__KeQueryPerformanceFrequency) {
    (void)base;
    static const std::uint64_t frequency = host_timebase_frequency();
    ctx.r3.u64 = frequency;
}
