#include "ue3_package.hpp"
#include "ue3_properties.hpp"
#include "ue3_texture.hpp"
#include "ue3_mip.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <map>
#include <stdexcept>
#include <string_view>

namespace {
bool gameplay_asset_class(std::string_view cls){
 for(const auto& name:{
    "Engine.Texture2D","Engine.Material","Engine.MaterialInstanceConstant",
    "Engine.StaticMesh","Engine.SkeletalMesh","Engine.SoundCue",
    "Engine.World","Engine.Level","Engine.ParticleSystem","Engine.Font",
    "GFxUI.SwfMovie"}) {
  if(cls==name)return true;
 }
 return false;
}
}
int main(int argc, char** argv) {
 const bool verify_all=argc>1&&std::strcmp(argv[1],"--verify-all")==0;
 const bool verify_tags=argc>1&&std::strcmp(argv[1],"--verify-tags")==0;
 const bool verify_textures=argc>1&&std::strcmp(argv[1],"--verify-textures")==0;
 const bool verify_mips=argc>1&&std::strcmp(argv[1],"--verify-mips")==0;
 const int first=verify_all||verify_tags||verify_textures||verify_mips?2:1;
 if(argc<=first) {
  std::fprintf(stderr,"Usage: dotm_ue3_probe [--verify-all|--verify-tags|--verify-textures|--verify-mips] <UE3 Xbox360 .xxx files...>\n");
  return 2;
 }
 std::size_t total_exports=0,validated_refs=0,tagged_assets=0,tag_count=0,failures=0,textures=0;
 std::size_t mip_textures=0,mip_levels=0,mip_inline=0,mip_lzo=0,mip_external=0,mip_absent=0;
 std::uint64_t loaded_mip_bytes=0;
 std::map<std::string,std::size_t> format_histogram;
 for(int i=first;i<argc;++i){
  try {
   const auto pkg=dotm::Ue3Package::load(argv[i]);
   const auto& q=pkg.info();
   std::printf("%s: names=%zu imports=%u exports=%u LZO chunks=%u blocks=%u\n",
               argv[i],pkg.names().size(),q.import_count,q.export_count,q.compressed_chunks,q.decompressed_blocks);
   total_exports+=pkg.exports().size();
   if(!verify_all&&!verify_tags&&!verify_textures&&!verify_mips) {
    for(const auto&name:pkg.imported_package_names())std::printf("  depends: %s\n",name.c_str());
    for(std::size_t n=0;n<std::min<std::size_t>(pkg.exports().size(),5);++n) {
      const auto& e=pkg.exports()[n];
      std::printf("  export[%zu] %s class=%s bytes=%u\n",n,
          pkg.object_reference_path(std::int32_t(n+1)).c_str(),
          pkg.export_class_path(n).c_str(),e.serial_size);
    }
   }
   if(verify_all||verify_tags||verify_textures||verify_mips)for(std::size_t n=0;n<pkg.exports().size();++n){
    try {
     const auto& e=pkg.exports()[n];
     if(verify_mips){
      if(e.serial_size==0||pkg.export_class_path(n)!="Engine.Texture2D")continue;
      const auto chain=dotm::parse_ue3_texture_mips(pkg,n);
      ++mip_textures;
      mip_levels+=chain.size();
      for(const auto& m:chain){
       switch(m.storage){
        case dotm::Ue3MipStorage::Embedded:
         ++mip_inline;loaded_mip_bytes+=dotm::read_ue3_texture_mip_bytes(pkg,n,m).size();break;
        case dotm::Ue3MipStorage::LzoEmbedded:
         ++mip_lzo;loaded_mip_bytes+=dotm::read_ue3_texture_mip_bytes(pkg,n,m).size();break;
        case dotm::Ue3MipStorage::External:++mip_external;break;
        case dotm::Ue3MipStorage::Absent:++mip_absent;break;
       }
      }
     }else if(verify_textures){
      if(e.serial_size==0||pkg.export_class_path(n)!="Engine.Texture2D")continue;
      const auto tx=dotm::inspect_ue3_texture2d(pkg,n);
      ++textures;
      ++format_histogram[tx.format];
     }else if(verify_tags) {
      const auto class_path=pkg.export_class_path(n);
      if(!gameplay_asset_class(class_path)||e.serial_size==0)continue;
      const auto stream=dotm::parse_ue3_tagged_properties(pkg,n);
      const auto native=pkg.serialized_object(n);
      if(stream.native_data_offset>native.size())
        throw std::runtime_error("Native object data out of bounds");
      for(const auto&tag:stream.properties) {
       if(dotm::ue3_property_payload(pkg,n,tag).size()!=tag.serialized_size)
        throw std::runtime_error("Tag serialized length mismatch");
       ++tag_count;
      }
      ++tagged_assets;
     }else{
      auto path=pkg.object_reference_path(std::int32_t(n+1));
      auto class_path=pkg.export_class_path(n);
      auto native=pkg.serialized_object(n);
      if(path.empty()||class_path.empty()||native.size()!=e.serial_size)
        throw std::runtime_error("Invalid UObject reference");
      ++validated_refs;
     }
    }catch(const std::exception&exc){
      ++failures;
      if(failures<=10)std::fprintf(stderr,"Invalid object %zu in %s: %s\n",n,argv[i],exc.what());
    }
   }
  }catch(const std::exception&exc){
   ++failures;
   std::fprintf(stderr,"%s: %s\n",argv[i],exc.what());
  }
 }
 if(verify_all)std::printf("References: exports=%zu checked=%zu failures=%zu\n",
                            total_exports,validated_refs,failures);
 if(verify_tags)std::printf("Tagged game assets: exports=%zu checked=%zu properties=%zu failures=%zu\n",
                            total_exports,tagged_assets,tag_count,failures);
 if(verify_textures){
  std::printf("DOTM Texture2D assets: exports=%zu checked=%zu failures=%zu\n",
              total_exports,textures,failures);
  for(const auto& [format,count]:format_histogram)
   std::printf("  %s: %zu\n",format.c_str(),count);
 }
 if(verify_mips)std::printf("DOTM mipmaps: textures=%zu levels=%zu inline=%zu LZO=%zu external=%zu absent=%zu loaded_bytes=%llu failures=%zu\n",
    mip_textures,mip_levels,mip_inline,mip_lzo,mip_external,mip_absent,
    (unsigned long long)loaded_mip_bytes,failures);
 return failures?1:0;
}
