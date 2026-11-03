#pragma once

#include <array>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace dotm {

// Post-vertex-shader output. Xenos primitive 0x08 is not a host triangle:
// three transformed vertices form a rectangle whose fourth corner must be
// generated after the guest vertex shader, including interpolator channels.
struct XenosRasterVertex {
    std::array<float, 4> position{};
    std::vector<std::array<float, 4>> interpolators;
    std::vector<float> clip_cull_distances;
};

// The result is ordered for a host triangle strip. The caller must run the
// original vertex shader before calling this and must still do real raster,
// pixel-shader, EDRAM, and presentation work; this does not submit a draw.
inline std::array<XenosRasterVertex, 4> assemble_xenos_rectangle_strip(
    const std::array<XenosRasterVertex, 3>& vertices) {
    const auto interpolator_count = vertices[0].interpolators.size();
    const auto clip_count = vertices[0].clip_cull_distances.size();
    for (std::size_t i = 1; i < 3; ++i) {
        if (vertices[i].interpolators.size() != interpolator_count ||
            vertices[i].clip_cull_distances.size() != clip_count)
            throw std::runtime_error("Mismatched Xenos post-VS rectangle channels");
    }
    const auto edge_length_squared = [&](std::size_t a, std::size_t b) {
        const auto dx = vertices[a].position[0] - vertices[b].position[0];
        const auto dy = vertices[a].position[1] - vertices[b].position[1];
        return dx * dx + dy * dy;
    };
    const auto edge12 = edge_length_squared(1, 2);
    const auto edge20 = edge_length_squared(2, 0);
    const auto edge01 = edge_length_squared(0, 1);
    const std::array<std::size_t, 3> order =
        edge12 > edge20 && edge12 > edge01 ?
            std::array<std::size_t, 3>{0, 1, 2} :
        edge20 > edge01 ?
            std::array<std::size_t, 3>{1, 2, 0} :
            std::array<std::size_t, 3>{2, 0, 1};

    std::array<XenosRasterVertex, 4> strip{
        vertices[order[0]], vertices[order[1]], vertices[order[2]], {}};
    auto& fourth = strip[3];
    const auto& opposite = strip[0];
    const auto& adjacent0 = strip[1];
    const auto& adjacent1 = strip[2];
    for (std::size_t channel = 0; channel < 4; ++channel)
        fourth.position[channel] =
            adjacent0.position[channel] + adjacent1.position[channel] -
            opposite.position[channel];
    fourth.interpolators.resize(interpolator_count);
    for (std::size_t i = 0; i < interpolator_count; ++i)
        for (std::size_t channel = 0; channel < 4; ++channel)
            fourth.interpolators[i][channel] =
                adjacent0.interpolators[i][channel] +
                adjacent1.interpolators[i][channel] -
                opposite.interpolators[i][channel];
    fourth.clip_cull_distances.resize(clip_count);
    for (std::size_t i = 0; i < clip_count; ++i)
        fourth.clip_cull_distances[i] =
            adjacent0.clip_cull_distances[i] +
            adjacent1.clip_cull_distances[i] -
            opposite.clip_cull_distances[i];
    return strip;
}

} // namespace dotm
