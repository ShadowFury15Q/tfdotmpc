#include "ppc_recomp_shared.h"
#include "kernel_title_termination.hpp"
#include <cstdint>
#include <array>
#include <vector>
#include <iostream>
#include <stdexcept>

PPC_EXTERN_FUNC(__imp__ExRegisterTitleTerminateNotification);
PPC_EXTERN_FUNC(__imp__XamLoaderTerminateTitle);
PPC_EXTERN_FUNC(__imp__HalReturnToFirmware);
namespace {
std::vector<int> callback_order;
void first_callback(PPCContext&, std::uint8_t*) { callback_order.push_back(1); }
void second_callback(PPCContext&, std::uint8_t*) { callback_order.push_back(2); }
void unexpected_callback(PPCContext&, std::uint8_t*) {
    throw std::runtime_error("Unmapped termination callback invoked");
}
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void put32(dotm::GuestMemory& mem, std::uint32_t addr, std::uint32_t value) {
    auto* p = mem.address(addr);
    p[0] = static_cast<std::uint8_t>(value >> 24);
    p[1] = static_cast<std::uint8_t>(value >> 16);
    p[2] = static_cast<std::uint8_t>(value >> 8);
    p[3] = static_cast<std::uint8_t>(value);
}
}
int main() {
    try {
        dotm::GuestMemory mem;
        constexpr std::uint32_t image = 0x82010000U;
        mem.commit(image, 4096);
        dotm::bind_title_termination(&mem);
        PPCContext ctx{};
        put32(mem, image + 0x20, image + 0x100);
        put32(mem, image + 0x24, 5);
        put32(mem, image + 0x40, image + 0x104);
        put32(mem, image + 0x44, 2);
        ctx.r3.u64 = image + 0x20;
        ctx.r4.u64 = 1;
        __imp__ExRegisterTitleTerminateNotification(ctx, mem.base());
        ctx.r3.u64 = image + 0x40;
        __imp__ExRegisterTitleTerminateNotification(ctx, mem.base());
        auto registrations = dotm::title_termination_snapshot();
        check(registrations.size() == 2 &&
              registrations[0].callback_address == image + 0x100 &&
              registrations[0].priority == 5 &&
              registrations[1].callback_address == image + 0x104 &&
              registrations[1].priority == 2,
              "PPC r3/r4 ABI registers real callback and priority");
        ctx.r3.u64 = image + 0x20;
        __imp__ExRegisterTitleTerminateNotification(ctx, mem.base());
        check(dotm::title_termination_snapshot().size() == 2,
              "Repeated registration cannot duplicate a callback record");
        ctx.r4.u64 = 0;
        __imp__ExRegisterTitleTerminateNotification(ctx, mem.base());
        registrations = dotm::title_termination_snapshot();
        check(registrations.size() == 1 &&
              registrations[0].callback_address == image + 0x104,
              "Unregister removes callback while preserving other registrations");
        put32(mem, image + 0x20, 0xFFFF0000U);
        ctx.r4.u64 = 1;
        bool rejected = false;
        try { __imp__ExRegisterTitleTerminateNotification(ctx, mem.base()); }
        catch (const std::runtime_error&) { rejected = true; }
        check(rejected && dotm::title_termination_snapshot().size() == 1,
              "Uncommitted callback rejected without changing registrations");
        dotm::bind_title_termination(nullptr);
        check(dotm::title_termination_snapshot().empty(),
              "Title termination callbacks are scoped to the bound game");
        // Actual native XAM termination must execute guest callbacks before
        // unwinding, never fall through to the next original PPC instruction.
        dotm::bind_title_termination(&mem);
        constexpr auto code = dotm::GuestMemory::kCodeBase;
        mem.commit(code, 4096);
        mem.initialize_function_table(unexpected_callback);
        const std::array<dotm::FunctionEntry, 2> functions{{
            {code + 0x100U, first_callback},
            {code + 0x104U, second_callback},
        }};
        mem.install_functions(functions);
        put32(mem, image + 0x20, code + 0x100U);
        put32(mem, image + 0x24, 1);
        put32(mem, image + 0x40, code + 0x104U);
        put32(mem, image + 0x44, 5);
        ctx.r3.u64 = image + 0x20;
        ctx.r4.u64 = 1;
        __imp__ExRegisterTitleTerminateNotification(ctx, mem.base());
        ctx.r3.u64 = image + 0x40;
        __imp__ExRegisterTitleTerminateNotification(ctx, mem.base());
        bool terminated = false;
        try { __imp__XamLoaderTerminateTitle(ctx, mem.base()); }
        catch (const dotm::TitleTerminated&) { terminated = true; }
        check(terminated && callback_order == std::vector<int>({2, 1}),
              "XAM title shutdown executes original PPC callbacks by priority");
        terminated = false;
        try { __imp__XamLoaderTerminateTitle(ctx, mem.base()); }
        catch (const dotm::TitleTerminated&) { terminated = true; }
        check(terminated && callback_order.size() == 2,
              "Repeated termination must not dispatch callbacks twice");
        dotm::bind_title_termination(nullptr);
        check(dotm::title_termination_snapshot().empty(),
              "Terminated title callbacks must not leak to the next game");
        // Original _xstart can reach HalReturnToFirmware on a fatal CRT path.
        // It must preserve the requested routine and run title callbacks once.
        callback_order.clear();
        dotm::bind_title_termination(&mem);
        put32(mem, image + 0x20, code + 0x100U);
        put32(mem, image + 0x24, 7);
        ctx.r3.u64 = image + 0x20;
        ctx.r4.u64 = 1;
        __imp__ExRegisterTitleTerminateNotification(ctx, mem.base());
        ctx.r3.u64 = 2; // HalQuickRebootRoutine
        bool firmware_requested = false;
        try { __imp__HalReturnToFirmware(ctx, mem.base()); }
        catch (const dotm::FirmwareReturnRequested& e) {
            firmware_requested = e.routine() == 2;
        }
        check(firmware_requested && callback_order == std::vector<int>({1}),
              "HAL quick reboot propagates PPC r3 and dispatches shutdown once");
        firmware_requested = false;
        try { __imp__HalReturnToFirmware(ctx, mem.base()); }
        catch (const dotm::FirmwareReturnRequested& e) {
            firmware_requested = e.routine() == 2;
        }
        check(firmware_requested && callback_order.size() == 1,
              "Repeated firmware transition must not redispatch callbacks");
        dotm::bind_title_termination(nullptr);
        std::cout << "Xbox title-termination callback registration PPC ABI PASS\n";
        return 0;
    } catch (const std::exception& e) {
        dotm::bind_title_termination(nullptr);
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
