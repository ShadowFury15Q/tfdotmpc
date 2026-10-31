#include "guest_memory.hpp"
#include "kernel_thread.hpp"
#include "kernel_mutant.hpp"
#include "ppc_recomp_shared.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <thread>
PPC_EXTERN_FUNC(__imp__ExCreateThread);
PPC_EXTERN_FUNC(__imp__NtResumeThread);
PPC_EXTERN_FUNC(__imp__KeDelayExecutionThread);
PPC_EXTERN_FUNC(__imp__ExTerminateThread);
PPC_EXTERN_FUNC(__imp__NtWaitForSingleObjectEx);
PPC_EXTERN_FUNC(__imp__NtClose);
PPC_EXTERN_FUNC(__imp__ObReferenceObjectByHandle);
PPC_EXTERN_FUNC(__imp__ObDereferenceObject);
PPC_EXTERN_FUNC(__imp__KeSetAffinityThread);
PPC_EXTERN_FUNC(__imp__KeQueryBasePriorityThread);
PPC_EXTERN_FUNC(__imp__KeSetBasePriorityThread);
namespace {
std::atomic<unsigned> calls{0};
std::atomic<bool> delay_entered{false};
std::atomic<bool> wait_entered{false};
std::atomic<std::uint32_t> blocked_handle{0};
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
constexpr std::uint32_t kDelayEntry = 0x82501200U;
constexpr std::uint32_t kWaitEntry = 0x82501300U;
PPC_FUNC(test_thread_entry) {
    check(ctx.r13.u32 >= 0x78000000U && ctx.r1.u32 >= 0x72000000U);
    check(ctx.r3.u32 == 0x12345678U);
    const auto kthread = read32(base + ctx.r13.u32 + 0x100U);
    check(read32(base + kthread + 0x14CU) == 2U);
    calls.fetch_add(1);
    ctx.r3.u64 = 0x55;
}
PPC_FUNC(test_thread_wait_entry) {
    wait_entered.store(true, std::memory_order_release);
    ctx.r3.u64 = blocked_handle.load(std::memory_order_acquire);
    ctx.r4.u64 = 1;
    ctx.r5.u64 = 0;
    ctx.r6.u64 = 0;
    __imp__NtWaitForSingleObjectEx(ctx, base);
}
PPC_FUNC(test_thread_delay_entry) {
    delay_entered.store(true, std::memory_order_release);
    ctx.r3.u64 = 1;
    ctx.r4.u64 = 0;
    ctx.r5.u64 = 0x70100040U;
    __imp__KeDelayExecutionThread(ctx, base);
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
    const std::array<dotm::FunctionEntry, 4> mapping{{
        {kStartup, test_thread_startup}, {kEntry, test_thread_entry},
        {kDelayEntry, test_thread_delay_entry}, {kWaitEntry, test_thread_wait_entry}}};
    memory.install_functions(mapping);
    dotm::bind_kernel_threads(&memory);
    dotm::bind_kernel_mutants(&memory);
    PPCContext c{};
    create(c, memory, 0x03000000U); // two hardware CPUs is not a valid selector
    check(c.r3.u32 == 0xC000000DU);
    create(c, memory, 2U); // unsupported creation flag
    check(c.r3.u32 == 0xC00000BBU);
    create(c, memory, 0x02000001U); // Xbox CPU 1, initially suspended
    check(c.r3.u32 == 0);
    const auto handle = read32(memory.address(0x70100000U));
    check(handle == 0xF7000000U);
    check(read32(memory.address(0x70100004U)) == 2U);
    check(calls.load() == 0);
    // Zero relative timeout: the suspended guest thread cannot have run.
    c.r3.u64 = handle; c.r4.u64 = 1; c.r5.u64 = 0; c.r6.u64 = 0x70100008U;
    __imp__NtWaitForSingleObjectEx(c, memory.base());
    check(c.r3.u32 == 0x102U);
    // Original CRT calls ObReferenceObjectByHandle(handle, ExThreadObjectType,
    // &thread) and must receive a live KTHREAD, not an opaque host pointer.
    c.r3.u64 = handle; c.r4.u64 = 0x81908000U; c.r5.u64 = 0x70100070U;
    __imp__ObReferenceObjectByHandle(c, memory.base());
    check(c.r3.u32 == 0U && read32(memory.address(0x70100070U)) == 0x78001000U);
    c.r3.u64 = handle; c.r4.u64 = 0x81907000U; c.r5.u64 = 0x70100070U;
    __imp__ObReferenceObjectByHandle(c, memory.base());
    check(c.r3.u32 == 0xC0000024U);
    c.r3.u64 = 0x78001000U; c.r4.u64 = 0x00000002U;
    c.r5.u64 = 0x70100074U;
    __imp__KeSetAffinityThread(c, memory.base());
    check(c.r3.u32 == 0U && read32(memory.address(0x70100074U)) == 2U);
    c.r3.u64 = 0x78001000U; c.r4.u64 = 3U; c.r5.u64 = 0;
    __imp__KeSetAffinityThread(c, memory.base());
    check(c.r3.u32 == 0xC000000DU);
    c.r3.u64 = 0x78001000U; c.r4.u64 = 12U;
    __imp__KeSetBasePriorityThread(c, memory.base());
    check(c.r3.u32 == 0U);
    c.r3.u64 = 0x78001000U;
    __imp__KeQueryBasePriorityThread(c, memory.base());
    check(c.r3.u32 == 12U);
    c.r3.u64 = 0x78001000U; c.r4.u64 = 17U;
    __imp__KeSetBasePriorityThread(c, memory.base());
    check(c.r3.u32 == 12U);
    c.r3.u64 = 0x78001000U;
    __imp__KeQueryBasePriorityThread(c, memory.base());
    check(c.r3.u32 == 17U);
    c.r3.u64 = 0x78001000U; c.r4.u64 = 32U;
    bool rejected_priority = false;
    try { __imp__KeSetBasePriorityThread(c, memory.base()); }
    catch (const std::runtime_error&) { rejected_priority = true; }
    check(rejected_priority);
    c.r3.u64 = 0x78001000U;
    __imp__ObDereferenceObject(c, memory.base());
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
    // A suspended worker must not execute its entrypoint, and teardown must
    // wake and join it rather than deadlock on its initial suspend gate.
    create(c, memory, 1U);
    check(c.r3.u32 == 0);
    blocked_handle.store(read32(memory.address(0x70100000U)), std::memory_order_release);
    // Original CRT sub_8351EE58 calls this kernel delay with a signed
    // big-endian relative timeout. Verify the PPC register ABI directly.
    write32(memory, 0x70100030U, 0xFFFFFFFFU);
    write32(memory, 0x70100034U, 0xFFFFB1E0U); // -20000 x 100 ns = 2 ms
    c.r3.u64 = 1; c.r4.u64 = 0; c.r5.u64 = 0x70100030U;
    __imp__KeDelayExecutionThread(c, memory.base());
    check(c.r3.u32 == 0);
    write32(memory, 0x70100030U, 0);
    write32(memory, 0x70100034U, 0); // expired absolute timestamp
    c.r3.u64 = 0; c.r4.u64 = 1; c.r5.u64 = 0x70100030U;
    __imp__KeDelayExecutionThread(c, memory.base());
    check(c.r3.u32 == 0);
    c.r3.u64 = 2; c.r4.u64 = 0; c.r5.u64 = 0x70100030U;
    __imp__KeDelayExecutionThread(c, memory.base());
    check(c.r3.u32 == 0xC000000DU);
    c.r3.u64 = 1; c.r4.u64 = 2; c.r5.u64 = 0x70100030U;
    __imp__KeDelayExecutionThread(c, memory.base());
    check(c.r3.u32 == 0xC000000DU);
    c.r3.u64 = 1; c.r4.u64 = 0; c.r5.u64 = 0x70100031U;
    __imp__KeDelayExecutionThread(c, memory.base());
    check(c.r3.u32 == 0xC000000DU);
    // A guest thread blocked in a one-hour kernel delay must not prevent
    // shutdown after the original PPC entry fails.
    write32(memory, 0x70100040U, 0xFFFFFFF7U);
    write32(memory, 0x70100044U, 0x9E3B9800U); // -1 hour, NT 100 ns units
    c.r3.u64 = 0x70100050U;
    c.r4.u64 = 0x20000U;
    c.r5.u64 = 0x70100054U;
    c.r6.u64 = 0;
    c.r7.u64 = kDelayEntry;
    c.r8.u64 = 0;
    c.r9.u64 = 0;
    __imp__ExCreateThread(c, memory.base());
    check(c.r3.u32 == 0);
    // A second worker waits forever on the suspended thread handle.
    // Both the delay and the thread-handle wait must be cancelled on unbind.
    c.r3.u64 = 0x70100058U;
    c.r4.u64 = 0x20000U;
    c.r5.u64 = 0x7010005CU;
    c.r6.u64 = 0;
    c.r7.u64 = kWaitEntry;
    c.r8.u64 = 0;
    c.r9.u64 = 0;
    __imp__ExCreateThread(c, memory.base());
    check(c.r3.u32 == 0);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!delay_entered.load(std::memory_order_acquire) ||
           !wait_entered.load(std::memory_order_acquire)) {
        check(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const auto shutdown_start = std::chrono::steady_clock::now();
    dotm::bind_kernel_mutants(nullptr);
    dotm::bind_kernel_threads(nullptr);
    check(std::chrono::steady_clock::now() - shutdown_start < std::chrono::seconds(2));
    check(calls.load() == 1);
    check(!memory.committed_range(0x72000000U, 1) &&
          !memory.committed_range(0x78000000U, 1));

    // A title can create far more than 32 threads over its lifetime even
    // though Xenon supports only a limited number of simultaneous workers.
    // Closing a finished handle must allow its stack/KPCR slot to be reused.
    dotm::bind_kernel_threads(&memory);
    for (unsigned iteration = 0; iteration < 40; ++iteration) {
        create(c, memory, 0U);
        check(c.r3.u32 == 0U);
        const auto recycled = read32(memory.address(0x70100000U));
        check(recycled == 0xF7000000U);
        c.r3.u64 = recycled; c.r4.u64 = 1; c.r5.u64 = 0; c.r6.u64 = 0;
        __imp__NtWaitForSingleObjectEx(c, memory.base());
        check(c.r3.u32 == 0U);
        c.r3.u64 = recycled;
        __imp__NtClose(c, memory.base());
        check(c.r3.u32 == 0U);
    }
    check(calls.load() == 41U);
    create(c, memory, 0U);
    check(c.r3.u32 == 0U);
    const auto referenced_handle = read32(memory.address(0x70100000U));
    c.r3.u64 = referenced_handle; c.r4.u64 = 1; c.r5.u64 = 0; c.r6.u64 = 0;
    __imp__NtWaitForSingleObjectEx(c, memory.base());
    check(c.r3.u32 == 0U);
    c.r3.u64 = referenced_handle; c.r4.u64 = 0x81908000U;
    c.r5.u64 = 0x70100070U;
    __imp__ObReferenceObjectByHandle(c, memory.base());
    check(c.r3.u32 == 0U);
    const auto referenced_object = read32(memory.address(0x70100070U));
    c.r3.u64 = referenced_handle;
    __imp__NtClose(c, memory.base());
    check(c.r3.u32 == 0U);
    // A closed handle does not release a still-referenced KTHREAD body.
    create(c, memory, 1U);
    check(c.r3.u32 == 0U &&
          read32(memory.address(0x70100000U)) == 0xF7000004U);
    c.r3.u64 = referenced_object;
    __imp__ObDereferenceObject(c, memory.base());
    dotm::bind_kernel_threads(nullptr);
    check(calls.load() == 42U);
    check(!memory.committed_range(0x72000000U, 1) &&
          !memory.committed_range(0x78000000U, 1));
}
