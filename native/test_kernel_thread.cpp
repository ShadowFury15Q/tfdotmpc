#include "guest_memory.hpp"
#include "kernel_thread.hpp"
#include "kernel_mutant.hpp"
#include "ppc_recomp_shared.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <stdexcept>
PPC_EXTERN_FUNC(__imp__ExCreateThread);
PPC_EXTERN_FUNC(__imp__NtResumeThread);
PPC_EXTERN_FUNC(__imp__ExTerminateThread);
PPC_EXTERN_FUNC(__imp__NtWaitForSingleObjectEx);
PPC_EXTERN_FUNC(__imp__NtClose);
namespace {
std::atomic<unsigned> calls{0};
void check(bool condition) { if (!condition) throw std::runtime_error("Xbox guest thread ABI regression"); }
void write32(dotm::GuestMemory& m, std::uint32_t a, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) m.address(a)[i] = std::uint8_t(v >> (24 - 8 * i));
}
std::uint32_t read32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 |
           std::uint32_t(p[2]) << 8 | p[3];
}
constexpr std::uint32_t kStartup = 0x82501000U;
constexpr std::uint32_t kEntry = 0x82501100U;
PPC_FUNC(test_thread_entry) {
    check(ctx.r13.u32 >= 0x78000000U && ctx.r1.u32 >= 0x72000000U);
    check(ctx.r3.u32 == 0x12345678U);
    const auto kthread = read32(base + ctx.r13.u32 + 0x100U);
    check(read32(base + kthread + 0x14CU) == 2U);
    calls.fetch_add(1);
    ctx.r3.u64 = 0x55;
}
PPC_FUNC(test_thread_startup) {
    check(ctx.r3.u32 == kEntry && ctx.r4.u32 == 0x12345678U);
    ctx.r3.u64 = ctx.r4.u32;
    test_thread_entry(ctx, base);
    __imp__ExTerminateThread(ctx, base);
}
void create(PPCContext& c, dotm::GuestMemory& m, std::uint32_t flags) {
    c.r3.u64 = 0x70100000U;
    c.r4.u64 = 0x20000U;
    c.r5.u64 = 0x70100004U;
    c.r6.u64 = kStartup;
    c.r7.u64 = kEntry;
    c.r8.u64 = 0x12345678U;
    c.r9.u64 = flags;
    __imp__ExCreateThread(c, m.base());
}
}
int main() {
    dotm::GuestMemory memory;
    memory.commit(0x70100000U, 4096);
    memory.initialize_function_table(test_thread_entry);
    const std::array<dotm::FunctionEntry, 2> mapping{{
        {kStartup, test_thread_startup}, {kEntry, test_thread_entry}}};
    memory.install_functions(mapping);
    dotm::bind_kernel_threads(&memory);
    dotm::bind_kernel_mutants(&memory);
    PPCContext c{};
    create(c, memory, 0x01000000U);
    check(c.r3.u32 == 0xC00000BBU);
    create(c, memory, 1U); // suspended
    check(c.r3.u32 == 0);
    const auto handle = read32(memory.address(0x70100000U));
    check(handle == 0xF7000000U);
    check(read32(memory.address(0x70100004U)) == 2U);
    check(calls.load() == 0);
    // Zero relative timeout: the suspended guest thread cannot have run.
    c.r3.u64 = handle; c.r4.u64 = 1; c.r5.u64 = 0; c.r6.u64 = 0x70100008U;
    __imp__NtWaitForSingleObjectEx(c, memory.base());
    check(c.r3.u32 == 0x102U);
    c.r3.u64 = handle; c.r4.u64 = 0x70100020U;
    __imp__NtResumeThread(c, memory.base());
    check(c.r3.u32 == 0 && read32(memory.address(0x70100020U)) == 1);
    c.r3.u64 = handle; c.r4.u64 = 1; c.r5.u64 = 0; c.r6.u64 = 0;
    __imp__NtWaitForSingleObjectEx(c, memory.base());
    check(c.r3.u32 == 0 && calls.load() == 1);
    c.r3.u64 = handle;
    __imp__NtClose(c, memory.base());
    check(c.r3.u32 == 0);
    c.r3.u64 = handle;
    __imp__NtClose(c, memory.base());
    check(c.r3.u32 == 0xC0000008U);
    dotm::bind_kernel_mutants(nullptr);
    dotm::bind_kernel_threads(nullptr);
}
