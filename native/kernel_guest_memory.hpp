#pragma once
#include "guest_virtual_allocator.hpp"
#include <cstdint>
#include <array>
#include <optional>

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
// Xbox 360 MM_STATISTICS: 104 bytes, big-endian words.
std::uint32_t mm_query_statistics_guest(
    GuestMemory& memory, const GuestVirtualAllocator& allocator,
    std::uint32_t output_pointer);
std::uint32_t mm_bound_query_statistics(std::uint32_t output_pointer);
// Xbox 360 physical allocation ABI: six PPC register arguments r3-r8.
// Physical offsets share one 512 MiB budget; distinct guest views are
// currently host-backed independently (GPU alias coherence is still pending).
std::uint32_t mm_bound_allocate_physical_memory_ex(
    std::uint32_t flags, std::uint32_t bytes, std::uint32_t protect,
    std::uint32_t min_address, std::uint32_t max_address,
    std::uint32_t alignment);
void mm_bound_free_physical_memory(std::uint32_t type,
                                   std::uint32_t guest_address);
std::uint32_t mm_bound_query_physical_allocation_size(
    std::uint32_t guest_address);
std::uint32_t mm_bound_get_physical_address(std::uint32_t guest_address);
// Resolve GPU DMA offsets only within a real physical allocation.
std::optional<std::uint32_t> mm_bound_resolve_physical_range(
    std::uint32_t physical_address, std::uint32_t length);
// Xbox 360 ExGetXConfigSetting with 16-bit big-endian required-size output.
std::uint32_t ex_get_xconfig_setting_guest(GuestMemory& memory,
    std::uint16_t category, std::uint16_t setting, std::uint32_t buffer,
    std::uint16_t buffer_size, std::uint32_t required_size);
void vd_bound_query_video_mode(std::uint32_t output_pointer);
std::uint32_t ex_bound_get_xconfig_setting(std::uint16_t category,
    std::uint16_t setting, std::uint32_t buffer, std::uint16_t buffer_size,
    std::uint32_t required_size);
std::uint32_t nt_free_virtual_memory_guest(
    GuestMemory& memory, GuestVirtualAllocator& allocator,
    std::uint32_t base_pointer, std::uint32_t size_pointer,
    std::uint32_t free_type, std::uint32_t debug_memory);

// Host service binding for recompiled PowerPC imports. Caller must unbind
// before destroying either object; no ownership is transferred.
// Xbox KeQuerySystemTime: 64-bit UTC FILETIME (100 ns since 1601), big-endian.
// The explicit-value overload supports deterministic PPC ABI regression tests.
void ke_query_system_time_guest(GuestMemory& memory, std::uint32_t output,
                                std::uint64_t filetime);
void ke_bound_query_system_time(std::uint32_t output);
// Xbox DbgPrint: format in r3, seven 32-bit PPC varargs in r4-r10.
// Reads only committed guest strings; never passes guest pointers to host printf.
std::uint32_t ke_bound_dbg_print(std::uint32_t format,
    const std::array<std::uint32_t, 7>& args);
void bind_kernel_guest_memory(GuestMemory* memory,
                              GuestVirtualAllocator* allocator) noexcept;
// Xbox 360 KeInitializeDpc: initialize a 32-byte PPC KDPC with the
// deferred routine and context in big-endian guest memory.
void ke_bound_initialize_dpc(std::uint32_t dpc, std::uint32_t routine,
                             std::uint32_t context);
// Xbox 360 ExAllocatePool/ExAllocatePoolWithTag/ExFreePool: page-backed
// guest allocations with exact-pointer ownership and synchronized release.
std::uint32_t ex_bound_allocate_pool(std::uint32_t pool_type,
                                     std::uint32_t bytes, std::uint32_t tag);
void ex_bound_free_pool(std::uint32_t address);
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
