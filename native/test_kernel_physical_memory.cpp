#include "ppc_recomp_shared.h"
#include "kernel_guest_memory.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>
PPC_EXTERN_FUNC(__imp__MmAllocatePhysicalMemoryEx);
PPC_EXTERN_FUNC(__imp__MmFreePhysicalMemory);
PPC_EXTERN_FUNC(__imp__MmQueryAllocationSize);
PPC_EXTERN_FUNC(__imp__MmGetPhysicalAddress);
namespace {
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
std::uint32_t allocate(PPCContext& ctx, std::uint8_t* base,
                       std::uint32_t flags, std::uint32_t bytes,
                       std::uint32_t protect, std::uint32_t min_address,
                       std::uint32_t max_address, std::uint32_t alignment) {
    ctx.r3.u64 = flags; ctx.r4.u64 = bytes; ctx.r5.u64 = protect;
    ctx.r6.u64 = min_address; ctx.r7.u64 = max_address;
    ctx.r8.u64 = alignment;
    __imp__MmAllocatePhysicalMemoryEx(ctx, base);
    return ctx.r3.u32;
}
}
int main() {
    try {
        dotm::GuestMemory memory;
        dotm::GuestVirtualAllocator allocator(memory, 0x40000000U, 0x40010000U);
        dotm::bind_kernel_guest_memory(&memory, &allocator);
        PPCContext ctx{};
        const auto first = allocate(ctx, memory.base(), 2, 8192, 0x404,
                                    0, 0xFFFFFFFFU, 4096);
        check(first >= 0xE0000000U && first % 4096 == 0 &&
              memory.committed_range(first, 8192),
              "4 KiB physical-view allocation and PPC r3-r8 ABI");
        memory.address(first)[0] = 0xA5;
        ctx.r3.u64 = first + 4096;
        __imp__MmGetPhysicalAddress(ctx, memory.base());
        check(ctx.r3.u32 == (first - 0xE0000000U) + 4096,
              "Physical offset resolves interior guest address");
        ctx.r3.u64 = first;
        __imp__MmQueryAllocationSize(ctx, memory.base());
        check(ctx.r3.u32 == 8192, "Physical allocation size");
        const auto large = allocate(ctx, memory.base(), 0, 4096,
                                    0x20000004U, 0, 0xFFFFU, 65536);
        check(large == 0xA0000000U &&
              memory.committed_range(large, 65536),
              "64 KiB physical view obeys physical min/max bounds");
        const auto collision = allocate(ctx, memory.base(), 0, 4096,
                                        0x404, 0, 0xFFFFU, 4096);
        check(collision == 0,
              "4 KiB view cannot double-book 64 KiB physical pages");
        check(allocate(ctx, memory.base(), 0, 4096, 0, 0, 0xFFFFFFFFU, 4096) == 0,
              "Reject missing protection");
        check(allocate(ctx, memory.base(), 0, 4096, 4, 0x10000, 0xFFFF, 4096) == 0,
              "Reject reversed physical address range");
        check(allocate(ctx, memory.base(), 0, 4096, 4, 0, 0xFFFFFFFFU, 3000) == 0,
              "Reject non-power-of-two alignment");
        ctx.r3.u64 = 0; ctx.r4.u64 = first;
        __imp__MmFreePhysicalMemory(ctx, memory.base());
        check(!memory.committed_range(first, 4096),
              "Physical free decommits host-backed guest pages");
        const auto reused = allocate(ctx, memory.base(), 2, 8192, 0x404,
                                     0, 0xFFFFFFFFU, 4096);
        check(reused == first && memory.address(reused)[0] == 0,
              "Freed physical pages are zeroed when reused");
        dotm::bind_kernel_guest_memory(nullptr, nullptr);
        check(!memory.committed_range(reused, 8192) &&
              !memory.committed_range(large, 65536),
              "Unbinding releases outstanding physical allocations");
        std::cout << "Xbox 360 physical memory PPC ABI PASS\n";
        return 0;
    } catch (const std::exception& e) {
        dotm::bind_kernel_guest_memory(nullptr, nullptr);
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
