#include "guest_memory.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#else
#include <sys/mman.h>
#endif
namespace dotm {
namespace {
constexpr std::uint64_t page=4096;
std::uint64_t down(std::uint64_t x){return x&~(page-1);}
std::uint64_t up(std::uint64_t x){return (x+page-1)&~(page-1);}
std::uint16_t u16(std::span<const std::uint8_t> b,std::size_t p){
 if(p>b.size()||b.size()-p<2)throw std::runtime_error("Invalid PE u16");
 return std::uint16_t(b[p])|(std::uint16_t(b[p+1])<<8);
}
std::uint32_t u32(std::span<const std::uint8_t> b,std::size_t p){
 if(p>b.size()||b.size()-p<4)throw std::runtime_error("Invalid PE u32");
 return std::uint32_t(b[p])|(std::uint32_t(b[p+1])<<8)|(std::uint32_t(b[p+2])<<16)|(std::uint32_t(b[p+3])<<24);
}
std::vector<std::uint8_t> read(const std::filesystem::path& p){
 std::ifstream f(p,std::ios::binary|std::ios::ate);
 if(!f)throw std::runtime_error("Cannot open decrypted PE");
 auto n=f.tellg();if(n<0)throw std::runtime_error("Cannot size PE");
 std::vector<std::uint8_t>b(static_cast<std::size_t>(n));f.seekg(0);
 if(!f.read(reinterpret_cast<char*>(b.data()),n))throw std::runtime_error("Cannot read PE");
 return b;
}
}
GuestMemory::GuestMemory(){
#ifdef _WIN32
 base_=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,kAddressSpaceSize,MEM_RESERVE,PAGE_NOACCESS));
 if(!base_)throw std::runtime_error("VirtualAlloc reservation failed");
#else
 int flags=MAP_PRIVATE|MAP_ANONYMOUS;
#ifdef MAP_NORESERVE
 flags|=MAP_NORESERVE;
#endif
 void* p=mmap(nullptr,kAddressSpaceSize,PROT_NONE,flags,-1,0);
 if(p==MAP_FAILED)throw std::runtime_error("mmap reservation failed");
 base_=static_cast<std::uint8_t*>(p);
#endif
}
GuestMemory::~GuestMemory(){if(!base_)return;
#ifdef _WIN32
 VirtualFree(base_,0,MEM_RELEASE);
#else
 munmap(base_,kAddressSpaceSize);
#endif
}
void GuestMemory::commit(std::uint64_t a,std::size_t length){
 if(!length)return;
 if(a>=kAddressSpaceSize||length>kAddressSpaceSize-a)throw std::out_of_range("Guest memory bounds");
 auto begin=down(a),end=up(a+length);
#ifdef _WIN32
 if(!VirtualAlloc(base_+begin,static_cast<SIZE_T>(end-begin),MEM_COMMIT,PAGE_READWRITE))throw std::runtime_error("Guest commit failed");
#else
 if(mprotect(base_+begin,end-begin,PROT_READ|PROT_WRITE)!=0)throw std::runtime_error("Guest commit failed");
#endif
}
void GuestMemory::copy_to_guest(std::uint32_t a,std::span<const std::uint8_t> b){
 if(b.empty())return;commit(a,b.size());std::memcpy(address(a),b.data(),b.size());
}
void GuestMemory::load_pe(const std::filesystem::path& path){
 const auto v=read(path);std::span<const std::uint8_t>b(v);
 if(v.size()<512||u16(b,0)!=0x5a4d)throw std::runtime_error("Expected decrypted Xbox 360 PE");
 auto nt=std::size_t(u32(b,0x3c));
 if(u32(b,nt)!=0x4550||u16(b,nt+4)!=0x01F2)throw std::runtime_error("Not an Xbox 360 PPC PE");
 std::size_t opt=nt+24; std::size_t sections=opt+u16(b,nt+20); std::size_t count=u16(b,nt+6);
 if(u16(b,opt)!=0x10b||u32(b,opt+28)!=kImageBase)throw std::runtime_error("Unexpected image base");
 auto header=u32(b,opt+60);
 if(header>v.size()||header>kImageSize)throw std::runtime_error("Invalid PE headers");
 copy_to_guest(kImageBase,b.first(header));
 for(std::size_t i=0;i<count;i++){
  auto sh=sections+40*i;
  if(sh>v.size()||v.size()-sh<40)throw std::runtime_error("Truncated section");
  std::string name(reinterpret_cast<const char*>(v.data()+sh),8);
  name.resize(std::min<std::size_t>(8,name.find('\0')));
  // PE .reloc extends into XenonRecomp's native indirect-call table.
  // Guest image keeps its original base, so relocations are unnecessary.
  if(name==".reloc")continue;
  auto virtual_size=u32(b,sh+8),rva=u32(b,sh+12),raw=u32(b,sh+16),file_off=u32(b,sh+20);
  auto len=std::max(virtual_size,raw);
  if(rva>=kImageSize||len>kImageSize-rva)throw std::runtime_error("PE section intersects native dispatch table");
  if(raw&&(file_off>v.size()||raw>v.size()-file_off))throw std::runtime_error("Invalid section raw size");
  if(len)commit(std::uint64_t(kImageBase)+rva,len);
  if(raw)std::memcpy(address(kImageBase+rva),v.data()+file_off,raw);
 }
}
void GuestMemory::initialize_function_table(NativeFunction fallback){
 if(!fallback)throw std::invalid_argument("Null fallback");
 static_assert(sizeof(NativeFunction)==8);
 static_assert(kFunctionTableOffset+kFunctionTableSize<kAddressSpaceSize);
 commit(kFunctionTableOffset,kFunctionTableSize);
 for(std::uint64_t i=0;i+8<=kFunctionTableSize;i+=8)
  std::memcpy(base_+kFunctionTableOffset+i,&fallback,sizeof(fallback));
 table_initialized_=true;
}
void GuestMemory::install_functions(std::span<const FunctionEntry> entries){
 if(!table_initialized_)throw std::logic_error("Uninitialized function mapping");
 for(auto e:entries){
  if(e.guest_address<kCodeBase||std::uint64_t(e.guest_address)-kCodeBase>=kCodeSize||(e.guest_address&3)||!e.native_function)
   throw std::invalid_argument("Invalid PPC function address");
  auto a=kFunctionTableOffset+2ULL*(e.guest_address-kCodeBase);
  std::memcpy(base_+a,&e.native_function,sizeof(e.native_function));
 }
}
NativeFunction GuestMemory::find_function(std::uint32_t a)const{
 if(!table_initialized_||a<kCodeBase||std::uint64_t(a)-kCodeBase>=kCodeSize||(a&3))
  throw std::invalid_argument("Invalid PPC lookup");
 NativeFunction f=nullptr;auto offset=kFunctionTableOffset+2ULL*(a-kCodeBase);
 std::memcpy(&f,base_+offset,sizeof(f));return f;
}
}