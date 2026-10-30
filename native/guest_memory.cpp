#include "guest_memory.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
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
 for(std::uint64_t p=begin/page;p<end/page;++p)
  committed_pages_[p/64]|=(std::uint64_t{1}<<(p%64));
}
void GuestMemory::decommit(std::uint64_t a,std::size_t length){
 if(!length)return;
 if(a>=kAddressSpaceSize||length>kAddressSpaceSize-a)throw std::out_of_range("Guest memory bounds");
 const auto begin=down(a),end=up(a+length);
#ifdef _WIN32
 if(!VirtualFree(base_+begin,static_cast<SIZE_T>(end-begin),MEM_DECOMMIT))
  throw std::runtime_error("Guest decommit failed");
#else
 // Discard old bytes before revoking access. Never update page tracking
 // unless both host operations have succeeded.
 if(madvise(base_+begin,end-begin,MADV_DONTNEED)!=0)
  throw std::runtime_error("Guest discard failed");
 if(mprotect(base_+begin,end-begin,PROT_NONE)!=0)
  throw std::runtime_error("Guest decommit failed");
#endif
 for(std::uint64_t p=begin/page;p<end/page;++p)
  committed_pages_[p/64]&=~(std::uint64_t{1}<<(p%64));
}
bool GuestMemory::committed_range(std::uint64_t a,std::size_t length) const noexcept{
 if(!length||a>=kAddressSpaceSize||length>kAddressSpaceSize-a)return false;
 const auto begin=down(a),end=up(a+length);
 for(std::uint64_t p=begin/page;p<end/page;++p)
  if(!(committed_pages_[p/64]&(std::uint64_t{1}<<(p%64))))return false;
 return true;
}
bool GuestMemory::any_committed_range(std::uint64_t a,std::size_t length) const noexcept{
 if(!length)return false;
 if(a>=kAddressSpaceSize||length>kAddressSpaceSize-a)return true;
 const auto begin=down(a),end=up(a+length);
 for(std::uint64_t p=begin/page;p<end/page;++p)
  if(committed_pages_[p/64]&(std::uint64_t{1}<<(p%64)))return true;
 return false;
}
void GuestMemory::copy_to_guest(std::uint32_t a,std::span<const std::uint8_t> b){
 if(b.empty())return;commit(a,b.size());std::memcpy(address(a),b.data(),b.size());
}
void GuestMemory::load_pe(const std::filesystem::path& path){
 const auto v=read(path);std::span<const std::uint8_t>b(v);
 if(v.size()<512||u16(b,0)!=0x5a4d)throw std::runtime_error("Expected decrypted Xbox 360 PE");
 const auto nt=std::size_t(u32(b,0x3c));
 if(nt>v.size()||v.size()-nt<24||u32(b,nt)!=0x4550||u16(b,nt+4)!=0x01F2)
  throw std::runtime_error("Not an Xbox 360 PPC PE");
 const auto optional_size=std::size_t(u16(b,nt+20));
 const auto opt=nt+24;
 const auto count=std::size_t(u16(b,nt+6));
 if(optional_size<64||opt>v.size()||optional_size>v.size()-opt)
  throw std::runtime_error("Truncated Xbox PE optional header");
 const auto entry_rva=u32(b,opt+16);
 if(entry_rva>=kImageSize||(entry_rva&3U))
  throw std::runtime_error("Invalid Xbox PPC PE entry RVA");
 const auto sections=opt+optional_size;
 if(count>(v.size()-sections)/40)
  throw std::runtime_error("Truncated Xbox PE section table");
 if(u16(b,opt)!=0x10b||u32(b,opt+28)!=kImageBase)
  throw std::runtime_error("Unexpected image base");
 const auto header=std::size_t(u32(b,opt+60));
 if(header>v.size()||header>kImageSize||header<sections+40*count)
  throw std::runtime_error("Invalid PE headers");

 struct Section {std::uint32_t rva,raw,file_off;std::uint64_t length;};
 std::vector<Section> checked;
 checked.reserve(count);
 // Validate every section before changing guest memory: malformed private
 // images must never partially overwrite guest code or the native call table.
 for(std::size_t i=0;i<count;++i){
  const auto sh=sections+40*i;
  std::string name(reinterpret_cast<const char*>(v.data()+sh),8);
  name.resize(std::min<std::size_t>(8,name.find('\0')));
  const auto virtual_size=u32(b,sh+8),rva=u32(b,sh+12);
  const auto raw=u32(b,sh+16),file_off=u32(b,sh+20);
  const std::uint64_t len=std::max(virtual_size,raw);
  // The original retail DOTM PE declares a .reloc raw range beyond its
  // decrypted Basic image (and a virtual range crossing our dispatch table).
  // This is valid for our fixed-base guest: relocations are not loaded at all.
  // Skip them BEFORE checking file offsets or image bounds; all other
  // sections still receive strict raw/virtual range and overlap validation.
  if(name==".reloc")continue;
  if(raw&&(file_off>v.size()||raw>v.size()-file_off))
   throw std::runtime_error("Invalid PE section raw size");
  if(rva>=kImageSize||len>kImageSize-rva)
   throw std::runtime_error("PE section intersects native dispatch table");
  if(len&&rva<header)throw std::runtime_error("PE section overlaps headers");
  for(const auto& other:checked)
   if(len&&other.length&&std::uint64_t(rva)<std::uint64_t(other.rva)+other.length&&
      std::uint64_t(other.rva)<std::uint64_t(rva)+len)
    throw std::runtime_error("Overlapping PE virtual sections");
  checked.push_back({rva,raw,file_off,len});
 }
 // A PE load replaces the entire fixed-base image, not just its sections.
 // Section gaps, skipped .reloc bytes and the tail of shorter headers must
 // never inherit data from a previous load. The native dispatch table starts
 // exactly at kImageBase + kImageSize and must remain untouched.
 commit(kImageBase,kImageSize);
 std::memset(address(kImageBase),0,kImageSize);
 std::memcpy(address(kImageBase),v.data(),header);
 for(const auto& section:checked){
  if(section.raw)std::memcpy(address(kImageBase+section.rva),
                             v.data()+section.file_off,section.raw);
 }
 pe_entry_=kImageBase+entry_rva;
 pe_loaded_=true;
}
std::uint32_t GuestMemory::loaded_pe_entry() const{
 if(!pe_loaded_)throw std::logic_error("No Xbox PPC PE loaded");
 return pe_entry_;
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
 // Validate the entire batch before changing any dispatch entry. A malformed
 // mapping must not leave a mixture of old and new recompiled functions.
 std::unordered_set<std::uint32_t> seen;
 seen.reserve(entries.size());
 for(const auto& e:entries){
  if(e.guest_address<kCodeBase||std::uint64_t(e.guest_address)-kCodeBase>=kCodeSize||(e.guest_address&3)||!e.native_function)
   throw std::invalid_argument("Invalid PPC function address");
  if(!seen.insert(e.guest_address).second)
   throw std::invalid_argument("Duplicate PPC function address");
 }
 for(const auto& e:entries){
  const auto a=kFunctionTableOffset+2ULL*(e.guest_address-kCodeBase);
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