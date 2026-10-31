#include "ppc_recomp_shared.h"
#include "kernel_guest_memory.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>
PPC_EXTERN_FUNC(__imp__MmQueryStatistics);
PPC_EXTERN_FUNC(__imp__ExGetXConfigSetting);
PPC_EXTERN_FUNC(__imp__XGetVideoMode);
PPC_EXTERN_FUNC(__imp__VdQueryVideoMode);
PPC_EXTERN_FUNC(__imp__VdQueryVideoFlags);
namespace {
void check(bool ok, const char* msg) { if (!ok) throw std::runtime_error(msg); }
void put32(dotm::GuestMemory& m, std::uint32_t a, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) m.address(a)[i] = std::uint8_t(v >> (24 - i * 8));
}
std::uint32_t get32(dotm::GuestMemory& m, std::uint32_t a) {
    auto* p = m.address(a);
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
           (std::uint32_t(p[2]) << 8) | p[3];
}
}
int main() {
    try {
        dotm::GuestMemory memory;
        dotm::GuestVirtualAllocator allocator(memory, 0x40000000U, 0x40010000U);
        constexpr std::uint32_t output = 0x82030000U;
        memory.commit(output, 4096);
        dotm::bind_kernel_guest_memory(&memory, &allocator);
        PPCContext ctx{};
        put32(memory, output, 104);
        ctx.r3.u64 = output;
        __imp__MmQueryStatistics(ctx, memory.base());
        check(ctx.r3.u32 == 0 && get32(memory, output) == 104 &&
              get32(memory, output + 4) == 0x20000U &&
              get32(memory, output + 12) == 0x20000U &&
              get32(memory, output + 16) == 0x10000U &&
              get32(memory, output + 20) == 0 &&
              get32(memory, output + 100) == 0x1FFFFU,
              "Initial Xbox memory statistics and PPC big-endian ABI");
        check(allocator.allocate(8192).has_value(), "Commit two guest pages");
        check(allocator.reserve(4096).has_value(), "Reserve one guest page");
        put32(memory, output, 104);
        ctx.r3.u64 = output;
        __imp__MmQueryStatistics(ctx, memory.base());
        check(ctx.r3.u32 == 0 &&
              get32(memory, output + 12) == 0x20000U - 2 &&
              get32(memory, output + 20) == 12288 &&
              get32(memory, output + 24) == 2 &&
              get32(memory, output + 40) == 2 &&
              get32(memory, output + 44) == 2,
              "Statistics track real committed and reserved guest pages");
        put32(memory, output, 100);
        put32(memory, output + 4, 0x12345678U);
        ctx.r3.u64 = output;
        __imp__MmQueryStatistics(ctx, memory.base());
        check(ctx.r3.u32 == 0xC0000023U &&
              get32(memory, output + 4) == 0x12345678U,
              "Wrong structure size returns BUFFER_TOO_SMALL without writes");
        ctx.r3.u64 = 0x50000000U;
        __imp__MmQueryStatistics(ctx, memory.base());
        check(ctx.r3.u32 == dotm::kNtInvalidParameter,
              "Uncommitted guest output rejected");
        ctx.r3.u64 = dotm::GuestMemory::kFunctionTableOffset;
        __imp__MmQueryStatistics(ctx, memory.base());
        check(ctx.r3.u32 == dotm::kNtInvalidParameter,
              "Native PPC dispatch table protected");
        ctx.r3.u64 = 3; ctx.r4.u64 = 10; ctx.r5.u64 = output + 0x100;
        ctx.r6.u64 = 4; ctx.r7.u64 = output + 0x110;
        __imp__ExGetXConfigSetting(ctx, memory.base());
        check(ctx.r3.u32 == 0 && get32(memory, output + 0x100) == 0x00040000U &&
              memory.address(output + 0x110)[0] == 0 &&
              memory.address(output + 0x110)[1] == 4,
              "XCONFIG user video flags and required-size endian");
        ctx.r3.u64 = 2; ctx.r4.u64 = 2;
        __imp__ExGetXConfigSetting(ctx, memory.base());
        check(ctx.r3.u32 == 0 && get32(memory, output + 0x100) == 0x1000U,
              "XCONFIG retail North American AV region");
        ctx.r3.u64 = 3; ctx.r4.u64 = 14; ctx.r6.u64 = 1;
        __imp__ExGetXConfigSetting(ctx, memory.base());
        check(ctx.r3.u32 == 0 && memory.address(output + 0x100)[0] == 103 &&
              memory.address(output + 0x110)[1] == 1,
              "XCONFIG user country byte");
        ctx.r3.u64 = 3; ctx.r4.u64 = 9; ctx.r6.u64 = 2;
        __imp__ExGetXConfigSetting(ctx, memory.base());
        check(ctx.r3.u32 == 0xC0000023U && memory.address(output + 0x110)[1] == 4,
              "XCONFIG short buffer returns size and NTSTATUS");
        ctx.r3.u64 = 99; ctx.r4.u64 = 9; ctx.r6.u64 = 4;
        __imp__ExGetXConfigSetting(ctx, memory.base());
        check(ctx.r3.u32 == 0xC00000EFU, "XCONFIG unknown category rejected");
        ctx.r3.u64 = 3; ctx.r4.u64 = 10;
        ctx.r7.u64 = dotm::GuestMemory::kFunctionTableOffset;
        __imp__ExGetXConfigSetting(ctx, memory.base());
        check(ctx.r3.u32 == 0xC00000F1U,
              "XCONFIG rejects host dispatch table output pointer");
        ctx.r3.u64 = output + 0x200;
        __imp__XGetVideoMode(ctx, memory.base());
        check(get32(memory, output + 0x200) == 1280 &&
              get32(memory, output + 0x204) == 720 &&
              get32(memory, output + 0x20C) == 1 &&
              get32(memory, output + 0x210) == 1 &&
              get32(memory, output + 0x214) == 0x42700000U,
              "Xbox 360 XGetVideoMode 720p 60 Hz big-endian layout");
        ctx.r3.u64 = output + 0x240;
        __imp__VdQueryVideoMode(ctx, memory.base());
        check(get32(memory, output + 0x240) == 1280 &&
              get32(memory, output + 0x244) == 720,
              "VdQueryVideoMode uses the same Xbox output profile");
        ctx.r3.u32 = 0xFFFFFFFFU;
        __imp__VdQueryVideoFlags(ctx, memory.base());
        check(ctx.r3.u32 == 3U,
              "VdQueryVideoFlags matches the actual 1280x720 widescreen profile");
        dotm::bind_kernel_guest_memory(nullptr, nullptr);
        ctx.r3.u64 = output;
        __imp__MmQueryStatistics(ctx, memory.base());
        check(ctx.r3.u32 == dotm::kNtNotSupported, "Unbound runtime fails closed");
        std::cout << "Xbox 360 memory statistics PPC ABI PASS\n";
        return 0;
    } catch (const std::exception& e) {
        dotm::bind_kernel_guest_memory(nullptr, nullptr);
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
