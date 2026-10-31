#include "xam_system_version.hpp"
#include <array>
#include <charconv>
#include <cstdlib>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
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

std::uint32_t parse_xam_console_language(std::string_view locale) noexcept {
    // Original Xbox 360 XLanguage IDs; unknown PC locales use English.
    if (locale.size() < 2) return 1U;
    auto lower = [](char c) noexcept -> char {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
    };
    const char a = lower(locale[0]), b = lower(locale[1]);
    if (locale.size() > 2 && locale[2] != '_' && locale[2] != '-' &&
        locale[2] != '.' && locale[2] != '@') return 1U;
    if (a == 'e' && b == 'n') return 1U;
    if (a == 'j' && b == 'a') return 2U;
    if (a == 'd' && b == 'e') return 3U;
    if (a == 'f' && b == 'r') return 4U;
    if (a == 'e' && b == 's') return 5U;
    if (a == 'i' && b == 't') return 6U;
    if (a == 'k' && b == 'o') return 7U;
    if (a == 'p' && b == 't') return 9U;
    if (a == 'p' && b == 'l') return 11U;
    if (a == 'r' && b == 'u') return 12U;
    if (a == 'z' && b == 'h') {
        // Xbox 360: traditional Chinese 8, simplified Chinese 10.
        if (locale.size() >= 5 && (locale[2] == '_' || locale[2] == '-')) {
            const char c = lower(locale[3]), d = lower(locale[4]);
            if ((c == 't' && d == 'w') || (c == 'h' && d == 'k') ||
                (c == 'm' && d == 'o')) return 8U;
        }
        return 10U;
    }
    return 1U;
}
std::uint32_t xam_console_language() noexcept {
    if (const char* configured = std::getenv("DOTM_XBOX_LANGUAGE"))
        return parse_xam_console_language(configured);
#ifdef _WIN32
    // Match the host user's actual Windows UI language, not a fixed stub.
    const auto primary = GetUserDefaultUILanguage() & 0x03FFU;
    switch (primary) {
        case LANG_JAPANESE: return 2U;
        case LANG_GERMAN: return 3U;
        case LANG_FRENCH: return 4U;
        case LANG_SPANISH: return 5U;
        case LANG_ITALIAN: return 6U;
        case LANG_KOREAN: return 7U;
        case LANG_PORTUGUESE: return 9U;
        case LANG_POLISH: return 11U;
        case LANG_RUSSIAN: return 12U;
        case LANG_CHINESE: {
            const auto sub = GetUserDefaultUILanguage() >> 10;
            return sub == SUBLANG_CHINESE_TRADITIONAL ||
                sub == SUBLANG_CHINESE_HONGKONG ||
                sub == SUBLANG_CHINESE_MACAU ? 8U : 10U;
        }
        default: return 1U;
    }
#else
    for (const char* key : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        if (const char* value = std::getenv(key); value && *value)
            return parse_xam_console_language(value);
    }
    return 1U;
#endif
}

}
