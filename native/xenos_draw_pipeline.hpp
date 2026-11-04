#pragma once
#include "gpu_engines.hpp"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <string>
#include <utility>
#include <vector>

namespace dotm {

// A draw identity is derived from the ACTUAL shader sequencer words submitted
// by original PM4, never from its draw ordinal. One shader pair may serve
// thousands of draws and frames with different guest vertex allocations.
inline std::uint64_t xenos_shader_hash(const std::vector<std::uint32_t>& words) {
    if (words.empty()) throw std::runtime_error("Missing original Xenos shader");
    std::uint64_t hash = 14695981039346656037ULL;
    for (auto word : words)
        for (unsigned shift : {24U, 16U, 8U, 0U})
            hash = (hash ^ ((word >> shift) & 255U)) * 1099511628211ULL;
    return hash;
}
inline std::string xenos_shader_hex(std::uint64_t hash) {
    std::ostringstream stream;
    stream << std::uppercase << std::hex << std::setfill('0') <<
        std::setw(16) << hash;
    return stream.str();
}
struct XenosShaderPair {
    std::uint64_t vertex = 0, pixel = 0;
    bool operator<(const XenosShaderPair& other) const noexcept {
        return std::tie(vertex, pixel) < std::tie(other.vertex, other.pixel);
    }
    bool operator==(const XenosShaderPair& other) const noexcept = default;
};
inline XenosShaderPair xenos_shader_pair(const GpuDrawCommand& draw) {
    return {xenos_shader_hash(draw.vertex_shader_words),
            xenos_shader_hash(draw.pixel_shader_words)};
}
inline std::filesystem::path xenos_shader_source(
        const std::filesystem::path& root, const char* stage,
        std::uint64_t hash) {
    return root / stage / (xenos_shader_hex(hash) + ".hlsl");
}
// Host IA topology codes intentionally match the supported subset of real
// Xenos primitive numbers. Rectangle-list needs its post-VS geometry stage.
enum class XenosHostTopology {
    point_list, line_list, line_strip, triangle_list,
    triangle_strip, rectangle_list
};
inline XenosHostTopology xenos_host_topology(std::uint32_t primitive) {
    switch (primitive) {
        case 1: return XenosHostTopology::point_list;
        case 2: return XenosHostTopology::line_list;
        case 3: return XenosHostTopology::line_strip;
        case 4: return XenosHostTopology::triangle_list;
        case 6: return XenosHostTopology::triangle_strip;
        case 8: return XenosHostTopology::rectangle_list;
        default: throw std::runtime_error("Unsupported original Xenos primitive topology");
    }
}
inline std::uint32_t xenos_minimum_primitive_vertices(
        XenosHostTopology kind) {
    switch (kind) {
        case XenosHostTopology::point_list: return 1;
        case XenosHostTopology::line_list:
        case XenosHostTopology::line_strip: return 2;
        case XenosHostTopology::triangle_list:
        case XenosHostTopology::triangle_strip:
        case XenosHostTopology::rectangle_list: return 3;
    }
    throw std::runtime_error("Unreachable Xenos topology");
}

enum class XenosInputFormat {
    float1, float2, float3, float4,
    unorm8x4, uint8x4, sint16x2, unorm16x2, float16x2,
    sint16x4, unorm16x4, float16x4
};
inline XenosInputFormat parse_xenos_input_format(const std::string& text) {
    static const std::map<std::string, XenosInputFormat> formats{
        {"float1", XenosInputFormat::float1},
        {"float2", XenosInputFormat::float2},
        {"float3", XenosInputFormat::float3},
        {"float4", XenosInputFormat::float4},
        {"unorm8x4", XenosInputFormat::unorm8x4},
        {"uint8x4", XenosInputFormat::uint8x4},
        {"sint16x2", XenosInputFormat::sint16x2},
        {"unorm16x2", XenosInputFormat::unorm16x2},
        {"float16x2", XenosInputFormat::float16x2},
        {"sint16x4", XenosInputFormat::sint16x4},
        {"unorm16x4", XenosInputFormat::unorm16x4},
        {"float16x4", XenosInputFormat::float16x4}
    };
    auto it = formats.find(text);
    if (it == formats.end())
        throw std::runtime_error("Unknown original Xenos input format: " + text);
    return it->second;
}
inline std::uint32_t xenos_input_format_bytes(XenosInputFormat f) {
    switch (f) {
        case XenosInputFormat::float1: return 4;
        case XenosInputFormat::float2: return 8;
        case XenosInputFormat::float3: return 12;
        case XenosInputFormat::float4: return 16;
        case XenosInputFormat::unorm8x4:
        case XenosInputFormat::uint8x4:
        case XenosInputFormat::sint16x2:
        case XenosInputFormat::unorm16x2:
        case XenosInputFormat::float16x2: return 4;
        case XenosInputFormat::sint16x4:
        case XenosInputFormat::unorm16x4:
        case XenosInputFormat::float16x4: return 8;
    }
    throw std::runtime_error("Unreachable original vertex format");
}
struct XenosVertexAttribute {
    std::uint32_t fetch_slot = 0;
    std::uint32_t stride = 0;
    std::string semantic;
    std::uint32_t semantic_index = 0;
    std::uint32_t byte_offset = 0;
    XenosInputFormat format{};
};
struct XenosVertexLayout {
    std::vector<XenosVertexAttribute> attributes;
    // Explicitly private, per-original-shader-pair reflection metadata.
    // This is a description of actual VFETCH semantics, NOT fake geometry.
    static XenosVertexLayout parse(std::istream& input) {
        XenosVertexLayout layout;
        std::string line;
        while (std::getline(input, line)) {
            auto first = line.find_first_not_of(" \t\r");
            if (first == std::string::npos || line[first] == '#') continue;
            std::istringstream fields(line.substr(first));
            std::string tag, format, extra;
            XenosVertexAttribute a;
            if (!(fields >> tag >> a.fetch_slot >> a.stride >> a.semantic >>
                    a.semantic_index >> a.byte_offset >> format) ||
                tag != "stream" || (fields >> extra))
                throw std::runtime_error("Invalid original Xenos VFETCH layout line");
            a.format = parse_xenos_input_format(format);
            if (a.fetch_slot >= 96 || a.stride == 0 || a.stride > 4096 ||
                a.semantic.empty() || a.semantic.size() > 32 ||
                a.semantic_index > 15 ||
                a.byte_offset + xenos_input_format_bytes(a.format) > a.stride)
                throw std::runtime_error("Original Xenos VFETCH layout is out of bounds");
            for (const auto& b : layout.attributes)
                if ((b.semantic == a.semantic &&
                     b.semantic_index == a.semantic_index) ||
                    (b.fetch_slot == a.fetch_slot && b.stride != a.stride))
                    throw std::runtime_error("Conflicting original Xenos vertex elements");
            layout.attributes.push_back(std::move(a));
            if (layout.attributes.size() > 32)
                throw std::runtime_error("Too many original Xenos vertex elements");
        }
        if (layout.attributes.empty())
            throw std::runtime_error("Original Xenos vertex layout is empty");
        return layout;
    }
    static XenosVertexLayout load(const std::filesystem::path& path) {
        std::ifstream stream(path);
        if (!stream) throw std::runtime_error("Original Xenos shader VFETCH reflection missing: " + path.string());
        return parse(stream);
    }
    static XenosVertexLayout original_first_rectangle() {
        std::istringstream text("stream 0 28 POSITION 0 0 float4\n"
                                "stream 0 28 COLOR 0 16 float3\n");
        return parse(text);
    }
};
struct XenosNativeVertexStream {
    std::uint32_t fetch_slot = 0;
    std::uint32_t stride = 0;
    std::vector<std::uint8_t> bytes;
};
struct XenosNativeDraw {
    XenosShaderPair shaders{};
    XenosHostTopology topology{};
    std::uint32_t count = 0;
    std::vector<std::uint32_t> indices;
    std::vector<XenosNativeVertexStream> streams;
};
// Snapshot a real PM4 draw while its physical memory reader is still valid.
// No guest pointer survives this operation. Unsupported Xenos features fail
// before acknowledging the ring (visibility query, instancing, tessellation).
inline XenosNativeDraw prepare_original_xenos_draw(
        const GpuDrawCommand& draw, const XenosVertexLayout& layout) {
    if (draw.has_visibility_query_token)
        throw std::runtime_error("Unresolved original Xenos visibility predicate");
    if (draw.major_mode != 0)
        throw std::runtime_error("Original Xenos major mode requires additional GPU support");
    XenosNativeDraw out;
    out.topology = xenos_host_topology(draw.primitive_type);
    out.count = draw.index_count;
    if (out.count < xenos_minimum_primitive_vertices(out.topology) ||
        out.count > 1'000'000U)
        throw std::runtime_error("Invalid original Xenos draw vertex count");
    if (out.topology == XenosHostTopology::rectangle_list &&
        out.count % 3U != 0)
        throw std::runtime_error("Original Xenos rectangle-list needs three vertices per rectangle");
    if (draw.source_select == 0) {
        if (draw.index_size_bytes != 2 && draw.index_size_bytes != 4)
            throw std::runtime_error("Invalid original Xenos index size");
        if (draw.decoded_indices.size() != out.count)
            throw std::runtime_error("Original Xenos index allocation unavailable");
        out.indices = draw.decoded_indices;
    } else if (draw.source_select != 2) {
        throw std::runtime_error("Unsupported original Xenos immediate or reserved index source");
    }
    // Indexed primitives may legally reuse a single vertex many times;
    // don't read the (index_count - 1)th vertex unless it is referenced.
    std::uint32_t max_index = out.indices.empty() ? out.count - 1 : 0;
    for (auto index : out.indices) {
        if (index >= 1'000'000U)
            throw std::runtime_error("Original Xenos index exceeds host limit");
        max_index = std::max(max_index, index);
    }
    // Shader pair is part of the plan, not the primitive number. This works
    // for consecutive original draws using different VS/PS combinations.
    out.shaders = xenos_shader_pair(draw);
    std::map<std::uint32_t, std::uint32_t> slots;
    for (const auto& a : layout.attributes)
        slots[a.fetch_slot] = a.stride;
    for (const auto& [slot, stride] : slots) {
        const auto fetched = std::find_if(
            draw.vertex_fetch_constants.begin(), draw.vertex_fetch_constants.end(),
            [slot](const XenosVertexFetchConstant& v) { return v.slot == slot; });
        if (fetched == draw.vertex_fetch_constants.end())
            throw std::runtime_error("Missing original Xenos vertex fetch slot");
        const auto required = (std::uint64_t(max_index) + 1U) * stride;
        const auto allocated = std::uint64_t(fetched->size_dwords) * 4U;
        if (required > allocated || required > 64U * 1024U * 1024U)
            throw std::runtime_error("Original Xenos vertex DMA exceeds shader binding");
        out.streams.push_back({
            slot, stride,
            draw.copy_vertex_fetch_range(slot, 0U,
                                         static_cast<std::uint32_t>(required))});
    }
    return out;
}
} // namespace dotm
