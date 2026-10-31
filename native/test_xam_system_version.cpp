#include "xam_system_version.hpp"
#include <stdexcept>
#include <string_view>
#include <iostream>
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
    std::cout << "XAM dashboard version profile PASS\n";
}
