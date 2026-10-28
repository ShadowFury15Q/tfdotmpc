#include "ue3_package.hpp"
#include "ue3_dds.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>

namespace {
std::string clean_name(const std::string& in) {
 std::string name;
 for(unsigned char c:in) {
  if(std::isalnum(c)||c=='_'||c=='-'||c=='.')name.push_back(char(c));
  else name.push_back('_');
  if(name.size()>=96)break;
 }
 if(name.empty())return "unnamed";
 return name;
}
}
int main(int argc,char**argv) {
 if(argc<3) {
  std::fprintf(stderr,"Usage: dotm_dds_export <private-output-dir> <original-UE3-xxx-files...>\n");
  return 2;
 }
 const std::filesystem::path output(argv[1]);
 std::filesystem::create_directories(output);
 std::size_t written=0,skipped=0,failed=0;
 for(int i=2;i<argc;++i){
  try {
   const auto path=std::filesystem::path(argv[i]);
   const auto pkg=dotm::Ue3Package::load(path);
   for(std::size_t n=0;n<pkg.exports().size();++n){
    const auto&e=pkg.exports()[n];
    if(e.serial_size==0||pkg.export_class_path(n)!="Engine.Texture2D")continue;
    try {
     auto dds=dotm::export_ue3_texture_dds(pkg,n);
     if(!dds){++skipped;continue;}
     auto filename=clean_name(path.stem().string())+"__"+
      std::to_string(n)+"__"+clean_name(pkg.object_reference_path(std::int32_t(n+1)))+".dds";
     auto dest=output/filename;
     std::ofstream f(dest,std::ios::binary|std::ios::trunc);
     if(!f||!f.write(reinterpret_cast<const char*>(dds->data()),dds->size()))
      throw std::runtime_error("Failed to save DDS file");
     ++written;
     std::printf("DDS %s (%zu bytes)\n",dest.filename().string().c_str(),dds->size());
    }catch(const std::exception&err){
     ++failed;
     std::fprintf(stderr,"Texture export %s [%zu] failed: %s\n",argv[i],n,err.what());
    }
   }
  }catch(const std::exception&err){
   ++failed;
   std::fprintf(stderr,"Package %s failed: %s\n",argv[i],err.what());
  }
 }
 std::printf("DOTM DDS: exported=%zu skipped_nonresident_or_unsupported=%zu errors=%zu\n",
             written,skipped,failed);
 return failed?1:0;
}
