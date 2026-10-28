#include "ue3_lod.hpp"
#include "ue3_mesh.hpp"
#include <bit>
#include <cmath>
#include <limits>
#include <span>
#include <stdexcept>

namespace dotm {
namespace {
class Reader {
public:
 explicit Reader(std::span<const std::uint8_t> input,std::size_t offset)
     : b(input),p(offset){}
 std::uint32_t word(){
  need(4);
  auto x=(std::uint32_t(b[p])<<24)|(std::uint32_t(b[p+1])<<16)|
          (std::uint32_t(b[p+2])<<8)|b[p+3];
  p+=4;
  return x;
 }
 std::uint16_t half_word(){
  need(2);
  auto x=std::uint16_t((std::uint16_t(b[p])<<8)|b[p+1]);p+=2;return x;
 }
 float real(){
  const float f=std::bit_cast<float>(word());
  if(!std::isfinite(f))throw std::runtime_error("Non-finite Xbox static mesh vertex or UV");
  return f;
 }
 void skip(std::size_t n){need(n);p+=n;}
 void need(std::size_t n)const {
  if(p>b.size()||n>b.size()-p)throw std::runtime_error("Static mesh LOD0 buffer overrun");
 }
 std::size_t pos()const{return p;}
private:
 std::span<const std::uint8_t> b;
 std::size_t p;
};
struct Bulk {
 std::uint32_t item_bytes=0,count=0;
 std::size_t data_offset=0;
};
Bulk read_bulk(Reader& r,std::size_t max_items=1000000) {
 Bulk x;
 x.item_bytes=r.word();
 x.count=r.word();
 if(x.count>max_items||x.item_bytes>4096)
  throw std::runtime_error("Unreasonable static mesh bulk item dimensions");
 const std::size_t bytes=std::size_t(x.item_bytes)*x.count;
 x.data_offset=r.pos();
 r.skip(bytes);
 return x;
}
float half_to_float(std::uint16_t value){
 const int exponent=(value>>10)&31;
 const std::uint16_t fraction=value&1023;
 float answer;
 if(exponent==0)answer=std::ldexp(float(fraction),-24);
 else if(exponent==31)throw std::runtime_error("Invalid half-float static mesh UV");
 else answer=std::ldexp(1.0f+float(fraction)/1024.0f,exponent-15);
 return (value&0x8000)?-answer:answer;
}
}
StaticMeshLOD read_static_mesh_lod0(const Ue3Package& pkg,std::size_t index){
 auto collision=read_static_mesh_collision(pkg,index);
 if(collision.lod_count!=1)
  throw std::runtime_error("Only single-LOD DOTM static meshes are supported");
 const auto raw=pkg.serialized_object(index);
 Reader r(raw,collision.lod_data_offset);
 // Native DOTM FByteBulkData skip descriptor has five 32-bit fields.
 for(int i=0;i<5;++i)(void)r.word();
 const auto section_count=r.word();
 if(section_count>512)throw std::runtime_error("Unreasonable static mesh section count");
 StaticMeshLOD lod;
 for(std::uint32_t i=0;i<section_count;++i){
  StaticMeshSection section;
  section.material_object_ref=std::bit_cast<std::int32_t>(r.word());
  (void)r.word(); // flags field
  (void)r.word(); // flags field
  (void)r.word(); // cast shadow
  section.first_index=r.word();
  section.num_triangles=r.word();
  (void)r.word(); // min vertex
  (void)r.word(); // max vertex
  (void)r.word(); // section index
  const auto pairs=r.word();
  if(pairs>100000)throw std::runtime_error("Unreasonable DOTM section subentry count");
  r.skip(std::size_t(pairs)*8);
  lod.sections.push_back(section);
 }
 const auto vertex_stride=r.word(),vertex_count=r.word();
 const auto vertices=read_bulk(r);
 if(vertex_stride!=12||vertices.item_bytes!=12||vertex_count!=vertices.count)
  throw std::runtime_error("Unsupported DOTM static vertex stream layout");
 lod.vertices.resize(vertex_count);
 Reader vr(raw,vertices.data_offset);
 for(auto& v:lod.vertices)
  for(auto& f:v.position)f=vr.real();

 lod.uv_channels=r.word();
 const auto uv_stride=r.word(),uv_vertex_count=r.word(),float_precision=r.word();
 const auto uvs=read_bulk(r);
 if(lod.uv_channels==0||lod.uv_channels>8||float_precision>1||
    uv_vertex_count!=vertex_count||uvs.count!=vertex_count||
    uvs.item_bytes!=uv_stride||
    uv_stride!=12+lod.uv_channels*(float_precision?8u:4u))
  throw std::runtime_error("Unsupported DOTM static mesh UV stream");
 Reader ur(raw,uvs.data_offset);
 for(auto& vertex:lod.vertices){
  ur.skip(12); // original packed tangent/normal triplet; retained in source package
  for(std::uint32_t channel=0;channel<lod.uv_channels;++channel){
   auto& uv=vertex.uv[channel];
   uv[0]=float_precision?ur.real():half_to_float(ur.half_word());
   uv[1]=float_precision?ur.real():half_to_float(ur.half_word());
  }
 }
 const auto color_item=r.word(),color_count=r.word();
 if(color_count) {
  const auto colors=read_bulk(r);
  if(colors.count!=color_count||colors.item_bytes!=color_item)
   throw std::runtime_error("Invalid DOTM static mesh color stream");
 }
 const auto normals_item=r.word(),normals_count=r.word();
 if(normals_count) {
  const auto normals=read_bulk(r);
  if(normals.count!=normals_count||normals.item_bytes!=normals_item)
   throw std::runtime_error("Invalid DOTM static mesh auxiliary stream");
 }
 if(r.word()!=vertex_count)
  throw std::runtime_error("Static LOD vertex count differs from vertex stream");
 const auto indices=read_bulk(r,3000000);
 if(indices.item_bytes!=2&&indices.item_bytes!=4)
  throw std::runtime_error("Unexpected static mesh index element size");
 if(indices.count%3)
  throw std::runtime_error("Static mesh index count is not a triangle multiple");
 Reader ir(raw,indices.data_offset);
 lod.indices.reserve(indices.count);
 for(std::uint32_t i=0;i<indices.count;++i){
  std::uint32_t a=indices.item_bytes==2?ir.half_word():ir.word();
  if(a>=vertex_count)throw std::runtime_error("Out-of-range DOTM mesh triangle vertex");
  lod.indices.push_back(a);
 }
 const auto wireframe=read_bulk(r,3000000);
 (void)wireframe;
 (void)r.word(); // DOTM licensee >=58 extra index buffer field.
 std::uint64_t total_section_tris=0;
 for(const auto& section:lod.sections) {
  const auto last=std::uint64_t(section.first_index)+3ULL*section.num_triangles;
  if(last>lod.indices.size())throw std::runtime_error("Static mesh section exceeds index buffer");
  total_section_tris+=section.num_triangles;
 }
 if(total_section_tris!=lod.indices.size()/3)
  throw std::runtime_error("Not all rendered triangles are covered by sections");
 return lod;
}
}
