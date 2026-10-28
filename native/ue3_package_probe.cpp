#include "ue3_package.hpp"
#include <cstdio>
#include <exception>
int main(int argc, char**argv) {
 if(argc < 2) {std::fprintf(stderr,"Usage: dotm_ue3_probe <UE3 Xbox360 .xxx files...>\n");return 2;}
 int failed=0;
 for(int i=1;i<argc;++i) {
  try {
   const auto p=dotm::Ue3Package::load(argv[i]);
   const auto& q=p.info();
   std::printf("%s: UE3 version=%u licensee=%u names=%zu imports=%u exports=%u LZO chunks=%u blocks=%u\n",
               argv[i],q.version,q.licensee_version,p.names().size(),q.import_count,q.export_count,q.compressed_chunks,q.decompressed_blocks);
   for(const auto&name:p.imported_package_names())std::printf("  depends: %s\n",name.c_str());
  }catch(const std::exception&e){std::fprintf(stderr,"%s: %s\n",argv[i],e.what());++failed;}
 }
 return failed?1:0;
}
