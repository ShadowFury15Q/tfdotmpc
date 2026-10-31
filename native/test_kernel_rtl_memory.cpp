#include "ppc_recomp_shared.h"
#include "guest_memory.hpp"
#include "kernel_rtl_memory.hpp"
#include <cstdint>
#include <iostream>
#include <utility>
#include <stdexcept>

PPC_EXTERN_FUNC(__imp__RtlFillMemoryUlong);
PPC_EXTERN_FUNC(__imp__RtlInitAnsiString);
PPC_EXTERN_FUNC(__imp__RtlInitUnicodeString);
PPC_EXTERN_FUNC(__imp__RtlCompareMemoryUlong);
PPC_EXTERN_FUNC(__imp__RtlNtStatusToDosError);
PPC_EXTERN_FUNC(__imp__RtlMultiByteToUnicodeN);
PPC_EXTERN_FUNC(__imp__RtlUnicodeToMultiByteN);

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
        // Real original PPC CRT converts Xbox ANSI bytes to big-endian UTF-16.
        const auto input = address + 0x100U;
        const auto output = address + 0x200U;
        const auto written = address + 0x300U;
        memory.address(input)[0] = 'A';
        memory.address(input)[1] = 0x80U; // CP1252 euro sign.
        memory.address(input)[2] = 0xE9U; // e acute.
        ctx.r3.u64 = output; ctx.r4.u64 = 6; ctx.r5.u64 = written;
        ctx.r6.u64 = input; ctx.r7.u64 = 3;
        __imp__RtlMultiByteToUnicodeN(ctx, memory.base());
        check(ctx.r3.u32 == 0 && memory.address(output)[0] == 0 &&
              memory.address(output)[1] == 'A' &&
              memory.address(output)[2] == 0x20 && memory.address(output)[3] == 0xAC &&
              memory.address(output)[4] == 0 && memory.address(output)[5] == 0xE9 &&
              memory.address(written)[3] == 6,
              "CP1252 to UTF-16BE PPC ABI and byte count");
        ctx.r3.u64 = input + 16; ctx.r4.u64 = 3; ctx.r5.u64 = written;
        ctx.r6.u64 = output; ctx.r7.u64 = 6;
        __imp__RtlUnicodeToMultiByteN(ctx, memory.base());
        check(ctx.r3.u32 == 0 && memory.address(input + 16)[0] == 'A' &&
              memory.address(input + 17)[0] == 0x80U &&
              memory.address(input + 18)[0] == 0xE9U &&
              memory.address(written)[3] == 3,
              "UTF-16BE to CP1252 roundtrip PPC ABI");
        ctx.r3.u64 = output; ctx.r4.u64 = 2; ctx.r5.u64 = written;
        ctx.r6.u64 = input; ctx.r7.u64 = 3;
        __imp__RtlMultiByteToUnicodeN(ctx, memory.base());
        check(ctx.r3.u32 == 0x80000005U && memory.address(written)[3] == 2,
              "Truncated conversion reports STATUS_BUFFER_OVERFLOW");
        ctx.r3.u64 = output; ctx.r4.u64 = 6; ctx.r5.u64 = written;
        ctx.r6.u64 = 0x50000000U; ctx.r7.u64 = 3;
        __imp__RtlMultiByteToUnicodeN(ctx, memory.base());
        check(ctx.r3.u32 == 0xC000000DU,
              "Invalid source range rejected without dereferencing guest memory");
        bool rejected = false;
        try { dotm::rtl_bound_fill_memory_ulong(0x50000000U, 4, 1); }
        catch (const std::runtime_error&) { rejected = true; }
        check(rejected, "Uncommitted guest write must be rejected");
        rejected = false;
        try { (void)dotm::rtl_bound_compare_memory_ulong(0x50000000U, 4, 1); }
        catch (const std::runtime_error&) { rejected = true; }
        check(rejected, "Uncommitted guest read must be rejected");
        // Xbox ANSI_STRING / UNICODE_STRING descriptors are 8-byte
        // big-endian structures with byte lengths and guest buffer pointers.
        const auto ansi = address + 0x380U;
        const auto ansi_text = address + 0x3A0U;
        const auto wide = address + 0x3C0U;
        const auto wide_text = address + 0x3E0U;
        memory.address(ansi_text)[0] = 'A';
        memory.address(ansi_text)[1] = 'B';
        memory.address(ansi_text)[2] = 0;
        ctx.r3.u64 = ansi; ctx.r4.u64 = ansi_text;
        __imp__RtlInitAnsiString(ctx, memory.base());
        check(ctx.r3.u32 == ansi && memory.address(ansi)[0] == 0 &&
              memory.address(ansi)[1] == 2 && memory.address(ansi)[3] == 3 &&
              memory.address(ansi)[4] == 0x40 && memory.address(ansi)[5] == 0 &&
              memory.address(ansi)[6] == 3 && memory.address(ansi)[7] == 0xA0,
              "RtlInitAnsiString big-endian PPC descriptor");
        ctx.r3.u64 = ansi; ctx.r4.u64 = 0;
        __imp__RtlInitAnsiString(ctx, memory.base());
        check(memory.address(ansi)[1] == 0 && memory.address(ansi)[3] == 0 &&
              memory.address(ansi)[7] == 0,
              "RtlInitAnsiString null source");
        memory.address(wide_text)[0] = 0;
        memory.address(wide_text)[1] = 'Z';
        memory.address(wide_text)[2] = 0;
        memory.address(wide_text)[3] = 0;
        ctx.r3.u64 = wide; ctx.r4.u64 = wide_text;
        __imp__RtlInitUnicodeString(ctx, memory.base());
        check(memory.address(wide)[1] == 2 && memory.address(wide)[3] == 4 &&
              memory.address(wide)[7] == 0xE0,
              "RtlInitUnicodeString UTF-16BE descriptor");
        rejected = false;
        try { dotm::rtl_bound_init_ansi_string(ansi, 0x50000000U); }
        catch (const std::runtime_error&) { rejected = true; }
        check(rejected, "RtlInitAnsiString rejects uncommitted source");
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
