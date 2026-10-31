#pragma once
#include <cstdint>
namespace dotm {
class GuestMemory;
// Resolve XEX2 optional header keys from actual big-endian guest XEX bytes.
std::uint32_t xex_optional_header_field(GuestMemory& memory, std::uint32_t header_address,
                                        std::uint32_t key);
void bind_xex_optional_header(GuestMemory* memory) noexcept;
std::uint32_t bound_xex_optional_header_field(std::uint32_t header_address,
                                              std::uint32_t key);
}
