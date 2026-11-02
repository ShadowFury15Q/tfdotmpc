#include "ppc_recomp_shared.h" // Private XenonRecomp header; public CI uses a synthetic fixture.
#include "kernel_guest_memory.hpp"
#include <cstdint>
#include <array>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <vector>
#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(__i386__) || defined(__x86_64__)
#include <x86intrin.h>
#endif

// KfAcquireSpinLock raises the caller's IRQL to DISPATCH_LEVEL and returns
// the previous IRQL in PPC r3. KfReleaseSpinLock restores that exact value.
// Keep ownership per native guest thread; never release a lock acquired by a
// different thread or silently accept a mismatched restore IRQL.
namespace {
struct GuestSpinFrame {
    std::uint32_t guest_address;
    std::uint32_t previous_irql;
};
thread_local std::uint32_t current_guest_irql = 0U;
thread_local std::vector<GuestSpinFrame> guest_spin_frames;
}
PPC_FUNC(__imp__KfAcquireSpinLock) {
    (void)base;
    const auto address = ctx.r3.u32;
    if (current_guest_irql > 2U)
        throw std::runtime_error("KfAcquireSpinLock: invalid guest IRQL");
    for (const auto& frame : guest_spin_frames)
        if (frame.guest_address == address)
            throw std::runtime_error("KfAcquireSpinLock: recursive guest lock");
    const auto previous = current_guest_irql;
    current_guest_irql = 2U;
    try {
        dotm::ke_bound_acquire_spin_lock_at_raised_irql(address);
    } catch (...) {
        current_guest_irql = previous;
        throw;
    }
    try {
        guest_spin_frames.push_back({address, previous});
    } catch (...) {
        dotm::ke_bound_release_spin_lock_from_raised_irql(address);
        current_guest_irql = previous;
        throw;
    }
    ctx.r3.u64 = previous;
}
PPC_FUNC(__imp__KfReleaseSpinLock) {
    (void)base;
    const auto address = ctx.r3.u32;
    const auto restore_irql = ctx.r4.u32;
    if (guest_spin_frames.empty() ||
        guest_spin_frames.back().guest_address != address ||
        guest_spin_frames.back().previous_irql != restore_irql ||
        current_guest_irql != 2U)
        throw std::runtime_error("KfReleaseSpinLock: invalid owner, order or IRQL");
    dotm::ke_bound_release_spin_lock_from_raised_irql(address);
    guest_spin_frames.pop_back();
    current_guest_irql = restore_irql;
    // This import is void: r3 retains the guest lock pointer.
}

// Xbox kernel bugchecks never return to the guest. Surface the original
// stop code and four PPC parameters to the host instead of continuing after
// a fatal title/kernel invariant failure or terminating without diagnostics.
PPC_FUNC(__imp__KeBugCheck) {
    (void)base;
    char detail[96]{};
    std::snprintf(detail, sizeof(detail),
                  "Xbox 360 KeBugCheck: stop=0x%08X", ctx.r3.u32);
    throw std::runtime_error(detail);
}
PPC_FUNC(__imp__KeBugCheckEx) {
    (void)base;
    char detail[192]{};
    std::snprintf(detail, sizeof(detail),
                  "Xbox 360 KeBugCheckEx: stop=0x%08X args=%08X,%08X,%08X,%08X",
                  ctx.r3.u32, ctx.r4.u32, ctx.r5.u32,
                  ctx.r6.u32, ctx.r7.u32);
    throw std::runtime_error(detail);
}

// The AtRaisedIrql variants preserve PPC r3 and do not change IRQL.
PPC_FUNC(__imp__KeAcquireSpinLockAtRaisedIrql) {
    (void)base;
    dotm::ke_bound_acquire_spin_lock_at_raised_irql(ctx.r3.u32);
}
PPC_FUNC(__imp__KeReleaseSpinLockFromRaisedIrql) {
    (void)base;
    dotm::ke_bound_release_spin_lock_from_raised_irql(ctx.r3.u32);
}

// KeInitializeDpc(PKDPC, PKDEFERRED_ROUTINE, DeferredContext) is void.
PPC_FUNC(__imp__KeInitializeDpc) {
    (void)base;
    dotm::ke_bound_initialize_dpc(ctx.r3.u32, ctx.r4.u32, ctx.r5.u32);
}

// Xbox 360 pool allocations are guest-addressed, not host malloc pointers.
// ExFreePool is void: the original PPC caller retains r3.
PPC_FUNC(__imp__ExAllocatePool) {
    (void)base;
    ctx.r3.u64 = dotm::ex_bound_allocate_pool(ctx.r3.u32, ctx.r4.u32, 0U);
}
PPC_FUNC(__imp__ExAllocatePoolWithTag) {
    (void)base;
    ctx.r3.u64 = dotm::ex_bound_allocate_pool(ctx.r3.u32, ctx.r4.u32, ctx.r5.u32);
}
PPC_FUNC(__imp__ExFreePool) {
    (void)base;
    dotm::ex_bound_free_pool(ctx.r3.u32);
}

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

// Xbox 360 physical allocation: six register arguments and pointer result.
PPC_FUNC(__imp__MmAllocatePhysicalMemoryEx) {
    (void)base;
    ctx.r3.u64 = dotm::mm_bound_allocate_physical_memory_ex(
        ctx.r3.u32, ctx.r4.u32, ctx.r5.u32,
        ctx.r6.u32, ctx.r7.u32, ctx.r8.u32);
}
PPC_FUNC(__imp__MmFreePhysicalMemory) {
    (void)base;
    dotm::mm_bound_free_physical_memory(ctx.r3.u32, ctx.r4.u32);
}
PPC_FUNC(__imp__MmQueryAllocationSize) {
    (void)base;
    ctx.r3.u64 = dotm::mm_bound_query_physical_allocation_size(ctx.r3.u32);
}
PPC_FUNC(__imp__MmGetPhysicalAddress) {
    (void)base;
    ctx.r3.u64 = dotm::mm_bound_get_physical_address(ctx.r3.u32);
}
// Xbox 360 physical page protection query returns XDK PAGE_* flags in r3.
PPC_FUNC(__imp__MmQueryAddressProtect) {
    (void)base;
    ctx.r3.u64 = dotm::mm_bound_query_address_protect(ctx.r3.u32);
}
// Xbox 360 MmSetAddressProtect is void: r3 contains the original address.
PPC_FUNC(__imp__MmSetAddressProtect) {
    (void)base;
    dotm::mm_bound_set_address_protect(ctx.r3.u32, ctx.r4.u32, ctx.r5.u32);
}

// Xbox 360 MmQueryStatistics: pointer to 104-byte big-endian structure.
PPC_FUNC(__imp__MmQueryStatistics) {
    ctx.r3.u64 = dotm::mm_bound_query_statistics(ctx.r3.u32);
}

// Xbox 360 KeQuerySystemTime takes a pointer to a big-endian 64-bit
// LARGE_INTEGER in r3 and returns void (preserves the PPC argument register).
PPC_FUNC(__imp__KeQuerySystemTime) {
    dotm::ke_bound_query_system_time(ctx.r3.u32);
}


// Original CRT and file timestamps use big-endian NT TIME_FIELDS (8 x SHORT).
// RtlTimeToTimeFields is void; RtlTimeFieldsToTime returns BOOLEAN in r3.
PPC_FUNC(__imp__RtlTimeToTimeFields) {
    (void)base;
    dotm::rtl_bound_time_to_time_fields(ctx.r3.u32, ctx.r4.u32);
}
PPC_FUNC(__imp__RtlTimeFieldsToTime) {
    (void)base;
    ctx.r3.u64 = dotm::rtl_bound_time_fields_to_time(ctx.r3.u32, ctx.r4.u32);
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

// Xbox 360 ExGetXConfigSetting(category, setting, buffer, size, required_size).
PPC_FUNC(__imp__ExGetXConfigSetting) {
    (void)base;
    ctx.r3.u64 = dotm::ex_bound_get_xconfig_setting(
        static_cast<std::uint16_t>(ctx.r3.u32),
        static_cast<std::uint16_t>(ctx.r4.u32), ctx.r5.u32,
        static_cast<std::uint16_t>(ctx.r6.u32), ctx.r7.u32);
}

// XGetVideoMode and VdQueryVideoMode share the 48-byte Xbox X_VIDEO_MODE ABI.
PPC_FUNC(__imp__XGetVideoMode) {
    (void)base;
    dotm::vd_bound_query_video_mode(ctx.r3.u32);
}
PPC_FUNC(__imp__VdQueryVideoMode) {
    (void)base;
    dotm::vd_bound_query_video_mode(ctx.r3.u32);
}
PPC_FUNC(__imp__VdGetCurrentDisplayGamma) {
    (void)base;
    dotm::vd_bound_get_current_display_gamma(ctx.r3.u32, ctx.r4.u32);
}
PPC_FUNC(__imp__VdQueryVideoFlags) {
    (void)base;
    ctx.r3.u64 = dotm::vd_bound_query_video_flags();
}

// Retail xboxkrnl DbgPrint: ANSI format in r3, 32-bit arguments in r4-r10.
// The kernel bridge validates guest pointers before logging to host stderr.
PPC_FUNC(__imp__DbgPrint) {
    (void)base;
    ctx.r3.u64 = dotm::ke_bound_dbg_print(ctx.r3.u32,
        {ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32,
         ctx.r8.u32, ctx.r9.u32, ctx.r10.u32});
}
