#pragma once
#include <cstdint>
namespace dotm {
class GuestMemory;
// Binding is thread-local, as Xbox kernel imports run on the calling guest thread.
void bind_kernel_rtl_memory(GuestMemory* memory) noexcept;
void rtl_bound_fill_memory_ulong(std::uint32_t destination,
                                 std::uint32_t length,
                                 std::uint32_t pattern);
std::uint32_t rtl_bound_compare_memory_ulong(std::uint32_t source,
                                              std::uint32_t length,
                                              std::uint32_t pattern);
}
