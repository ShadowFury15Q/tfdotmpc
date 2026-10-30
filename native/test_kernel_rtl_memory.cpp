#include "ppc_recomp_shared.h"
#include "guest_memory.hpp"
#include "kernel_rtl_memory.hpp"
#include <cstdint>
#include <iostream>
#include <utility>
#include <stdexcept>

PPC_EXTERN_FUNC(__imp__RtlFillMemoryUlong);
PPC_EXTERN_FUNC(__imp__RtlCompareMemoryUlong);
PPC_EXTERN_FUNC(__imp__RtlNtStatusToDosError);

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    try {
        dotm::GuestMemory memory;
        constexpr std::uint32_t address = 0x40000000U;
        memory.commit(address, 4096);
        dotm::bind_kernel_rtl_memory(&memory);
        PPCContext ctx{};
        ctx.r3.u64 = address;
        ctx.r4.u64 = 14;
        ctx.r5.u64 = 0x12345678U;
        __imp__RtlFillMemoryUlong(ctx, memory.base());
        check(ctx.r3.u32 == address, "Void fill must preserve r3");
        const auto* data = memory.address(address);
        for (unsigned i = 0; i < 12; ++i)
            check(data[i] == static_cast<std::uint8_t>(0x12345678U >> (24 - (i % 4) * 8)),
                  "Big-endian DWORD fill");
        check(data[12] == 0 && data[13] == 0, "Trailing bytes must be untouched");

        ctx.r3.u64 = address;
        ctx.r4.u64 = 12;
        __imp__RtlCompareMemoryUlong(ctx, memory.base());
        check(ctx.r3.u32 == 12, "Matching DWORD count");
        memory.address(address + 5)[0] = 0;
        ctx.r3.u64 = address;
        __imp__RtlCompareMemoryUlong(ctx, memory.base());
        check(ctx.r3.u32 == 4, "Stop at first mismatching DWORD");
        ctx.r3.u64 = address + 1;
        __imp__RtlCompareMemoryUlong(ctx, memory.base());
        check(ctx.r3.u32 == 0, "Unaligned comparison returns zero");
        ctx.r3.u64 = address;
        ctx.r4.u64 = 11;
        __imp__RtlCompareMemoryUlong(ctx, memory.base());
        check(ctx.r3.u32 == 0, "Non-DWORD length returns zero");

        for (const auto [status, expected] : {
                std::pair<std::uint32_t, std::uint32_t>{0U, 0U},
                {0xC000000DU, 87U}, {0xC000000FU, 2U},
                {0xC0000262U, 182U}, {0xC0000263U, 127U},
                {0xC00000BBU, 50U}, {0xDEADBEEFU, 317U}}) {
            ctx.r3.u64 = status;
            __imp__RtlNtStatusToDosError(ctx, memory.base());
            check(ctx.r3.u32 == expected, "NTSTATUS to DOS mapping PPC ABI");
        }
        bool rejected = false;
        try { dotm::rtl_bound_fill_memory_ulong(0x50000000U, 4, 1); }
        catch (const std::runtime_error&) { rejected = true; }
        check(rejected, "Uncommitted guest write must be rejected");
        rejected = false;
        try { (void)dotm::rtl_bound_compare_memory_ulong(0x50000000U, 4, 1); }
        catch (const std::runtime_error&) { rejected = true; }
        check(rejected, "Uncommitted guest read must be rejected");
        dotm::bind_kernel_rtl_memory(nullptr);
        rejected = false;
        try { dotm::rtl_bound_fill_memory_ulong(address, 4, 1); }
        catch (const std::runtime_error&) { rejected = true; }
        check(rejected, "Unbound kernel must not access guest memory");
        std::cout << "Xbox 360 RTL ULONG memory PPC ABI PASS\n";
        return 0;
    } catch (const std::exception& e) {
        dotm::bind_kernel_rtl_memory(nullptr);
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
