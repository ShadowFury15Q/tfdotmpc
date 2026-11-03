#include "xenos_vertex_fetch.hpp"
#include "gpu_engines.hpp"
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
    const std::vector<std::uint8_t> raw = {0x11, 0x22, 0x33, 0x44,
                                           0x55, 0x66, 0x77, 0x88};
    const std::vector<std::vector<std::uint8_t>> expected = {
        raw,
        {0x22, 0x11, 0x44, 0x33, 0x66, 0x55, 0x88, 0x77},
        {0x44, 0x33, 0x22, 0x11, 0x88, 0x77, 0x66, 0x55},
        {0x33, 0x44, 0x11, 0x22, 0x77, 0x88, 0x55, 0x66},
    };
    for (std::uint32_t mode = 0; mode < 4; ++mode) {
        dotm::XenosVertexFetchConstant binding{0U, 0x1200U, 2U, mode};
        if (dotm::decode_xenos_vertex_fetch_bytes(binding, raw) != expected[mode])
            return 3;
    }
    try {
        (void)dotm::decode_xenos_vertex_fetch_bytes({0, 0x1200, 2, 4}, raw);
        return 4;
    } catch (const std::runtime_error&) {}
    try {
        (void)dotm::decode_xenos_vertex_fetch_bytes({0, 0x1200, 3, 2}, raw);
        return 5;
    } catch (const std::runtime_error&) {}

    dotm::GpuDrawCommand draw{};
    draw.copy_physical_memory = +[](std::uint32_t address, std::uint32_t count) {
        if (address != 0x1200U || count != 8U)
            throw std::runtime_error("Incorrect original vertex fetch range");
        return std::vector<std::uint8_t>{0x11, 0x22, 0x33, 0x44,
                                         0x55, 0x66, 0x77, 0x88};
    };
    draw.vertex_fetch_constants.push_back({0U, 0x1200U, 2U, 2U});
    if (draw.copy_vertex_fetch_buffer(0U) != expected[2]) return 6;
    try {
        (void)draw.copy_vertex_fetch_buffer(1U);
        return 7;
    } catch (const std::runtime_error&) {}
    draw.copy_physical_memory = nullptr;
    try {
        (void)draw.copy_vertex_fetch_buffer(0U);
        return 8;
    } catch (const std::runtime_error&) {}

    // A shader may read only part of a large bound buffer. Preserve the
    // original DWORD-wide Xenos lane swap even for unaligned byte ranges.
    draw.copy_physical_memory = +[](std::uint32_t address, std::uint32_t count) {
        if (address != 0x1200U || count != 8U)
            throw std::runtime_error("Unaligned fetch did not expand to complete lanes");
        return std::vector<std::uint8_t>{0x11, 0x22, 0x33, 0x44,
                                         0x55, 0x66, 0x77, 0x88};
    };
    if (draw.copy_vertex_fetch_range(0U, 3U, 3U) !=
        std::vector<std::uint8_t>({0x11, 0x88, 0x77}))
        return 9;
    draw.copy_physical_memory = +[](std::uint32_t address, std::uint32_t count) {
        if (address != 0x1204U || count != 4U)
            throw std::runtime_error("Vertex fetch copied unused physical bytes");
        return std::vector<std::uint8_t>{0x55, 0x66, 0x77, 0x88};
    };
    if (draw.copy_vertex_fetch_range(0U, 4U, 3U) !=
        std::vector<std::uint8_t>({0x88, 0x77, 0x66}))
        return 10;
    try {
        (void)draw.copy_vertex_fetch_range(0U, 7U, 2U);
        return 11;
    } catch (const std::runtime_error&) {}
    if (!draw.copy_vertex_fetch_range(0U, 8U, 0U).empty()) return 12;
    draw.copy_physical_memory = nullptr;
    try {
        (void)draw.copy_vertex_fetch_range(0U, 0U, 1U);
        return 13;
    } catch (const std::runtime_error&) {}
    draw.copy_physical_memory = +[](std::uint32_t, std::uint32_t count) {
        return std::vector<std::uint8_t>(count, 0U);
    };
    draw.vertex_fetch_constants.push_back({1U, 0xFFFFFFFCU, 2U, 0U});
    try {
        (void)draw.copy_vertex_fetch_range(1U, 4U, 1U);
        return 14;
    } catch (const std::runtime_error&) {}

    // A truncated guest DMA read must fail before shader vertex decoding.
    draw.copy_physical_memory = +[](std::uint32_t, std::uint32_t) {
        return std::vector<std::uint8_t>{0x11U, 0x22U};
    };
    try {
        (void)draw.copy_vertex_fetch_range(0U, 0U, 4U);
        return 15;
    } catch (const std::runtime_error&) {}

    std::cout << "Xenos vertex fetch constant and DMA endian decode PASS\n";
    return 0;
}
