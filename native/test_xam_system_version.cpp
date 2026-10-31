#include "xam_system_version.hpp"
#include <stdexcept>
#include <string_view>
#include <iostream>
#include <utility>
int main() {
    if (dotm::parse_xam_dashboard_version("2.0.17559.0") != 0x20449700U)
        throw std::runtime_error("Retail dashboard version encoding mismatch");
    if (dotm::parse_xam_dashboard_version("2.0.12611.1") != 0x20314301U)
        throw std::runtime_error("Dashboard build/QFE encoding mismatch");
    if (dotm::xam_system_version() != 0x20449700U)
        throw std::runtime_error("Default retail dashboard profile mismatch");
    for (const auto text : {"", "2.0.17559", "2.0.17559.0.0", "3.0.17559.0", "2.1.17559.0", "2.0.99999.0", "2.0.17559.256", "2.0.-1.0", "2.0.17559.0junk"}) {
        bool rejected = false;
        try { (void)dotm::parse_xam_dashboard_version(text); }
        catch (const std::invalid_argument&) { rejected = true; }
        if (!rejected) throw std::runtime_error("Invalid dashboard version accepted");
    }
    for (const auto [locale, expected] : {
        std::pair{"en_US.UTF-8", 1U}, std::pair{"ja_JP", 2U},
        std::pair{"de_DE", 3U}, std::pair{"fr_FR", 4U},
        std::pair{"es_ES", 5U}, std::pair{"it_IT", 6U},
        std::pair{"ko_KR", 7U}, std::pair{"zh_TW", 8U},
        std::pair{"pt_BR", 9U}, std::pair{"zh_CN", 10U},
        std::pair{"pl_PL", 11U}, std::pair{"ru_RU", 12U},
        std::pair{"uk_UA", 1U}, std::pair{"C.UTF-8", 1U}
    }) {
        if (dotm::parse_xam_console_language(locale) != expected)
            throw std::runtime_error("Xbox 360 console language mapping mismatch");
    }
    std::cout << "XAM dashboard and original Xbox console language profile PASS\n";
}
