#include "guest_memory.hpp"
#include <cstdint>
#include <cstdio>
#include <exception>
#include <stdexcept>
namespace {
void unresolved_indirect(void*,std::uint8_t*) {
    throw std::runtime_error("Unresolved native DOTM function mapping");
}
}
int main(int argc,char**argv){
 if(argc!=2){std::fprintf(stderr,"Usage: dotm_pe_probe <decrypted Xbox 360 PE>\n");return 2;}
 try {
   dotm::GuestMemory m;
   m.load_pe(argv[1]);
   m.initialize_function_table(unresolved_indirect);
   auto* p=m.address(0x82000100U);
   if(p[0]!='P'||p[1]!='E') throw std::runtime_error("Invalid PE after loading");
   auto* o=p+24;
   std::uint32_t rva=std::uint32_t(o[16])|(std::uint32_t(o[17])<<8)|(std::uint32_t(o[18])<<16)|(std::uint32_t(o[19])<<24);
   std::printf("DOTM original native-mapped entry point: 0x%08X\n",dotm::GuestMemory::kImageBase+rva);
   std::puts("Only Xbox 360 PE and guest function table loaded. This is not a playable game.");
   return 0;
 }catch(const std::exception&e){std::fprintf(stderr,"Native loader error: %s\n",e.what());return 1;}
}