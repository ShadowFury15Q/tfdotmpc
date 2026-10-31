#pragma once
#include <cstdint>
namespace dotm {
class GuestMemory;
void bind_kernel_critical_sections(GuestMemory* memory);
void kernel_initialize_critical_section(std::uint32_t address, std::uint32_t spin_count);
void kernel_enter_critical_section(std::uint32_t address);
bool kernel_try_enter_critical_section(std::uint32_t address);
void kernel_leave_critical_section(std::uint32_t address);
}
