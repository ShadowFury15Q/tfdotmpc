#pragma once
#include <cstdint>
#include <string_view>
namespace dotm {
// XamGetSystemVersion uses 0x20000000 | (dashboard build << 8) | QFE.
// This is the host-selected retail Xbox 360 dashboard profile, not the title's
// XEX execution-ID version (which describes the game, not the console OS).
constexpr std::uint32_t encode_xam_dashboard_version(
    std::uint16_t build, std::uint8_t qfe) noexcept {
    return 0x20000000U | (std::uint32_t(build) << 8) | qfe;
}
std::uint32_t parse_xam_dashboard_version(std::string_view text);
std::uint32_t xam_system_version();
}

// Xbox 360 XGetLanguage returns a console language ID, not a locale string.
std::uint32_t parse_xam_console_language(std::string_view locale) noexcept;
std::uint32_t xam_console_language() noexcept;
