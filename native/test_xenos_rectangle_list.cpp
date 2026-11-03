#include "xenos_rectangle_list.hpp"
#include <array>
#include <stdexcept>

using dotm::XenosRasterVertex;

#define VERIFY(condition) do { if (!(condition)) throw std::runtime_error(#condition); } while (false)

static XenosRasterVertex vertex(float x, float y, float u, float v,
                                 float clip_distance) {
    XenosRasterVertex result;
    result.position = {x, y, 0.25f, 1.0f};
    result.interpolators = {{{u, v, 1.0f, 2.0f}}};
    result.clip_cull_distances = {clip_distance};
    return result;
}

int main() {
    {
        // 1-2 is the diagonal. Preserve winding 0-1-2-3, including the
        // fourth interpolator and clip distance synthesized after the VS.
        const std::array<XenosRasterVertex, 3> input{
            vertex(0, 1, 0, 1, 1), vertex(1, 1, 1, 1, 2),
            vertex(0, 0, 0, 0, 3)};
        const auto strip = dotm::assemble_xenos_rectangle_strip(input);
        VERIFY(strip[0].position[0] == 0 && strip[1].position[0] == 1);
        VERIFY(strip[2].position[1] == 0);
        VERIFY(strip[3].position == (std::array<float, 4>{1, 0, 0.25f, 1}));
        VERIFY(strip[3].interpolators[0] ==
               (std::array<float, 4>{1, 0, 1, 2}));
        VERIFY(strip[3].clip_cull_distances[0] == 4);
    }
    {
        // 2-0 diagonal: triangle order must rotate to 1-2-0-3.
        const std::array<XenosRasterVertex, 3> input{
            vertex(1, 0, 1, 0, 0), vertex(0, 0, 0, 0, 0),
            vertex(0, 2, 0, 2, 0)};
        const auto strip = dotm::assemble_xenos_rectangle_strip(input);
        VERIFY(strip[0].position == input[1].position);
        VERIFY(strip[1].position == input[2].position);
        VERIFY(strip[2].position == input[0].position);
        VERIFY(strip[3].position == (std::array<float, 4>{1, 2, 0.25f, 1}));
    }
    {
        // 0-1 diagonal: order 2-0-1-3, preserving signed strip winding.
        const std::array<XenosRasterVertex, 3> input{
            vertex(0, 2, 0, 2, 0), vertex(1, 0, 1, 0, 0),
            vertex(0, 0, 0, 0, 0)};
        const auto strip = dotm::assemble_xenos_rectangle_strip(input);
        VERIFY(strip[0].position == input[2].position);
        VERIFY(strip[1].position == input[0].position);
        VERIFY(strip[2].position == input[1].position);
        VERIFY(strip[3].position == (std::array<float, 4>{1, 2, 0.25f, 1}));
    }
    {
        // All channels must exist for all three shader output vertices.
        std::array<XenosRasterVertex, 3> input{
            vertex(0, 1, 0, 1, 0), vertex(1, 1, 1, 1, 0),
            vertex(0, 0, 0, 0, 0)};
        input[2].interpolators.clear();
        bool rejected = false;
        try { (void)dotm::assemble_xenos_rectangle_strip(input); }
        catch (const std::runtime_error&) { rejected = true; }
        VERIFY(rejected);
        input[2].interpolators = input[1].interpolators;
        input[2].clip_cull_distances.clear();
        rejected = false;
        try { (void)dotm::assemble_xenos_rectangle_strip(input); }
        catch (const std::runtime_error&) { rejected = true; }
        VERIFY(rejected);
    }
}
