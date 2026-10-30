#include "guest_memory.hpp"
#include "kernel_mutant.hpp"
#include "ppc_recomp_shared.h"
#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <thread>
PPC_EXTERN_FUNC(__imp__NtCreateMutant);
PPC_EXTERN_FUNC(__imp__NtReleaseMutant);
PPC_EXTERN_FUNC(__imp__NtWaitForSingleObjectEx);
PPC_EXTERN_FUNC(__imp__NtClose);
namespace {
void check(bool value) { if (!value) throw std::runtime_error("Xbox mutant PPC ABI failure"); }
void write32(dotm::GuestMemory& m, std::uint32_t a, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) m.address(a)[i] = std::uint8_t(v >> (24 - 8*i));
}
std::uint32_t read32(dotm::GuestMemory& m, std::uint32_t a) {
    const auto* p = m.address(a);
    return std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 |
           std::uint32_t(p[2]) << 8 | p[3];
}
void create(PPCContext& c, dotm::GuestMemory& m, std::uint32_t out,
            std::uint32_t attributes, std::uint32_t initial) {
    c.r3.u64 = out; c.r4.u64 = attributes; c.r5.u64 = initial;
    __imp__NtCreateMutant(c, m.base());
}
void release(PPCContext& c, dotm::GuestMemory& m, std::uint32_t handle,
             std::uint32_t previous) {
    c.r3.u64 = handle; c.r4.u64 = previous;
    __imp__NtReleaseMutant(c, m.base());
}
void wait(PPCContext& c, dotm::GuestMemory& m, std::uint32_t handle,
          std::uint32_t timeout = 0) {
    c.r3.u64 = handle; c.r4.u64 = 1; c.r5.u64 = 0; c.r6.u64 = timeout;
    __imp__NtWaitForSingleObjectEx(c, m.base());
}
}
int main() {
    dotm::GuestMemory m;
    constexpr std::uint32_t base = 0x70102000U;
    m.commit(base, 4096);
    dotm::bind_kernel_mutants(&m);
    PPCContext c{};
    create(c, m, base + 0x1000, 0, 0);
    check(c.r3.u32 == 0xC000000DU); // uncommitted handle output
    check(!m.committed_range(base + 0x1000, 4));
    create(c, m, base, 0, 1);
    check(c.r3.u32 == 0);
    const auto h = read32(m, base);
    check(h == 0xF8000000U);
    write32(m, base + 8, 0);
    wait(c, m, h, base + 8); // zero timeout, recursive acquisition
    check(c.r3.u32 == 0);
    release(c, m, h, base + 12);
    check(c.r3.u32 == 0 && read32(m, base + 12) == 0xFFFFFFFFU);
    release(c, m, h, base + 12);
    check(c.r3.u32 == 0 && read32(m, base + 12) == 0U);
    release(c, m, h, 0);
    check(c.r3.u32 == 0xC0000046U);
    wait(c, m, h);
    check(c.r3.u32 == 0);
    std::atomic<std::uint32_t> blocked{0};
    std::thread contender([&] {
        PPCContext other{};
        wait(other, m, h, base + 8);
        blocked.store(other.r3.u32);
        release(other, m, h, 0);
        check(other.r3.u32 == 0xC0000046U);
    });
    contender.join();
    check(blocked.load() == 0x102U);
    release(c, m, h, 0);
    check(c.r3.u32 == 0);
    // The original CRT builds OBJECT_ATTRIBUTES and ANSI_STRING in guest BE.
    constexpr std::uint32_t attr = base + 0x40, ansi = base + 0x60, name = base + 0x80;
    write32(m, attr, 0xFFFFFFFCU);
    write32(m, attr + 4, ansi);
    write32(m, attr + 8, 0x80U); // OBJ_OPENIF
    m.address(ansi)[0] = 0; m.address(ansi)[1] = 5;
    m.address(ansi)[2] = 0; m.address(ansi)[3] = 6;
    write32(m, ansi + 4, name);
    for (int i = 0; i < 5; ++i) m.address(name)[i] = "mutex"[i];
    create(c, m, base + 4, attr, 0);
    check(c.r3.u32 == 0);
    const auto named_handle = read32(m, base + 4);
    create(c, m, base + 16, attr, 1);
    check(c.r3.u32 == 0x40000000U && read32(m, base + 16) != named_handle);
    write32(m, attr + 8, 0);
    create(c, m, base + 20, attr, 0);
    check(c.r3.u32 == 0xC0000035U);
    c.r3.u64 = named_handle;
    __imp__NtClose(c, m.base());
    check(c.r3.u32 == 0);
    wait(c, m, named_handle);
    check(c.r3.u32 == 0xC0000008U);
    c.r3.u64 = read32(m, base + 16);
    __imp__NtClose(c, m.base());
    check(c.r3.u32 == 0);
    c.r3.u64 = h;
    __imp__NtClose(c, m.base());
    check(c.r3.u32 == 0);
    dotm::bind_kernel_mutants(nullptr);
    return 0;
}
