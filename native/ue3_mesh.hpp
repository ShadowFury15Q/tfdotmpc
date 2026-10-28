#pragma once
#include "ue3_package.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dotm {

// Unreal Engine 3 DOTM static-mesh collision kDOP tree.
// These are COLLISION data, not renderable vertices/UVs/materials.
struct StaticMeshCollisionNode {
    std::array<float, 3> minimum{};
    std::array<float, 3> maximum{};
    std::uint32_t first_child_or_triangle = 0;
    std::int16_t branch_or_first_triangle = 0;
    std::int16_t triangle_count_or_flag = 0;
};
struct StaticMeshCollisionTriangle {
    std::array<std::uint16_t, 3> vertex_indices{};
    std::uint16_t material_index = 0;
};
struct StaticMeshCollision {
    std::string object_path;
    std::vector<StaticMeshCollisionNode> nodes;
    std::vector<StaticMeshCollisionTriangle> triangles;
    std::uint32_t lod_count = 0;
    std::size_t lod_data_offset = 0; // relative to the serialized UObject, after the LOD count
};
// Throws if the export is not a StaticMesh, or if collision data are truncated.
// Geometry LOD vertex positions and faces are NOT decoded by this function.
StaticMeshCollision read_static_mesh_collision(const Ue3Package& pkg,
                                              std::size_t export_index);

// First 28 native bytes of USkeletalMesh::Serialize are FBoxSphereBounds.
struct MeshBounds {
    std::array<float,3> origin{};
    std::array<float,3> extent{};
    float sphere_radius = 0;
};
struct SkeletalMeshHeader {
    std::string object_path;
    MeshBounds bounds;
    std::size_t native_data_offset = 0;
};
// Bounds only; skeleton, skin weights, LOD indices and animations not decoded yet.
SkeletalMeshHeader read_skeletal_mesh_header(const Ue3Package& pkg,
                                             std::size_t export_index);
}