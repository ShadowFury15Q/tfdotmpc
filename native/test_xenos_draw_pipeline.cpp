#include "xenos_draw_pipeline.hpp"
#include <array>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::array<std::uint8_t, 256> guest{};
std::vector<std::uint8_t> fetch(std::uint32_t physical, std::uint32_t count) {
    if (physical < 0x1000U || std::uint64_t(physical) + count > 0x1100U)
        throw std::runtime_error("Reader outside allocated test buffer");
    auto start = physical - 0x1000U;
    return {guest.begin() + start, guest.begin() + start + count};
}
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
void fail(auto&& work, const char* why) {
    bool caught = false;
    try { work(); } catch (const std::runtime_error&) { caught = true; }
    check(caught, why);
}
}
int main() {
    // Synthetic testing uses no proprietary original shader bytes or assets.
    std::istringstream text("stream 0 16 POSITION 0 0 float4\n"
                             "stream 2 4 COLOR 0 0 unorm8x4\n");
    const auto layout = dotm::XenosVertexLayout::parse(text);
    check(layout.attributes.size() == 2, "Two distinct fetch slots lost");
    dotm::GpuDrawCommand draw;
    draw.initiator = (4U << 16U) | 4U | (2U << 6U);
    draw.primitive_type = 4;
    draw.source_select = 2;
    draw.index_count = 4;
    draw.vertex_shader_words = {0x12345678U};
    draw.pixel_shader_words = {0xAABBCCDDU};
    draw.vertex_fetch_constants.push_back({0, 0x1000U, 32U, 0U});
    draw.vertex_fetch_constants.push_back({2, 0x1080U, 16U, 0U});
    draw.copy_physical_memory = &fetch;
    auto plain = dotm::prepare_original_xenos_draw(draw, layout);
    check(plain.count == 4 && plain.streams.size() == 2 &&
          plain.streams[0].bytes.size() == 64 &&
          plain.streams[1].bytes.size() == 16,
          "PM4 auto-indexed multi-slot draw validation incorrect");
    draw.source_select = 0;
    draw.index_size_bytes = 2;
    draw.decoded_indices = {0, 2, 7, 1};
    auto indexed = dotm::prepare_original_xenos_draw(draw, layout);
    check(indexed.indices.size() == 4 &&
          indexed.streams[0].bytes.size() == 128 &&
          indexed.streams[1].bytes.size() == 32,
          "PM4 indexed-draw did not fetch real highest index");
    draw.has_visibility_query_token = true;
    fail([&] { dotm::prepare_original_xenos_draw(draw, layout); },
         "Conditional original GPU draw was incorrectly accepted");
    draw.has_visibility_query_token = false;
    draw.source_select = 2;
    draw.decoded_indices.clear();
    draw.primitive_type = 5;
    auto fan = dotm::prepare_original_xenos_draw(draw, layout);
    check(fan.topology == dotm::XenosHostTopology::triangle_list &&
          fan.count == 6U &&
          fan.indices == std::vector<std::uint32_t>({0, 1, 2, 0, 2, 3}),
          "Original Xenos triangle fan did not preserve original winding");
    draw.primitive_type = 12;
    auto loop = dotm::prepare_original_xenos_draw(draw, layout);
    check(loop.topology == dotm::XenosHostTopology::line_strip &&
          loop.count == 5U &&
          loop.indices == std::vector<std::uint32_t>({0, 1, 2, 3, 0}),
          "Original Xenos line loop lost closing edge");
    draw.source_select = 0;
    draw.decoded_indices = {0, 2, 7, 1};
    draw.primitive_type = 5;
    auto indexed_fan = dotm::prepare_original_xenos_draw(draw, layout);
    check(indexed_fan.indices ==
          std::vector<std::uint32_t>({0, 2, 7, 0, 7, 1}),
          "DMA-indexed original triangle fan indices were altered");
    draw.primitive_type = 4;
    draw.decoded_indices = {0, 2, 80, 1};
    fail([&] { dotm::prepare_original_xenos_draw(draw, layout); },
         "Out-of-bounds original physical DMA silently accepted");
    std::istringstream bad("stream 0 8 POSITION 0 0 float4\n");
    fail([&] { (void)dotm::XenosVertexLayout::parse(bad); },
         "Input element beyond guest stride was accepted");
    std::istringstream clash(
        "stream 0 16 POSITION 0 0 float4\n"
        "stream 2 16 POSITION 0 0 float4\n");
    fail([&] { (void)dotm::XenosVertexLayout::parse(clash); },
         "Duplicate guest shader semantic was accepted");
    const auto shader = dotm::xenos_shader_hex(dotm::xenos_shader_hash({0x12345678U}));
    check(shader.size() == 16, "Original shader identity is not 64 bit");
    auto key = dotm::xenos_shader_pair(draw);
    draw.pixel_shader_words = {0xAABBCCDEU};
    check(!(dotm::xenos_shader_pair(draw) == key),
          "Different original Xenos shader pair reused identical pipeline");
    return 0;
}
