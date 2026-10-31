#include "xex_privilege.hpp"
#include "xex_optional_header.hpp"
#include <cstdint>
namespace dotm {
namespace {
constexpr std::uint32_t kOriginalXexHeaderAddress = 0x81000000U;
constexpr std::uint32_t kSystemFlagsOptionalHeader = 0x00030000U;
}
std::uint32_t bound_xex_check_executable_privilege(std::uint32_t privilege) {
    if (privilege >= 32U) return 0;
    const auto flags = bound_xex_optional_header_field(
        kOriginalXexHeaderAddress, kSystemFlagsOptionalHeader);
    return (flags & (1U << privilege)) ? 1U : 0U;
}
}
