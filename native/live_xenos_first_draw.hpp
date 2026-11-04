#pragma once
#include "gpu_engines.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace dotm {
// Only the verified ORIGINAL retail first PM4 rectangle is currently
// supported by this live Windows renderer. Fail closed on other commands.
inline std::uint64_t original_xenos_word_hash(
    const std::vector<std::uint32_t>& words) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    for (auto word : words)
        for (unsigned shift : {24U, 16U, 8U, 0U})
            hash = (hash ^ ((word >> shift) & 255U)) * 1099511628211ULL;
    return hash;
}
inline bool is_original_first_xenos_draw(const GpuDrawCommand& draw) {
    if (draw.initiator != 0x00030088U || draw.primitive_type != 8U ||
        draw.source_select != 2U || draw.major_mode != 0U ||
        draw.index_count != 3U || draw.index_size_bytes != 2U ||
        draw.has_visibility_query_token || !draw.decoded_indices.empty() ||
        draw.vertex_shader_words.size() != 27U ||
        draw.pixel_shader_words.size() != 9U ||
        original_xenos_word_hash(draw.vertex_shader_words) != 0x539EA9E08AA83F0CULL ||
        original_xenos_word_hash(draw.pixel_shader_words) != 0x438C2AF84C78A133ULL ||
        draw.vertex_fetch_constants.size() != 1U)
        return false;
    const auto& v = draw.vertex_fetch_constants[0];
    return v.slot == 0U && v.size_dwords == 21U && v.endian_mode == 2U;
}
inline std::vector<std::uint8_t> read_original_first_xenos_vertices(
    const GpuDrawCommand& draw) {
    if (!is_original_first_xenos_draw(draw))
        throw std::runtime_error("Unsupported live original Xenos draw state");
    // Reads the ORIGINAL active physical GPU allocation through the PM4
    // scoped reader; does not use offline capture bytes.
    auto data = draw.copy_vertex_fetch_range(0U, 0U, 84U);
    if (data.size() != 84U)
        throw std::runtime_error("Incomplete original Xenos vertex DMA");
    std::array<std::array<float, 2>, 3> xy{};
    for (std::size_t i = 0; i < 3; ++i) {
        std::memcpy(xy[i].data(), data.data() + i * 28U, 8U);
        if (!std::isfinite(xy[i][0]) || !std::isfinite(xy[i][1]))
            throw std::runtime_error("Nonfinite live original Xenos vertices");
    }
    if (!(xy[1][0] > xy[0][0] && xy[2][1] > xy[1][1]))
        throw std::runtime_error("Unexpected live original rectangle layout");
    return data;
}
}
