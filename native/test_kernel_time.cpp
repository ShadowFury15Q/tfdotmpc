#include "ppc_recomp_shared.h"
#include "kernel_guest_memory.hpp"
#include <cstdint>
#include <cstdio>
#include <stdexcept>
PPC_EXTERN_FUNC(__imp__RtlTimeToTimeFields);
PPC_EXTERN_FUNC(__imp__RtlTimeFieldsToTime);
namespace {
void check(bool value, const char* what) {
    if (!value) throw std::runtime_error(what);
}
void put16(dotm::GuestMemory& m, std::uint32_t a, std::uint16_t v) {
    m.address(a)[0] = static_cast<std::uint8_t>(v >> 8);
    m.address(a)[1] = static_cast<std::uint8_t>(v);
}
std::uint16_t get16(dotm::GuestMemory& m, std::uint32_t a) {
    const auto* p = m.address(a);
    return static_cast<std::uint16_t>((std::uint16_t(p[0]) << 8) | p[1]);
}
void put64(dotm::GuestMemory& m, std::uint32_t a, std::uint64_t v) {
    for (int i = 0; i < 8; ++i)
        m.address(a)[i] = static_cast<std::uint8_t>(v >> (56 - 8 * i));
}
std::uint64_t get64(dotm::GuestMemory& m, std::uint32_t a) {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | m.address(a)[i];
    return v;
}
}
int main() {
    try {
        dotm::GuestMemory memory;
        dotm::GuestVirtualAllocator allocator(memory, 0x40000000U, 0x40010000U);
        constexpr std::uint32_t time = 0x82050000U;
        constexpr std::uint32_t fields = time + 32U;
        memory.commit(time, 4096);
        dotm::bind_kernel_guest_memory(&memory, &allocator);
        PPCContext ctx{};
        auto to_fields = [&](std::uint64_t ticks) {
            put64(memory, time, ticks);
            ctx.r3.u64 = time; ctx.r4.u64 = fields;
            __imp__RtlTimeToTimeFields(ctx, memory.base());
            check(ctx.r3.u32 == time, "Void time-to-fields must preserve PPC r3");
        };
        auto to_time = [&]() {
            ctx.r3.u64 = fields; ctx.r4.u64 = time;
            __imp__RtlTimeFieldsToTime(ctx, memory.base());
            return ctx.r3.u32;
        };
        to_fields(0);
        check(get16(memory, fields) == 1601 &&
              get16(memory, fields + 2) == 1 &&
              get16(memory, fields + 4) == 1 &&
              get16(memory, fields + 14) == 1,
              "1601 NT epoch is Monday");
        check(to_time() == 1 && get64(memory, time) == 0,
              "1601 epoch round-trip");
        to_fields(116444736000000000ULL);
        check(get16(memory, fields) == 1970 &&
              get16(memory, fields + 2) == 1 &&
              get16(memory, fields + 4) == 1 &&
              get16(memory, fields + 14) == 4,
              "Unix epoch is Thursday");
        to_fields(125963012967890000ULL);
        check(get16(memory, fields) == 2000 &&
              get16(memory, fields + 2) == 2 &&
              get16(memory, fields + 4) == 29 &&
              get16(memory, fields + 6) == 12 &&
              get16(memory, fields + 8) == 34 &&
              get16(memory, fields + 10) == 56 &&
              get16(memory, fields + 12) == 789 &&
              get16(memory, fields + 14) == 2,
              "2000 leap-day and subsecond conversion");
        check(to_time() == 1 && get64(memory, time) == 125963012967890000ULL,
              "2000 leap-day round-trip");
        to_fields(133537247999990000ULL);
        check(get16(memory, fields) == 2024 &&
              get16(memory, fields + 2) == 2 &&
              get16(memory, fields + 4) == 29 &&
              get16(memory, fields + 6) == 23 &&
              get16(memory, fields + 8) == 59 &&
              get16(memory, fields + 10) == 59 &&
              get16(memory, fields + 12) == 999 &&
              get16(memory, fields + 14) == 4,
              "2024 leap day 23:59:59.999");
        check(to_time() == 1 && get64(memory, time) == 133537247999990000ULL,
              "2024 leap-day round-trip");
        to_fields(157520160000000000ULL);
        check(get16(memory, fields) == 2100 &&
              get16(memory, fields + 2) == 3 &&
              get16(memory, fields + 4) == 1 &&
              get16(memory, fields + 14) == 1,
              "2100 is not a leap year");
        put16(memory, fields + 2, 2);
        put16(memory, fields + 4, 29);
        put64(memory, time, 0x123456789ABCDEF0ULL);
        check(to_time() == 0 && get64(memory, time) == 0x123456789ABCDEF0ULL,
              "Reject February 29 in 2100 without overwriting output");
        put16(memory, fields, 2000);
        check(to_time() == 1, "Allow February 29 in 2000");
        put16(memory, fields + 6, 24);
        put64(memory, time, 0x123456789ABCDEF0ULL);
        check(to_time() == 0 && get64(memory, time) == 0x123456789ABCDEF0ULL,
              "Reject invalid hour without overwriting output");
        bool rejected = false;
        try {
            ctx.r3.u64 = 0x50000000U; ctx.r4.u64 = fields;
            __imp__RtlTimeToTimeFields(ctx, memory.base());
        } catch (const std::runtime_error&) { rejected = true; }
        check(rejected, "Reject uncommitted guest time pointer");
        dotm::bind_kernel_guest_memory(nullptr, nullptr);
        std::puts("Xbox 360 NT calendar PPC ABI tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "NT time conversion test failed: %s\n", error.what());
        return 1;
    }
}
