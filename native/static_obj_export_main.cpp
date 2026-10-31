#include "ue3_package.hpp"
#include "ue3_lod.hpp"
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <string>
#include <cctype>
#include <cstdint>

namespace {
std::string safe(std::string text){
 for(auto&ch:text)if(!std::isalnum(static_cast<unsigned char>(ch))&&ch!='_'&&ch!='-')ch='_';
 if(text.size()>88)text.resize(88);
 if(text.empty())text="mesh";
 return text;
}
void write_obj(const std::filesystem::path&dest,const std::string&name,
               const dotm::StaticMeshLOD&lod){
 std::ofstream out(dest,std::ios::trunc);
 if(!out)throw std::runtime_error("Cannot write mesh OBJ file");
 out<<std::setprecision(std::numeric_limits<float>::max_digits10);
 out<<"# Original DOTM Xbox360 StaticMesh LOD0; no recentering, rescaling or rotations\n";
 out<<"# Original material names, normals and skeletal skinning not yet exported\n";
 out<<"o "<<safe(name)<<"\n";
 for(const auto& v:lod.vertices)
   out<<"v "<<v.position[0]<<" "<<v.position[1]<<" "<<v.position[2]<<"\n";
 for(const auto& v:lod.vertices)
   out<<"vt "<<v.uv[0][0]<<" "<<v.uv[0][1]<<"\n";
 for(std::size_t i=0;i<lod.sections.size();++i){
   const auto&s=lod.sections[i];
   out<<"g section_"<<i<<"_material_reference_"<<s.material_object_ref<<"\n";
   for(std::uint32_t face=0;face<s.num_triangles;++face){
     const auto offset=std::size_t(s.first_index)+std::size_t(face)*3;
     out<<"f";
     for(int k=0;k<3;++k){
       const auto v=lod.indices.at(offset+k)+1;
       out<<" "<<v<<"/"<<v;
     }
     out<<"\n";
   }
 }
 if(!out)throw std::runtime_error("Failed while writing mesh OBJ");
}
}
int main(int argc,char**argv){
 if(argc<3){
  std::fprintf(stderr,"Usage: dotm_static_obj_export <private-output-folder> <original-Xbox360-.xxx-files...>\n");
  return 2;
 }
 const std::filesystem::path out(argv[1]);
 std::filesystem::create_directories(out);
 std::size_t count=0,verts=0,faces=0,failures=0;
 for(int i=2;i<argc;++i){
  try{
    auto path=std::filesystem::path(argv[i]);
    auto package=dotm::Ue3Package::load(path);
    for(std::size_t n=0;n<package.exports().size();++n){
      if(package.exports()[n].serial_size==0||
         package.export_class_path(n)!="Engine.StaticMesh")continue;
      try{
        auto lod=dotm::read_static_mesh_lod0(package,n);
        const auto name=package.object_reference_path(std::int32_t(n+1));
        auto file=safe(path.stem().string())+"__"+std::to_string(n)+"__"+safe(name)+".obj";
        write_obj(out/file,name,lod);
        ++count;verts+=lod.vertices.size();faces+=lod.indices.size()/3;
        std::printf("OBJ %s vertices=%zu triangles=%zu uv_channels=%u\n",
                    file.c_str(),lod.vertices.size(),lod.indices.size()/3,lod.uv_channels);
      }catch(const std::exception&err){
        ++failures;std::fprintf(stderr,"Mesh %zu in %s: %s\n",n,argv[i],err.what());
      }
    }
  }catch(const std::exception&err){
    ++failures;std::fprintf(stderr,"Package %s: %s\n",argv[i],err.what());
  }
 }
 std::printf("DOTM NATIVE OBJ: meshes=%zu vertices=%zu triangles=%zu errors=%zu\n",
             count,verts,faces,failures);
 return failures?1:0;
}
