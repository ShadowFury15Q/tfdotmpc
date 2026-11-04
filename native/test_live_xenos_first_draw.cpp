#include "live_xenos_first_draw.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {
std::array<std::uint8_t, 84> raw{};
std::vector<std::uint8_t> read_physical(std::uint32_t addr, std::uint32_t size) {
    if (addr != 0x00200000U || size != raw.size())
        throw std::runtime_error("Incorrect live original vertex DMA range");
    return {raw.begin(), raw.end()};
}
void expect(bool result, const char* why) {
    if (!result) throw std::runtime_error(why);
}
}
int main() {
    // No commercial game shader data belongs in public tests.
    // Verify the original endian-word hashing contract on known plain words.
    const auto hash = dotm::original_xenos_word_hash({0x11223344U, 0xAABBCCDDU});
    std::uint64_t expected = 14695981039346656037ULL;
    for (auto b : {0x11U,0x22U,0x33U,0x44U,0xAAU,0xBBU,0xCCU,0xDDU})
        expected = (expected ^ b) * 1099511628211ULL;
    expect(hash == expected, "Original Xenos word hash lost BE byte order");
    dotm::GpuDrawCommand draw;
    draw.initiator = 0x00030088U;
    draw.primitive_type = 8;
    draw.source_select = 2;
    draw.major_mode = 0;
    draw.index_count = 3;
    draw.index_size_bytes = 2;
    draw.vertex_fetch_constants.push_back({0, 0x00200000U, 21U, 2U});
    draw.copy_physical_memory = &read_physical;
    // Only original private shaders are allowed to make it through this gate.
    expect(!dotm::is_original_first_xenos_draw(draw),
           "Unverified shader pair incorrectly accepted");
    draw.has_visibility_query_token = true;
    expect(!dotm::is_original_first_xenos_draw(draw),
           "Conditional query must not be silently accepted");
    draw.has_visibility_query_token = false;
    for (std::size_t v=0; v<3; ++v) {
        const float pos[]{float(v != 0), float(v == 2)};
        std::memcpy(raw.data()+v*28, pos, sizeof(pos));
    }
    // Decoder for fetch endian mode 2 swaps every physical 32-bit DWORD;
    // test the actual per-vertex buffer reader directly without guest shaders.
    std::array<std::uint8_t,84> decoded{};
    for (std::size_t i=0;i<84;++i) decoded[i]=raw[i ^ 3U];
    raw=decoded;
    const auto bytes = draw.copy_vertex_fetch_range(0, 0, 84);
    expect(bytes.size()==84, "Live original DMA read did not return 84 bytes");
    std::array<std::uint8_t,84> roundtrip{};
    for (std::size_t i=0;i<84;++i) roundtrip[i]=bytes[i ^ 3U];
    expect(roundtrip == raw, "Xenos endian lane correction failed");
    return 0;
}
