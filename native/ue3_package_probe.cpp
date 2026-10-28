#include "ue3_package.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <stdexcept>

int main(int argc, char** argv) {
 const bool verify_all = argc > 1 && std::strcmp(argv[1], "--verify-all") == 0;
 const int first = verify_all ? 2 : 1;
 if (argc <= first) {
  std::fprintf(stderr, "Usage: dotm_ue3_probe [--verify-all] <UE3 Xbox360 .xxx files...>\n");
  return 2;
 }
 std::size_t total_exports = 0;
 std::size_t validated_refs = 0;
 std::size_t errors = 0;
 for (int i = first; i < argc; ++i) {
  try {
   const auto p = dotm::Ue3Package::load(argv[i]);
   const auto& q = p.info();
   std::printf("%s: UE3 version=%u licensee=%u names=%zu imports=%u exports=%u LZO chunks=%u blocks=%u\n",
               argv[i], q.version, q.licensee_version, p.names().size(), q.import_count,
               q.export_count, q.compressed_chunks, q.decompressed_blocks);
   total_exports += p.exports().size();
   if (!verify_all) {
    for (const auto& name : p.imported_package_names())
     std::printf("  depends: %s\n", name.c_str());
    const auto limit = std::min<std::size_t>(p.exports().size(), 5);
    for (std::size_t n = 0; n < limit; ++n) {
     const auto& e = p.exports()[n];
     std::printf("  export[%zu] %s class=%s serialized=%u bytes @0x%X\n",
         n, p.object_reference_path(std::int32_t(n+1)).c_str(),
         p.export_class_path(n).c_str(), e.serial_size, e.serial_offset);
    }
   } else {
    for (std::size_t n = 0; n < p.exports().size(); ++n) {
     try {
      const auto& e = p.exports()[n];
      const auto path = p.object_reference_path(std::int32_t(n+1));
      const auto class_path = p.export_class_path(n);
      const auto bytes = p.serialized_object(n);
      if (path.empty() || class_path.empty() || bytes.size() != e.serial_size)
       throw std::runtime_error("Mismatched export path/class/serial span");
      ++validated_refs;
     } catch (const std::exception& exc) {
      ++errors;
      if (errors <= 10)
       std::fprintf(stderr, "INVALID export[%zu] in %s: %s\n",n,argv[i],exc.what());
     }
    }
   }
  } catch(const std::exception& e) {
   ++errors;
   std::fprintf(stderr, "%s: %s\n",argv[i],e.what());
  }
 }
 if (verify_all) {
  std::printf("UE3 DOTM export validation: total=%zu references_checked=%zu failures=%zu\n",
              total_exports,validated_refs,errors);
 }
 return errors ? 1 : 0;
}
