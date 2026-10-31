#include "guest_memory.hpp"
#include "ppc_recomp_shared.h"
#include "xam_launch_data.hpp"
#include <array>
#include <cstdint>
#include <stdexcept>
namespace {
void check(bool ok) {
    if (!ok) throw std::runtime_error("XAM launch-data PPC ABI regression");
}
}
int main() {
    dotm::GuestMemory memory;
    constexpr std::uint32_t page = 0x71200000;
    memory.commit(page, 4096);
    dotm::XamLaunchDataHost host;
    dotm::bind_xam_launch_data(&host, &memory);
    PPCContext context{};
    context.r3.u32 = page;
    __imp__XamLoaderGetLaunchDataSize(context, memory.base());
    check(context.r3.u64 == 1168);
    check(memory.address(page)[0] == 0 && memory.address(page + 3)[0] == 0);
    context.r3.u32 = page + 512;
    context.r4.u32 = 4;
    __imp__XamLoaderGetLaunchData(context, memory.base());
    check(context.r3.u64 == 1168);
    const std::array<std::uint8_t, 5> payload{0x11, 0x00, 0xBB, 0xCC, 0xDD};
    for (std::size_t i = 0; i < payload.size(); ++i)
        memory.address(page + 64)[i] = payload[i];
    context.r3.u32 = page + 64;
    context.r4.u32 = static_cast<std::uint32_t>(payload.size());
    __imp__XamLoaderSetLaunchData(context, memory.base());
    check(context.r3.u64 == 0);
    context.r3.u32 = page;
    __imp__XamLoaderGetLaunchDataSize(context, memory.base());
    check(context.r3.u64 == 0);
    check(memory.address(page)[0] == 0 && memory.address(page + 3)[0] == 5);
    context.r3.u32 = page + 512;
    context.r4.u32 = 3;
    __imp__XamLoaderGetLaunchData(context, memory.base());
    check(context.r3.u64 == 0);
    check(memory.address(page + 512)[0] == 0x11);
    check(memory.address(page + 513)[0] == 0x00);
    check(memory.address(page + 514)[0] == 0xBB);
    check(memory.address(page + 515)[0] == 0);
    context.r3.u32 = page + 100;
    context.r4.u32 = 5;
    __imp__XamLoaderGetLaunchData(context, memory.base());
    check(context.r3.u64 == 0);
    for (std::size_t i = 0; i < payload.size(); ++i)
        check(memory.address(page + 100)[i] == payload[i]);
    context.r3.u32 = page + 4096;
    __imp__XamLoaderGetLaunchDataSize(context, memory.base());
    check(context.r3.u64 == 87);
    context.r3.u32 = page + 4096;
    context.r4.u32 = 5;
    __imp__XamLoaderSetLaunchData(context, memory.base());
    check(context.r3.u64 == 87);
    context.r3.u32 = page + 64;
    context.r4.u32 = 1024U * 1024U + 1U;
    __imp__XamLoaderSetLaunchData(context, memory.base());
    check(context.r3.u64 == 87);
    context.r3.u32 = 0;
    context.r4.u32 = 0;
    __imp__XamLoaderSetLaunchData(context, memory.base());
    check(context.r3.u64 == 0);
    context.r3.u32 = page;
    __imp__XamLoaderGetLaunchDataSize(context, memory.base());
    check(context.r3.u64 == 1168 && memory.address(page + 3)[0] == 0);
    dotm::bind_xam_launch_data(nullptr, nullptr);
    return 0;
}
