#pragma once
#include "ue3_package.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dotm {
// DOTM Xbox 360 UE3 StaticMesh LOD0 renderable geometry.
// Vertex positions and UVs are in ORIGINAL game coordinates; no centering,
// reorientation, scale modification or texture-V flip is performed.
struct StaticMeshVertex {
    std::array<float, 3> position{};
    std::array<std::array<float, 2>, 8> uv{};
};
struct StaticMeshSection {
    std::int32_t material_object_ref = 0; // UE3 signed import/export reference
    std::uint32_t first_index = 0;
    std::uint32_t num_triangles = 0;
};
struct StaticMeshLOD {
    std::uint32_t uv_channels = 0;
    std::vector<StaticMeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<StaticMeshSection> sections;
};
// This is a separate reader from the collision kDOP tree.
// Currently supports the observed one-LOD DOTM 537/174 native serialization.
StaticMeshLOD read_static_mesh_lod0(const Ue3Package& package,std::size_t export_index);
}
