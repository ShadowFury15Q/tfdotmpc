#pragma once
#include "guest_memory.hpp"
#include <cstdint>
#include <vector>

namespace dotm {
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
}
