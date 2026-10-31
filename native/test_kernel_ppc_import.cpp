#include "ppc_recomp_shared.h"
#include "kernel_guest_memory.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>

PPC_EXTERN_FUNC(__imp__NtAllocateVirtualMemory);
PPC_EXTERN_FUNC(__imp__NtFreeVirtualMemory);
PPC_EXTERN_FUNC(__imp__NtQueryVirtualMemory);
PPC_EXTERN_FUNC(__imp__KeGetCurrentProcessType);
PPC_EXTERN_FUNC(__imp__KeQuerySystemTime);

namespace {
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
void write32(dotm::GuestMemory& mem, std::uint32_t addr, std::uint32_t value) {
    auto* p = mem.address(addr);
    for (int i = 0; i != 4; ++i)
        p[i] = static_cast<std::uint8_t>(value >> (24 - 8 * i));
}
std::uint32_t read32(const dotm::GuestMemory& mem, std::uint32_t addr) {
    const auto* p = mem.address(addr);
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
           (std::uint32_t(p[2]) << 8) | p[3];
}
}
int main() {
    try {
        dotm::GuestMemory memory;
        dotm::GuestVirtualAllocator heap(memory, 0x40000000U, 0x40010000U);
        constexpr std::uint32_t ptr = 0x82030000U;
        constexpr std::uint32_t size_ptr = ptr + 4;
        memory.commit(ptr, 4096);
        dotm::bind_kernel_guest_memory(&memory, &heap);
        PPCContext ctx{};
        ctx.r3.u64 = 0xFFFFFFFFU;
        __imp__KeGetCurrentProcessType(ctx, memory.base());
        check(ctx.r3.u64 == 1U, "Retail user process type ABI");
        // The original PPC CRT reads a 64-bit Xbox UTC timestamp from r3.
        const auto timestamp = ptr + 0x80U;
        dotm::ke_query_system_time_guest(memory, timestamp,
                                         0x0123456789ABCDEFULL);
        const std::uint8_t expected_time[] = {
            0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
        for (unsigned i = 0; i < 8; ++i)
            check(memory.address(timestamp)[i] == expected_time[i],
                  "KeQuerySystemTime big-endian 64-bit store");
        ctx.r3.u64 = timestamp;
        __imp__KeQuerySystemTime(ctx, memory.base());
        check(ctx.r3.u32 == timestamp, "KeQuerySystemTime void PPC ABI");
        const auto unix_utc =
            (std::uint64_t(read32(memory, timestamp)) << 32) |
            read32(memory, timestamp + 4);
        check(unix_utc > 116444736000000000ULL,
              "KeQuerySystemTime returns UTC FILETIME rather than Unix seconds");
        bool time_rejected = false;
        try { dotm::ke_query_system_time_guest(memory, timestamp + 1, 1); }
        catch (const std::runtime_error&) { time_rejected = true; }
        check(time_rejected, "Unaligned KeQuerySystemTime output rejected");
        time_rejected = false;
        try { dotm::ke_query_system_time_guest(memory, 0x50000000U, 1); }
        catch (const std::runtime_error&) { time_rejected = true; }
        check(time_rejected, "Uncommitted KeQuerySystemTime output rejected");
        time_rejected = false;
        try { dotm::ke_query_system_time_guest(memory,
                 static_cast<std::uint32_t>(dotm::GuestMemory::kFunctionTableOffset), 1); }
        catch (const std::runtime_error&) { time_rejected = true; }
        check(time_rejected, "KeQuerySystemTime protects PPC dispatch table");
        auto allocate = [&](std::uint32_t base, std::uint32_t size,
                            std::uint32_t type = 0x3000U,
                            std::uint32_t protect = 4U,
                            std::uint32_t debug = 0U) {
            write32(memory, ptr, base);
            write32(memory, size_ptr, size);
            ctx.r3.u64 = ptr;
            ctx.r4.u64 = size_ptr;
            ctx.r5.u64 = type;
            ctx.r6.u64 = protect;
            ctx.r7.u64 = debug;
            __imp__NtAllocateVirtualMemory(ctx, memory.base());
            return static_cast<std::uint32_t>(ctx.r3.u64);
        };
        auto release = [&](std::uint32_t base, std::uint32_t size,
                           std::uint32_t type = 0x8000U) {
            write32(memory, ptr, base);
            write32(memory, size_ptr, size);
            ctx.r3.u64 = ptr;
            ctx.r4.u64 = size_ptr;
            ctx.r5.u64 = type;
            ctx.r6.u64 = 0;
            __imp__NtFreeVirtualMemory(ctx, memory.base());
            return static_cast<std::uint32_t>(ctx.r3.u64);
        };
        check(allocate(0, 1) == dotm::kNtSuccess, "PPC allocate NTSTATUS");
        check(read32(memory, ptr) == 0x40000000U, "PPC pointer is big-endian");
        check(read32(memory, size_ptr) == 4096, "PPC rounded size");
        check(heap.allocated_bytes() == 4096, "Allocator backing is real");
        memory.address(0x40000000U)[0] = 0xCD;
        check(allocate(0x40003013U, 100) == dotm::kNtSuccess,
              "Unaligned fixed base rounds down");
        check(read32(memory, ptr) == 0x40003000U &&
              read32(memory, size_ptr) == 4096, "Fixed allocation outputs");
        // MEM_RELEASE writes RegionSize after decommitting the region.
        // The output pointer must not be inside the allocation being freed.
        write32(memory, ptr, 0x40003000U);
        write32(memory, 0x40003000U, 0);
        ctx.r3.u64 = ptr;
        ctx.r4.u64 = 0x40003000U;
        ctx.r5.u64 = 0x8000U;
        ctx.r6.u64 = 0;
        __imp__NtFreeVirtualMemory(ctx, memory.base());
        check(ctx.r3.u64 == dotm::kNtInvalidParameter,
              "MEM_RELEASE rejects an output word in revoked guest pages");
        check(memory.committed_range(0x40003000U, 4) &&
              heap.allocation_size(0x40003000U).has_value(),
              "Rejected MEM_RELEASE preserves the original allocation");
        check(allocate(0x40003000U, 4096) == dotm::kNtNoMemory,
              "Overlapping fixed address rejected");
        check(read32(memory, ptr) == 0x40003000U &&
              read32(memory, size_ptr) == 4096,
              "Failed allocation leaves guest arguments unchanged");
        check(allocate(0, 0) == dotm::kNtInvalidParameter,
              "Zero region rejected");
        check(allocate(0, 8192, 0x2000) == dotm::kNtSuccess,
              "Reservation-only must reserve guest address space");
        const auto reserved = read32(memory, ptr);
        check(!memory.committed_range(reserved, 1) &&
              heap.allocation_size(reserved) == 8192 &&
              heap.allocated_bytes() == 8192,
              "MEM_RESERVE does not commit physical guest pages");
        check(allocate(reserved, 4096, 0x1000) == dotm::kNtSuccess,
              "Partial MEM_COMMIT activates first reserved guest page");
        check(memory.committed_range(reserved, 4096) &&
              !memory.committed_range(reserved + 4096, 4096) &&
              heap.allocated_bytes() == 12288,
              "Partial commit leaves neighboring reserved page untouched");
        check(allocate(reserved + 4096, 4096, 0x1000) == dotm::kNtSuccess,
              "Interior MEM_COMMIT activates second reserved guest page");
        check(memory.committed_range(reserved, 8192) &&
              heap.allocated_bytes() == 16384,
              "Both partial commits account for the full reservation");
        check(allocate(reserved, 4096, 0x1000) == dotm::kNtSuccess &&
              heap.allocated_bytes() == 16384,
              "Duplicate MEM_COMMIT succeeds without double-counting committed pages");
        memory.address(reserved)[0] = 0xAB;
        memory.address(reserved + 4096)[0] = 0xCD;
        check(allocate(reserved, 8192, 0x1000) == dotm::kNtSuccess &&
              heap.allocated_bytes() == 16384 &&
              memory.address(reserved)[0] == 0xAB &&
              memory.address(reserved + 4096)[0] == 0xCD,
              "Repeated multi-page MEM_COMMIT preserves guest data");
        check(release(reserved, 4096, 0x4000U) == dotm::kNtSuccess,
              "Partial MEM_DECOMMIT revokes first guest page");
        check(!memory.committed_range(reserved, 4096) &&
              memory.committed_range(reserved + 4096, 4096) &&
              memory.address(reserved + 4096)[0] == 0xCD &&
              heap.allocated_bytes() == 12288,
              "Partial decommit preserves neighboring committed guest page");
        check(release(reserved + 4096, 4096, 0x4000U) == dotm::kNtSuccess,
              "Interior MEM_DECOMMIT revokes remaining guest page");
        check(!memory.committed_range(reserved, 8192) &&
              heap.allocation_size(reserved) == 8192 &&
              heap.allocated_bytes() == 8192,
              "Decommitted reservation retains address space and accounting");
        check(allocate(reserved, 8192, 0x1000U) == dotm::kNtSuccess,
              "MEM_COMMIT restores decommitted reservation");
        check(memory.address(reserved)[0] == 0 &&
              heap.allocated_bytes() == 16384,
              "Recommitted guest memory is zero-filled");
        check(release(reserved, 0) == dotm::kNtSuccess,
              "MEM_RELEASE frees committed reservation");
        check(!memory.committed_range(reserved, 1),
              "Released reservation revokes guest pages");
        check(allocate(0, 4096, 0x2000) == dotm::kNtSuccess,
              "Reservation-only can be repeated");
        const auto uncommitted_reservation = read32(memory, ptr);
        check(release(uncommitted_reservation, 0) == dotm::kNtSuccess,
              "MEM_RELEASE frees never-committed reservation");
        check(allocate(0, 4096, 0x1000) == dotm::kNtSuccess,
              "Null-base MEM_COMMIT implicitly reserves pages");
        const auto implicit_reservation = read32(memory, ptr);
        check(memory.committed_range(implicit_reservation, 4096),
              "Implicit reservation is committed");
        check(release(implicit_reservation, 0) == dotm::kNtSuccess,
              "Implicit reservation can be released");
        check(allocate(0x40005000U, 4096, 0x1000) == dotm::kNtMemoryNotAllocated,
              "MEM_COMMIT cannot invent an existing fixed reservation");
        check(allocate(0, 4096, 0x3000, 0x20) == dotm::kNtNotSupported,
              "Execute-only protection must fail closed");
        check(allocate(0, 4096, 0x3000, 4, 1) == dotm::kNtInvalidParameter,
              "Devkit memory unsupported");
        check(allocate(0xFFFFFFF0U, 4096) == dotm::kNtInvalidParameter,
              "Guest size overflow rejected");
        check(heap.allocation_count() == 2, "Rejected requests do not allocate");
        check(release(0x40000000U, 1) == dotm::kNtInvalidParameter,
              "MEM_RELEASE requires zero size");
        check(release(0x40000000U, 0, 0x4000U) == dotm::kNtNotSupported,
              "MEM_DECOMMIT must fail closed");
        check(memory.committed_range(0x40000000U, 1),
              "Unsupported free preserves allocation");
        check(release(0x40000001U, 0) == dotm::kNtMemoryNotAllocated,
              "Interior pointer rejected");
        check(release(0x40000000U, 0) == dotm::kNtSuccess,
              "PPC MEM_RELEASE succeeded");
        check(read32(memory, size_ptr) == 4096, "Freed size written big-endian");
        check(!memory.committed_range(0x40000000U, 1),
              "PPC free revokes host memory");
        check(release(0x40000000U, 0) == dotm::kNtMemoryNotAllocated,
              "PPC double-free rejected");
        check(allocate(0, 4096) == dotm::kNtSuccess &&
              read32(memory, ptr) == 0x40000000U &&
              memory.address(0x40000000U)[0] == 0,
              "Freed page reused zero-filled");
        ctx.r3.u64 = dotm::GuestMemory::kFunctionTableOffset;
        ctx.r4.u64 = size_ptr;
        ctx.r5.u64 = 0x3000U;
        ctx.r6.u64 = 4;
        ctx.r7.u64 = 0;
        __imp__NtAllocateVirtualMemory(ctx, memory.base());
        check(ctx.r3.u64 == dotm::kNtInvalidParameter,
              "Kernel import refuses dispatch-table output");
        ctx.r3.u64 = ptr;
        ctx.r4.u64 = ptr;
        __imp__NtAllocateVirtualMemory(ctx, memory.base());
        check(ctx.r3.u64 == dotm::kNtInvalidParameter,
              "Aliased guest outputs rejected");
        memory.decommit(ptr, 4096);
        ctx.r3.u64 = ptr;
        ctx.r4.u64 = size_ptr;
        __imp__NtAllocateVirtualMemory(ctx, memory.base());
        check(ctx.r3.u64 == dotm::kNtInvalidParameter,
              "Uncommitted guest pointers rejected");
        dotm::bind_kernel_guest_memory(nullptr, nullptr);
        check(dotm::nt_bound_allocate_virtual_memory(ptr, size_ptr, 0x3000, 4, 0)
              == dotm::kNtNotSupported, "Unbound kernel does not claim success");
        // Real Xbox 360 retail heap: 64 KiB reserve and partial commit.
        dotm::GuestMemory large_memory;
        dotm::GuestVirtualAllocator large_heap(
            large_memory, 0x50000000U, 0x51000000U);
        large_memory.commit(ptr, 4096);
        dotm::bind_kernel_guest_memory(&large_memory, &large_heap);
        auto call_large = [&](std::uint32_t base, std::uint32_t size,
                              std::uint32_t type) {
            write32(large_memory, ptr, base);
            write32(large_memory, size_ptr, size);
            ctx.r3.u64 = ptr;
            ctx.r4.u64 = size_ptr;
            ctx.r5.u64 = type;
            ctx.r6.u64 = 4;
            ctx.r7.u64 = 0;
            __imp__NtAllocateVirtualMemory(ctx, large_memory.base());
            return static_cast<std::uint32_t>(ctx.r3.u64);
        };
        check(call_large(0, 0x100000U, 0x60002000U) == dotm::kNtSuccess &&
              read32(large_memory, ptr) == 0x50000000U &&
              read32(large_memory, size_ptr) == 0x100000U &&
              !large_memory.committed_range(0x50000000U, 1),
              "Xbox 64 KiB page reserve must allocate real guest VA");
        check(call_large(0x50000000U, 0x10000U, 0x60001000U) == dotm::kNtSuccess &&
              large_memory.committed_range(0x50000000U, 0x10000U) &&
              !large_memory.committed_range(0x50010000U, 1),
              "Xbox 64 KiB partial commit preserves reservation");
        check(call_large(0, 0x1800U, 0x20801000U) == dotm::kNtSuccess &&
              read32(large_memory, ptr) == 0x50100000U &&
              read32(large_memory, size_ptr) == 0x10000U &&
              large_memory.committed_range(0x50100000U, 0x10000U),
              "Xbox LARGE_PAGES|NOZERO commits guest pages");
        check(call_large(0x50001000U, 0x10000U, 0x60001000U) ==
                  dotm::kNtInvalidParameter &&
              read32(large_memory, ptr) == 0x50001000U,
              "Misaligned Xbox 64 KiB commit rejected without mutation");
        check(call_large(0, 0x10000U, 0x40002000U) == dotm::kNtNotSupported &&
              read32(large_memory, ptr) == 0,
              "Unknown page-class combination fails closed");
        write32(large_memory, ptr, 0x50100000U);
        write32(large_memory, size_ptr, 0);
        ctx.r3.u64 = ptr;
        ctx.r4.u64 = size_ptr;
        ctx.r5.u64 = 0x8000U;
        ctx.r6.u64 = 0;
        __imp__NtFreeVirtualMemory(ctx, large_memory.base());
        check(ctx.r3.u64 == dotm::kNtSuccess &&
              !large_memory.committed_range(0x50100000U, 1),
              "Xbox NOZERO allocation can be released");
        // The original title calls NtQueryVirtualMemory when extending its CRT heap.
        auto query = [&](std::uint32_t address, std::uint32_t output = ptr) {
            ctx.r3.u64 = address;
            ctx.r4.u64 = output;
            __imp__NtQueryVirtualMemory(ctx, large_memory.base());
            return static_cast<std::uint32_t>(ctx.r3.u64);
        };
        check(query(0x50000001U) == dotm::kNtSuccess &&
              read32(large_memory, ptr) == 0x50000000U &&
              read32(large_memory, ptr + 4) == 0x50000000U &&
              read32(large_memory, ptr + 12) == 0x10000U &&
              read32(large_memory, ptr + 16) == 0x1000U &&
              read32(large_memory, ptr + 20) == 4U &&
              read32(large_memory, ptr + 24) == 0x20000U,
              "NtQueryVirtualMemory reports a committed 64 KiB guest run");
        check(query(0x50010000U) == dotm::kNtSuccess &&
              read32(large_memory, ptr) == 0x50010000U &&
              read32(large_memory, ptr + 12) == 0xF0000U &&
              read32(large_memory, ptr + 16) == 0x2000U &&
              read32(large_memory, ptr + 20) == 0U,
              "NtQueryVirtualMemory reports remaining reserved pages");
        check(query(0x50100000U) == dotm::kNtSuccess &&
              read32(large_memory, ptr) == 0x50100000U &&
              read32(large_memory, ptr + 4) == 0U &&
              read32(large_memory, ptr + 16) == 0x10000U,
              "NtQueryVirtualMemory reports released guest pages as free");
        write32(large_memory, ptr, 0xAABBCCDDU);
        check(query(0x82000000U) == dotm::kNtInvalidParameter &&
              read32(large_memory, ptr) == 0xAABBCCDDU,
              "Unknown address must not corrupt query output");
        check(query(0x50000000U, dotm::GuestMemory::kFunctionTableOffset) ==
                  dotm::kNtInvalidParameter,
              "Query output must never overwrite the native PPC dispatch table");
        large_memory.decommit(ptr, 4096);
        check(query(0x50000000U) == dotm::kNtInvalidParameter,
              "Uncommitted output rejected");
        dotm::bind_kernel_guest_memory(nullptr, nullptr);
        check(dotm::nt_bound_query_virtual_memory(0x50000000U, ptr) ==
                  dotm::kNtNotSupported,
              "Unbound memory query must fail closed");
        std::cout << "DOTM synthetic Xbox 360 NT memory PPC ABI PASS\n";
        return 0;
    } catch (const std::exception& e) {
        dotm::bind_kernel_guest_memory(nullptr, nullptr);
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
