#include "ue3_mesh.hpp"
#include <cstdio>
#include <exception>
#include <filesystem>
#include <stdexcept>

int main(int argc,char**argv){
 if(argc<2){
    std::fprintf(stderr,"Usage: dotm_mesh_probe <original-xxx-files...>\n");
    return 2;
 }
 std::size_t static_meshes=0,skeletal_meshes=0,nodes=0,collision_triangles=0,lods=0,errors=0;
 for(int i=1;i<argc;++i) {
  try{
    const auto pkg=dotm::Ue3Package::load(argv[i]);
    for(std::size_t n=0;n<pkg.exports().size();++n){
      if(pkg.exports()[n].serial_size==0)continue;
      try {
        auto cls=pkg.export_class_path(n);
        if(cls=="Engine.StaticMesh"){
          auto mesh=dotm::read_static_mesh_collision(pkg,n);
          ++static_meshes;
          nodes+=mesh.nodes.size();
          collision_triangles+=mesh.triangles.size();
          lods+=mesh.lod_count;
          std::printf("STATIC %s: collision_nodes=%zu collision_triangles=%zu LODs=%u\n",
                      mesh.object_path.c_str(),mesh.nodes.size(),mesh.triangles.size(),mesh.lod_count);
        }else if(cls=="Engine.SkeletalMesh"){
          auto skel=dotm::read_skeletal_mesh_header(pkg,n);
          ++skeletal_meshes;
          std::printf("SKELETAL %s: origin=(%.2f %.2f %.2f) extents=(%.2f %.2f %.2f) radius=%.2f\n",
                      skel.object_path.c_str(),
                      skel.bounds.origin[0],skel.bounds.origin[1],skel.bounds.origin[2],
                      skel.bounds.extent[0],skel.bounds.extent[1],skel.bounds.extent[2],
                      skel.bounds.sphere_radius);
        }
      }catch(const std::exception&exc){
        ++errors;
        std::fprintf(stderr,"Object %zu in %s: %s\n",n,argv[i],exc.what());
      }
    }
  }catch(const std::exception&exc){
    ++errors;std::fprintf(stderr,"Package %s: %s\n",argv[i],exc.what());
  }
 }
 std::printf("DOTM MESHES: static=%zu skeletal=%zu collision_nodes=%zu collision_triangles=%zu LODs=%zu errors=%zu\n",
             static_meshes,skeletal_meshes,nodes,collision_triangles,lods,errors);
 return errors?1:0;
}
