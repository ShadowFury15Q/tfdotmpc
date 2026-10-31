#pragma once
#include <cstdint>
namespace dotm {
class GuestMemory;
using GuestThreadEntry = std::uint32_t(*)(GuestMemory&, std::uint32_t pcr,
    std::uint32_t stack_pointer, std::uint32_t api_startup,
    std::uint32_t start_address, std::uint32_t parameter);
void bind_kernel_threads(GuestMemory* memory);
std::uint32_t nt_bound_create_thread(std::uint32_t handle_output,
    std::uint32_t stack_size, std::uint32_t thread_id_output,
    std::uint32_t api_startup, std::uint32_t start_address,
    std::uint32_t parameter, std::uint32_t creation_flags,
    GuestThreadEntry entry);
std::uint32_t nt_bound_resume_thread(std::uint32_t handle,
    std::uint32_t previous_suspend_count);
std::uint32_t nt_bound_wait_thread(std::uint32_t handle,
    std::uint32_t timeout);
std::uint32_t nt_bound_close_thread(std::uint32_t handle);
std::uint32_t nt_bound_delay_execution_thread(std::uint32_t wait_mode,
    std::uint32_t alertable, std::uint32_t interval);
[[noreturn]] void nt_bound_terminate_thread(std::uint32_t exit_code);
}
