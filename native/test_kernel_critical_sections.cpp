#include "guest_memory.hpp"
#include "kernel_critical_sections.hpp"
#include "ppc_recomp_shared.h"
#include <atomic>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <thread>
PPC_EXTERN_FUNC(__imp__KeEnterCriticalRegion);
PPC_EXTERN_FUNC(__imp__KeLeaveCriticalRegion);
namespace {
void check(bool b) { if (!b) throw std::runtime_error("critical-section ABI failure"); }
std::uint32_t read32(const dotm::GuestMemory& m, std::uint32_t a) {
    const auto* p = m.address(a);
    return std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 |
           std::uint32_t(p[2]) << 8 | std::uint32_t(p[3]);
}
}
int main() {
    dotm::GuestMemory memory;
    constexpr std::uint32_t address = 0x70102000;
    memory.commit(address, 4096);
    dotm::bind_kernel_critical_sections(&memory);
    PPCContext c{};
    c.r3.u32 = 0xABCD1234U;
    __imp__KeEnterCriticalRegion(c, memory.base());
    __imp__KeEnterCriticalRegion(c, memory.base());
    check(c.r3.u32 == 0xABCD1234U && dotm::kernel_critical_region_depth() == 2U);
    std::atomic<bool> isolated{false};
    std::thread region_worker([&] {
        check(dotm::kernel_critical_region_depth() == 0U);
        dotm::kernel_enter_critical_region();
        check(dotm::kernel_critical_region_depth() == 1U);
        dotm::kernel_leave_critical_region();
        isolated = true;
    });
    region_worker.join();
    check(isolated && dotm::kernel_critical_region_depth() == 2U);
    __imp__KeLeaveCriticalRegion(c, memory.base());
    __imp__KeLeaveCriticalRegion(c, memory.base());
    check(c.r3.u32 == 0xABCD1234U && dotm::kernel_critical_region_depth() == 0U);
    bool region_underflow = false;
    try { __imp__KeLeaveCriticalRegion(c, memory.base()); }
    catch (const std::runtime_error&) { region_underflow = true; }
    check(region_underflow);

    c.r3.u32 = address;
    c.r4.u32 = 300;
    __imp__RtlInitializeCriticalSectionAndSpinCount(c, memory.base());
    check(c.r3.u64 == 0);
    check(memory.address(address)[0] == 1 && memory.address(address)[1] == 2);
    check(read32(memory, address + 16) == 0xffffffffU);
    c.r3.u32 = address;
    __imp__RtlEnterCriticalSection(c, memory.base());
    check(read32(memory, address + 16) == 0 && read32(memory, address + 20) == 1);
    c.r3.u32 = address;
    __imp__RtlTryEnterCriticalSection(c, memory.base());
    check(c.r3.u64 == 1 && read32(memory, address + 20) == 2);
    std::atomic<bool> other_entered{false};
    std::thread other([&] {
        check(!dotm::kernel_try_enter_critical_section(address));
        other_entered = true;
    });
    other.join();
    check(other_entered);
    c.r3.u32 = address;
    __imp__RtlLeaveCriticalSection(c, memory.base());
    check(read32(memory, address + 20) == 1);
    c.r3.u32 = address;
    __imp__RtlLeaveCriticalSection(c, memory.base());
    check(read32(memory, address + 16) == 0xffffffffU);
    std::thread acquired([&] {
        dotm::kernel_enter_critical_section(address);
        check(read32(memory, address + 20) == 1);
        dotm::kernel_leave_critical_section(address);
    });
    acquired.join();
    bool rejected = false;
    try { dotm::kernel_leave_critical_section(address); }
    catch (const std::runtime_error&) { rejected = true; }
    check(rejected);
    rejected = false;
    try { dotm::kernel_initialize_critical_section(address + 4096, 0); }
    catch (const std::runtime_error&) { rejected = true; }
    check(rejected);
    dotm::bind_kernel_critical_sections(nullptr);
    return 0;
}
