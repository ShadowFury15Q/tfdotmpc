#include "guest_memory.hpp"
#include "xnet_startup.hpp"
#include "ppc_recomp_shared.h"
#include <cstdint>
#include <iostream>
#include <stdexcept>

PPC_EXTERN_FUNC(__imp__NetDll_XNetStartup);
PPC_EXTERN_FUNC(__imp__NetDll_XNetCleanup);
PPC_EXTERN_FUNC(__imp__NetDll_WSAStartup);
PPC_EXTERN_FUNC(__imp__NetDll_WSACleanup);
namespace {
void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
}
int main() {
    constexpr std::uint32_t ptr = 0x82035000U;
    dotm::GuestMemory memory;
    memory.commit(ptr, 4096);
    dotm::XNetStartupHost host;
    dotm::bind_xnet_startup(&host, &memory);
    auto* params = memory.address(ptr);
    params[0] = static_cast<std::uint8_t>(dotm::kXNetStartupParamsBytes);
    params[1] = 1;
    params[2] = 8;
    params[3] = 4;
    PPCContext ctx{};
    ctx.r3.u64 = 1;
    ctx.r4.u64 = ptr;
    __imp__NetDll_XNetStartup(ctx, memory.base());
    require(ctx.r3.u32 == dotm::kXNetSuccess && host.reference_count() == 1,
            "PPC XNetStartup status and reference count");
    require(host.configuration()[0] == 13 && host.configuration()[1] == 1 &&
            host.configuration()[2] == 8 && host.configuration()[3] == 4,
            "13-byte original Xbox XNetStartupParams preserved");
    ctx.r3.u64 = 1;
    ctx.r4.u64 = ptr;
    __imp__NetDll_XNetStartup(ctx, memory.base());
    require(ctx.r3.u32 == dotm::kXNetSuccess && host.reference_count() == 2,
            "Repeated XNetStartup increments reference count");
    params[0] = 12;
    ctx.r3.u64 = 1;
    ctx.r4.u64 = ptr;
    __imp__NetDll_XNetStartup(ctx, memory.base());
    require(ctx.r3.u32 == dotm::kXNetInvalidArgument && host.reference_count() == 2,
            "Malformed params rejected without mutation");
    params[0] = 13;
    for (std::uint32_t bad : {0x22000000U, 0x82035FF8U,
             static_cast<std::uint32_t>(dotm::GuestMemory::kFunctionTableOffset)}) {
        ctx.r3.u64 = 1;
        ctx.r4.u64 = bad;
        __imp__NetDll_XNetStartup(ctx, memory.base());
        require(ctx.r3.u32 == dotm::kXNetInvalidArgument && host.reference_count() == 2,
                "Uncommitted or protected guest params rejected");
    }
    ctx.r3.u64 = 1;
    __imp__NetDll_XNetCleanup(ctx, memory.base());
    require(ctx.r3.u32 == dotm::kXNetSuccess && host.reference_count() == 1,
            "First cleanup preserves second initialization");
    ctx.r3.u64 = 1;
    __imp__NetDll_XNetCleanup(ctx, memory.base());
    require(ctx.r3.u32 == dotm::kXNetSuccess && host.reference_count() == 0 &&
            host.configuration()[0] == 0, "Final cleanup clears state");
    ctx.r3.u64 = 1;
    ctx.r4.u64 = 0;
    __imp__NetDll_XNetStartup(ctx, memory.base());
    require(ctx.r3.u32 == dotm::kXNetSuccess && host.reference_count() == 1 &&
            host.configuration()[0] == 0, "Null params select Xbox defaults");
    constexpr std::uint32_t wsadata = ptr + 0x100U;
    ctx.r3.u64 = 1;
    ctx.r4.u64 = 0x0202;
    ctx.r5.u64 = wsadata;
    __imp__NetDll_WSAStartup(ctx, memory.base());
    require(ctx.r3.u32 == dotm::kXNetSuccess && host.wsa_reference_count() == 1,
            "Host Winsock 2.2 initialization failed");
    const auto* out = memory.address(wsadata);
    require(out[0] == 2 && out[1] == 2 && out[2] == 2 && out[3] == 2,
            "Xbox WSADATA versions must be big-endian 2.2");
    require(out[4] != 0 && out[396] == 0 && out[397] == 0 &&
            out[398] == 0 && out[399] == 0,
            "Xbox WSADATA description and vendor pointer");
    ctx.r3.u64 = 1;
    ctx.r4.u64 = 0x0202;
    ctx.r5.u64 = ptr + 4096 - 16;
    __imp__NetDll_WSAStartup(ctx, memory.base());
    require(ctx.r3.u32 == dotm::kXNetFault && host.wsa_reference_count() == 1,
            "WSAStartup rejects uncommitted guest WSADATA span");
    ctx.r3.u64 = 1;
    ctx.r4.u64 = 0;
    ctx.r5.u64 = wsadata;
    __imp__NetDll_WSAStartup(ctx, memory.base());
    require(ctx.r3.u32 == dotm::kXNetVersionNotSupported && host.wsa_reference_count() == 1,
            "WSAStartup rejects unsupported socket API version");
    ctx.r3.u64 = 1;
    __imp__NetDll_WSACleanup(ctx, memory.base());
    require(ctx.r3.u32 == dotm::kXNetSuccess && host.wsa_reference_count() == 0,
            "WSACleanup releases host network reference");
    ctx.r3.u64 = 1;
    __imp__NetDll_WSACleanup(ctx, memory.base());
    require(ctx.r3.u32 == dotm::kXNetNotInitialized,
            "WSACleanup rejects unmatched calls");
    dotm::bind_xnet_startup(nullptr, nullptr);
    ctx.r3.u64 = 1;
    ctx.r4.u64 = ptr;
    __imp__NetDll_XNetStartup(ctx, memory.base());
    require(ctx.r3.u32 == dotm::kXNetNotInitialized && host.reference_count() == 1,
            "Unbound startup fails without changing state");
    std::cout << "Original NetDll XNetStartup/Cleanup PPC ABI PASS\n";
}
