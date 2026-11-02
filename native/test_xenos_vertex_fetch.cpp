#include "xenos_vertex_fetch.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

int main() {
    std::vector<std::uint32_t> registers(0x5003U, 0U);
    registers[0x4800U] = (0x1200U & ~3U) | 3U;
    registers[0x4801U] = (6U << 2U) | 2U;
    registers[0x4802U] = (0x2000U & ~3U) | 2U; // Texture, not vertex.
    registers[0x4803U] = (5U << 2U);
    registers[0x4800U + 95U * 2U] = 0xFFFF0003U;
    registers[0x4801U + 95U * 2U] = (4U << 2U) | 1U;
    const auto bindings = dotm::decode_xenos_vertex_fetch_constants(registers);
    if (bindings.size() != 2U || bindings[0].slot != 0U ||
        bindings[0].physical_address != 0x1200U ||
        bindings[0].size_dwords != 6U || bindings[0].endian_mode != 2U ||
        bindings[1].slot != 95U ||
        bindings[1].physical_address != 0xFFFF0000U ||
        bindings[1].size_dwords != 4U || bindings[1].endian_mode != 1U)
        return 1;
    bool rejected = false;
    try {
        (void)dotm::decode_xenos_vertex_fetch_constants(
            std::vector<std::uint32_t>(0x48BFU));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    if (!rejected) return 2;
    std::cout << "Xenos vertex fetch constant decode PASS\n";
    return 0;
}
