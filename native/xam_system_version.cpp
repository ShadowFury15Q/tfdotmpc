#include "xam_system_version.hpp"
#include <array>
#include <charconv>
#include <cstdlib>
#include <stdexcept>
namespace dotm {
namespace {
std::uint32_t parse_decimal(std::string_view field) {
    if (field.empty()) throw std::invalid_argument("Empty Xbox dashboard version component");
    std::uint32_t value = 0;
    const auto result = std::from_chars(field.data(), field.data() + field.size(), value, 10);
    if (result.ec != std::errc{} || result.ptr != field.data() + field.size())
        throw std::invalid_argument("Invalid Xbox dashboard version component");
    return value;
}
}
std::uint32_t parse_xam_dashboard_version(std::string_view text) {
    std::array<std::uint32_t, 4> fields{};
    for (std::size_t index = 0; index < fields.size(); ++index) {
        const auto dot = text.find('.');
        if (index < fields.size() - 1 && dot == std::string_view::npos)
            throw std::invalid_argument("Expected Xbox dashboard version 2.0.build.qfe");
        if (index == fields.size() - 1 && dot != std::string_view::npos)
            throw std::invalid_argument("Too many Xbox dashboard version components");
        fields[index] = parse_decimal(text.substr(0, dot));
        if (dot != std::string_view::npos) text.remove_prefix(dot + 1);
    }
    if (fields[0] != 2 || fields[1] != 0 || fields[2] < 1888 ||
        fields[2] > 17559 || fields[3] > 255)
        throw std::invalid_argument("Unsupported retail Xbox 360 dashboard version");
    return encode_xam_dashboard_version(
        static_cast<std::uint16_t>(fields[2]), static_cast<std::uint8_t>(fields[3]));
}
std::uint32_t xam_system_version() {
    static const std::uint32_t version = [] {
        const char* configured = std::getenv("DOTM_XAM_DASHBOARD_VERSION");
        // Final retail Xbox 360 dashboard: 2.0.17559.0. A real console OS
        // version cannot be read from the game's XEX, so allow an explicit
        // compatible retail profile rather than fabricating a title-derived one.
        return parse_xam_dashboard_version(configured ? configured : "2.0.17559.0");
    }();
    return version;
}
}
