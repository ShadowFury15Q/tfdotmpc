#pragma once
#include <cstdint>
namespace dotm {
class GuestMemory;
void bind_kernel_mutants(GuestMemory* memory) noexcept;
void cancel_kernel_mutant_waiters() noexcept;
// Xbox 360 in-memory KEVENT dispatcher objects (not NT handles).
std::uint32_t ke_bound_set_event(std::uint32_t object, std::uint32_t increment,
                                 std::uint32_t wait);
std::uint32_t ke_bound_reset_event(std::uint32_t object);
std::uint32_t ke_bound_wait_for_single_object(std::uint32_t object,
    std::uint32_t reason, std::uint32_t mode, std::uint32_t alertable,
    std::uint32_t timeout);

std::uint32_t nt_bound_create_event(std::uint32_t output_handle,
                                     std::uint32_t object_attributes,
                                     std::uint32_t event_type,
                                     std::uint32_t initial_state);
std::uint32_t nt_bound_set_event(std::uint32_t handle, std::uint32_t previous_state);
std::uint32_t nt_bound_clear_event(std::uint32_t handle);
std::uint32_t nt_bound_pulse_event(std::uint32_t handle, std::uint32_t previous_state);
std::uint32_t nt_bound_create_mutant(std::uint32_t output_handle,
                                     std::uint32_t object_attributes,
                                     std::uint32_t initial_owner);
std::uint32_t nt_bound_release_mutant(std::uint32_t handle,
                                      std::uint32_t previous_count);
std::uint32_t nt_bound_wait_for_single_object_ex(std::uint32_t handle,
                                                  std::uint32_t wait_mode,
                                                  std::uint32_t alertable,
                                                  std::uint32_t timeout);
std::uint32_t nt_bound_close_mutant(std::uint32_t handle);
}
