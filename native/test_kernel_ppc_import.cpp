#include "ppc_recomp_shared.h"
#include "kernel_guest_memory.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>

PPC_EXTERN_FUNC(__imp__NtAllocateVirtualMemory);
PPC_EXTERN_FUNC(__imp__NtFreeVirtualMemory);

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
        check(allocate(0, 4096, 0x2000) == dotm::kNtSuccess,
              "Reservation-only must reserve guest address space");
        const auto reserved = read32(memory, ptr);
        check(!memory.committed_range(reserved, 1) &&
              heap.allocation_size(reserved) == 4096 &&
              heap.allocated_bytes() == 8192,
              "MEM_RESERVE does not commit physical guest pages");
        check(allocate(reserved, 2048, 0x1000) == dotm::kNtNotSupported,
              "Partial MEM_COMMIT is rejected without changing reservation");
        check(!memory.committed_range(reserved, 1),
              "Unsupported partial commit must not touch guest pages");
        check(allocate(reserved, 4096, 0x1000) == dotm::kNtSuccess,
              "MEM_COMMIT activates a full reserved region");
        check(memory.committed_range(reserved, 4096) &&
              heap.allocated_bytes() == 12288,
              "Committed reservation has real writable guest memory");
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
        std::cout << "DOTM synthetic Xbox 360 NT memory PPC ABI PASS\n";
        return 0;
    } catch (const std::exception& e) {
        dotm::bind_kernel_guest_memory(nullptr, nullptr);
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
