#include "ue3_mesh.hpp"
#include "ue3_properties.hpp"
#include <bit>
#include <cmath>
#include <span>
#include <stdexcept>

namespace dotm {
namespace {
void require(std::span<const std::uint8_t> s,std::size_t offset,std::size_t n) {
    if(offset>s.size()||n>s.size()-offset)
        throw std::runtime_error("DOTM mesh serialization out of bounds");
}
std::uint16_t be16(std::span<const std::uint8_t> s,std::size_t p){
    require(s,p,2);
    return (std::uint16_t(s[p])<<8)|s[p+1];
}
std::uint32_t be32(std::span<const std::uint8_t> s,std::size_t p){
    require(s,p,4);
    return (std::uint32_t(s[p])<<24)|(std::uint32_t(s[p+1])<<16)|
           (std::uint32_t(s[p+2])<<8)|s[p+3];
}
float bef32(std::span<const std::uint8_t> s,std::size_t p){
    return std::bit_cast<float>(be32(s,p));
}
}
StaticMeshCollision read_static_mesh_collision(const Ue3Package&pkg,std::size_t idx){
    if(pkg.export_class_path(idx)!="Engine.StaticMesh")
        throw std::invalid_argument("Export is not Engine.StaticMesh");
    const auto props=parse_ue3_tagged_properties(pkg,idx);
    auto data=pkg.serialized_object(idx);
    std::size_t p=props.native_data_offset;
    StaticMeshCollision result;
    result.object_path=pkg.object_reference_path(std::int32_t(idx+1));
    require(data,p,8);
    const auto node_stride=be32(data,p),node_count=be32(data,p+4);
    p+=8;
    if(node_stride!=32||node_count>100000)
        throw std::runtime_error("Unsupported DOTM kDOP collision node geometry");
    require(data,p,std::size_t(node_count)*32);
    result.nodes.reserve(node_count);
    for(std::uint32_t i=0;i<node_count;++i,p+=32){
        StaticMeshCollisionNode n;
        for(std::size_t a=0;a<3;++a){
            n.minimum[a]=bef32(data,p+4*a);
            n.maximum[a]=bef32(data,p+12+4*a);
            if(!std::isfinite(n.minimum[a])||!std::isfinite(n.maximum[a]))
                throw std::runtime_error("Nonfinite DOTM collision node bounds");
        }
        // Some original game kDOP nodes are empty sentinel boxes with min > max:
        // preserve these exact values instead of discarding or "fixing" them.
        n.first_child_or_triangle=be32(data,p+24);
        n.branch_or_first_triangle=std::bit_cast<std::int16_t>(be16(data,p+28));
        n.triangle_count_or_flag=std::bit_cast<std::int16_t>(be16(data,p+30));
        result.nodes.push_back(n);
    }
    require(data,p,8);
    const auto tri_stride=be32(data,p),tri_count=be32(data,p+4);
    p+=8;
    if(tri_stride!=8||tri_count>1000000)
        throw std::runtime_error("Unsupported DOTM kDOP collision triangles");
    require(data,p,std::size_t(tri_count)*8);
    result.triangles.reserve(tri_count);
    for(std::uint32_t i=0;i<tri_count;++i,p+=8){
        StaticMeshCollisionTriangle t;
        for(std::size_t a=0;a<3;++a)t.vertex_indices[a]=be16(data,p+a*2);
        t.material_index=be16(data,p+6);
        result.triangles.push_back(t);
    }
    require(data,p,4);
    result.lod_count=be32(data,p);
    p+=4;
    if(result.lod_count>32)
        throw std::runtime_error("Unreasonable DOTM static mesh LOD count");
    result.lod_data_offset=p;
    return result;
}
SkeletalMeshHeader read_skeletal_mesh_header(const Ue3Package&pkg,std::size_t idx){
    if(pkg.export_class_path(idx)!="Engine.SkeletalMesh")
        throw std::invalid_argument("Export is not Engine.SkeletalMesh");
    const auto props=parse_ue3_tagged_properties(pkg,idx);
    const auto data=pkg.serialized_object(idx);
    std::size_t p=props.native_data_offset;
    require(data,p,28);
    SkeletalMeshHeader header;
    header.object_path=pkg.object_reference_path(std::int32_t(idx+1));
    header.native_data_offset=p;
    for(std::size_t a=0;a<3;++a) {
        header.bounds.origin[a]=bef32(data,p+a*4);
        header.bounds.extent[a]=bef32(data,p+12+a*4);
        if(!std::isfinite(header.bounds.origin[a])||
           !std::isfinite(header.bounds.extent[a])||
           header.bounds.extent[a]<0)
            throw std::runtime_error("Invalid DOTM skeletal mesh bounds");
    }
    header.bounds.sphere_radius=bef32(data,p+24);
    if(!std::isfinite(header.bounds.sphere_radius)||header.bounds.sphere_radius<0)
        throw std::runtime_error("Invalid skeletal bounding sphere");
    return header;
}
}