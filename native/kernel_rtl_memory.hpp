#pragma once
#include <cstdint>
namespace dotm {
class GuestMemory;
// Binding is thread-local, as Xbox kernel imports run on the calling guest thread.
void bind_kernel_rtl_memory(GuestMemory* memory) noexcept;
std::uint32_t rtl_bound_multi_byte_to_unicode(std::uint32_t output,
    std::uint32_t output_max, std::uint32_t written, std::uint32_t input,
    std::uint32_t input_size);
std::uint32_t rtl_bound_unicode_to_multi_byte(std::uint32_t output,
    std::uint32_t output_max, std::uint32_t written, std::uint32_t input,
    std::uint32_t input_size);
void rtl_bound_fill_memory_ulong(std::uint32_t destination,
                                 std::uint32_t length,
                                 std::uint32_t pattern);
std::uint32_t rtl_ntstatus_to_dos_error(std::uint32_t status) noexcept;
std::uint32_t rtl_bound_compare_memory_ulong(std::uint32_t source,
                                              std::uint32_t length,
                                              std::uint32_t pattern);
}
