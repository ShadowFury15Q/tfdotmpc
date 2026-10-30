#pragma once
#include "guest_virtual_allocator.hpp"
#include <cstdint>

namespace dotm {
// Restricted Xbox 360 kernel virtual-memory bridge. All guest pointers and
// pointed-to 32-bit values are big-endian. Unsupported flags fail closed.
inline constexpr std::uint32_t kNtSuccess = 0x00000000U;
inline constexpr std::uint32_t kNtInvalidParameter = 0xC000000DU;
inline constexpr std::uint32_t kNtNoMemory = 0xC0000017U;
inline constexpr std::uint32_t kNtNotSupported = 0xC00000BBU;
inline constexpr std::uint32_t kNtMemoryNotAllocated = 0xC00000A0U;

std::uint32_t nt_allocate_virtual_memory_guest(
    GuestMemory& memory, GuestVirtualAllocator& allocator,
    std::uint32_t base_pointer, std::uint32_t size_pointer,
    std::uint32_t allocation_type, std::uint32_t protect,
    std::uint32_t debug_memory);
std::uint32_t nt_query_virtual_memory_guest(
    GuestMemory& memory, const GuestVirtualAllocator& allocator,
    std::uint32_t address, std::uint32_t output_pointer);
std::uint32_t nt_free_virtual_memory_guest(
    GuestMemory& memory, GuestVirtualAllocator& allocator,
    std::uint32_t base_pointer, std::uint32_t size_pointer,
    std::uint32_t free_type, std::uint32_t debug_memory);

// Host service binding for recompiled PowerPC imports. Caller must unbind
// before destroying either object; no ownership is transferred.
void bind_kernel_guest_memory(GuestMemory* memory,
                              GuestVirtualAllocator* allocator) noexcept;
std::uint32_t nt_bound_allocate_virtual_memory(
    std::uint32_t base_pointer, std::uint32_t size_pointer,
    std::uint32_t allocation_type, std::uint32_t protect,
    std::uint32_t debug_memory);
std::uint32_t nt_bound_query_virtual_memory(
    std::uint32_t address, std::uint32_t output_pointer);
std::uint32_t nt_bound_free_virtual_memory(
    std::uint32_t base_pointer, std::uint32_t size_pointer,
    std::uint32_t free_type, std::uint32_t debug_memory);
}
