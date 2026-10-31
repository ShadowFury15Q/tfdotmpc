#pragma once
#include "guest_memory.hpp"
#include <cstdint>
#include <vector>
#include <stdexcept>

namespace dotm {
class TitleTerminated final : public std::runtime_error {
public:
    TitleTerminated() : std::runtime_error("Xbox 360 title requested termination via XamLoaderTerminateTitle") {}
};
// A firmware power/reboot request ends the guest title, never the host OS.
class FirmwareReturnRequested final : public std::runtime_error {
public:
    explicit FirmwareReturnRequested(std::uint32_t routine)
        : std::runtime_error("Xbox 360 title requested HalReturnToFirmware"),
          routine_(routine) {}
    std::uint32_t routine() const noexcept { return routine_; }
private:
    std::uint32_t routine_;
};
struct TitleTerminationNotification {
    std::uint32_t registration_address = 0;
    std::uint32_t callback_address = 0;
    std::uint32_t priority = 0;
};
// Bind to the current title; all registrations are discarded on unbind.
void bind_title_termination(GuestMemory* memory) noexcept;
// ExRegisterTitleTerminateNotification(r3=registration, r4=create).
// The 16-byte guest registration has callback and priority at offsets 0 and 4.
void ex_bound_register_title_termination(std::uint32_t registration,
                                         std::uint32_t create);
std::vector<TitleTerminationNotification> title_termination_snapshot();
// XamLoaderTerminateTitle: execute registered PPC termination callbacks in
// priority order, then unwind to the host without returning to guest code.
[[noreturn]] void xam_bound_loader_terminate_title(PPCContext& context);
// HalReturnToFirmware(r3=power/reboot routine): execute title termination
// notifications, then unwind to the host without rebooting the PC.
[[noreturn]] void hal_bound_return_to_firmware(PPCContext& context,
                                                std::uint32_t routine);
}
