#include "ue3_mesh.hpp"
#include "ue3_lod.hpp"
#include <algorithm>
#include <cstdint>
#include <bit>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
void store(std::vector<std::uint8_t>&d,std::size_t p,std::uint32_t n) {
 if(d.size()<p+4)d.resize(p+4);
 d[p]=n>>24;d[p+1]=n>>16;d[p+2]=n>>8;d[p+3]=n;
}
void add(std::vector<std::uint8_t>&d,std::uint32_t n){store(d,d.size(),n);}
void name(std::vector<std::uint8_t>&d,const std::string&s){
 add(d,std::uint32_t(s.size()+1));
 d.insert(d.end(),s.begin(),s.end());d.push_back(0);d.insert(d.end(),8,0);
}
void import(std::vector<std::uint8_t>&d,std::uint32_t classp,std::uint32_t classname,
            std::int32_t outer,std::uint32_t object){
 for(auto n:{classp,0u,classname,0u,std::uint32_t(outer),object,0u})add(d,n);
}
void export_record(std::vector<std::uint8_t>&d,std::int32_t cls,std::uint32_t name_idx) {
 for(auto n:{std::uint32_t(cls),0u,0u,name_idx,0u,0u,0u,0u,0u,0u,0u})add(d,n);
 d.push_back(0);
}
std::vector<std::uint8_t> fixture(bool invalid=false) {
 std::vector<std::uint8_t>d(113,0);
 store(d,0,0x9E2A83C1u);store(d,4,0x00AE0219u);store(d,16,5);
 std::copy_n("None",4,d.data()+20);
 store(d,29,7);store(d,33,113);
 for(auto text:{"None","Core","Package","Engine","Class","StaticMesh","SkeletalMesh"})
  name(d,text);
 const auto imports=std::uint32_t(d.size());
 import(d,1,2,0,3);
 import(d,1,4,-1,5);
 import(d,1,4,-1,6);
 store(d,45,3);store(d,49,imports);
 const auto exports=std::uint32_t(d.size());
 export_record(d,-2,5);export_record(d,-3,6);
 store(d,37,2);store(d,41,exports);store(d,53,std::uint32_t(d.size()));
 const auto first=std::uint32_t(d.size());
 add(d,0);add(d,0);add(d,0); // net idx, FName None/0
 add(d,32);add(d,1); // collision tree element stride, count
 for(float f:{-1.f,-2.f,-3.f,1.f,2.f,3.f})add(d,std::bit_cast<std::uint32_t>(f));
 add(d,7);add(d,0x00020003); // children/leaf info
 add(d,8);add(d,1);  // kDOP triangles stride + count
 d.insert(d.end(),{0,0,0,1,0,2,0,4});
 add(d,1); // 1 rendering LOD.
 // DOTM FByteBulkData five uint32 fields (empty).
 for(int i=0;i<5;++i)add(d,0);
 add(d,1); // 1 material section
 for(auto v:{0u,1u,1u,1u,0u,1u,0u,2u,0u})add(d,v);
 add(d,1);add(d,0);add(d,1); // Section's one metadata pair.
 add(d,12);add(d,3); // vertex item stride, number of vertices
 add(d,12);add(d,3); // vertex bulk item stride, number of vertices
 for(float v:{0.f,0.f,0.f, 1.f,0.f,0.f, 0.f,1.f,0.f})
   add(d,std::bit_cast<std::uint32_t>(v));
 add(d,1);add(d,16);add(d,3);add(d,0); // UV channel count/stride/verts/half
 add(d,16);add(d,3); // UV bulk stride, count
 for(auto uv : {0x00000000u,0x3C000000u,0x00003C00u}) {
   add(d,0);add(d,0);add(d,0); // packed normals
   add(d,uv); // two big-endian half-float UVs
 }
 add(d,0);add(d,0); // no ColorStream2.
 add(d,4);add(d,0); // no extra normal stream.
 add(d,3); // NumVerts
 add(d,2);add(d,3); // 16-bit indices, three entries
 d.insert(d.end(),{0,0,0,1,0,2});
 add(d,0);add(d,0); // no secondary indices
 add(d,0); // trailing DOTM index field.
 store(d,exports+32,std::uint32_t(d.size())-first);
 store(d,exports+36,first);
 const auto second=std::uint32_t(d.size());
 add(d,0);add(d,0);add(d,0);
 for(float f:{1.f,2.f,3.f,4.f,5.f,6.f,7.f})add(d,std::bit_cast<std::uint32_t>(f));
 store(d,exports+45+32,std::uint32_t(d.size())-second);
 store(d,exports+45+36,second);
 store(d,12,std::uint32_t(d.size()));
 if(invalid)store(d,first+12,99); // invalid kDOP stride should be rejected
 return d;
}
void check(bool x,const char*message){if(!x)throw std::runtime_error(message);}
}
int main(){
 try{
  const auto pkg=dotm::Ue3Package::parse(fixture());
  const auto stat=dotm::read_static_mesh_collision(pkg,0);
  check(stat.nodes.size()==1 && stat.triangles.size()==1 && stat.lod_count==1,
        "Synthetic DOTM static collision structures");
  check(stat.nodes[0].minimum[0]==-1.f && stat.nodes[0].maximum[2]==3.f,
        "Big-endian kDOP box floats");
  check(stat.nodes[0].first_child_or_triangle==7 &&
        stat.nodes[0].branch_or_first_triangle==2 &&
        stat.nodes[0].triangle_count_or_flag==3,"kDOP leaf fields");
  check(stat.triangles[0].vertex_indices[0]==0 &&
        stat.triangles[0].vertex_indices[2]==2 &&
        stat.triangles[0].material_index==4,"Collision triangle indexing");
  const auto lod=dotm::read_static_mesh_lod0(pkg,0);
  check(lod.vertices.size()==3&&lod.indices.size()==3&&lod.sections.size()==1,
        "DOTM synthetic renderable LOD0 geometry");
  check(lod.vertices[1].position[0]==1.f&&lod.vertices[2].position[1]==1.f,
        "Real vertex XYZ positions");
  check(lod.vertices[1].uv[0][0]==1.f&&lod.vertices[2].uv[0][1]==1.f,
        "Xbox half-precision UV0 coordinates");
  check(lod.indices[0]==0&&lod.indices[1]==1&&lod.indices[2]==2,
        "Triangle index order");
  check(lod.sections[0].num_triangles==1,"Material section coverage");
  const auto skel=dotm::read_skeletal_mesh_header(pkg,1);
  check(skel.bounds.origin[0]==1.f && skel.bounds.extent[1]==5.f &&
        skel.bounds.sphere_radius==7.f,"Skeletal FBoxSphereBounds floats");
  bool failed=false;
  try{const auto corrupt=dotm::Ue3Package::parse(fixture(true));
      (void)dotm::read_static_mesh_collision(corrupt,0);}
  catch(const std::runtime_error&){failed=true;}
  check(failed,"Corrupted collision layout rejected");
  std::cout<<"DOTM synthetic mesh bounds and kDOP structures PASS\n";
  return 0;
 }catch(const std::exception&exc){std::cerr<<"FAIL: "<<exc.what()<<"\n";return 1;}
}
