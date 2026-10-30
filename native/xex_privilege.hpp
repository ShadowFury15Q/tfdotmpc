#pragma once
#include <cstdint>
namespace dotm {
// The original Xbox 360 XEX_SYSTEM_FLAGS optional header controls privilege
// bits. Read the real, privately loaded XEX2 metadata, not host defaults.
std::uint32_t bound_xex_check_executable_privilege(std::uint32_t privilege);
}
