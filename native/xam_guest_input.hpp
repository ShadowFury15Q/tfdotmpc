#pragma once
#include "guest_memory.hpp"
#include "xam_input.hpp"
#include <cstdint>

namespace dotm {
// Implements a single original XAM function's memory semantics, not a
// complete Xbox kernel/XAM subsystem. The final binding to the compiled
// PPC import is separately guarded by a runtime host-service registration.
std::uint32_t xam_input_get_state_guest(
    XamInputHost& input,GuestMemory& memory,
    std::uint32_t user,std::uint32_t flags,std::uint32_t guest_address);
void bind_xam_guest_input(XamInputHost* input,GuestMemory* memory) noexcept;
std::uint32_t xam_bound_input_get_state(
    std::uint32_t user,std::uint32_t flags,std::uint32_t guest_address);
}
