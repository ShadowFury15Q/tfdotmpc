#include "guest_memory.hpp"
#include <array>
#include <cstring>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <vector>
#include <system_error>
#include <stdexcept>
void fallback(PPCContext&,std::uint8_t*){}
void target(PPCContext&,std::uint8_t*){}
void check(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
int main(int argc,char** argv){
 try {
  dotm::GuestMemory m;
  std::array<std::uint8_t,4> data{1,2,3,4};
  m.copy_to_guest(0x82001000,data);
  check(std::memcmp(m.address(0x82001000),data.data(),4)==0,"guest memory copy");
  m.commit(0x82002003,8192);
  m.initialize_function_table(fallback);
  check(m.find_function(0x82500000)==fallback,"fallback dispatch");
  const auto reject_mapping=[&](std::span<const dotm::FunctionEntry> entries,
                                const char* reason){
   bool failed=false;
   try{m.install_functions(entries);}catch(const std::invalid_argument&){failed=true;}
   check(failed,reason);
   check(m.find_function(0x82500000)==fallback,
         "Rejected batch must not modify its first dispatch slot");
   check(m.find_function(0x82500004)==fallback,
         "Rejected batch must not modify its second dispatch slot");
  };
  const std::array<dotm::FunctionEntry,2> bad_late{{
   {0x82500000,target},{0x82500003,target}
  }};
  reject_mapping(bad_late,"Reject late unaligned PPC address atomically");
  const std::array<dotm::FunctionEntry,2> bad_null{{
   {0x82500000,target},{0x82500004,nullptr}
  }};
  reject_mapping(bad_null,"Reject late null native function atomically");
  const std::array<dotm::FunctionEntry,2> bad_duplicate{{
   {0x82500000,target},{0x82500000,target}
  }};
  reject_mapping(bad_duplicate,"Reject duplicate PPC function atomically");
  const std::array<dotm::FunctionEntry,2> bad_range{{
   {0x82500000,target},{dotm::GuestMemory::kCodeBase+
    dotm::GuestMemory::kCodeSize,target}
  }};
  reject_mapping(bad_range,"Reject out-of-range PPC address atomically");
  const std::array<dotm::FunctionEntry,2> good{{
   {0x82500000,target},{0x82500004,target}
  }};
  m.install_functions(good);
  check(m.find_function(0x82500000)==target,"first native PPC function mapping");
  check(m.find_function(0x82500004)==target,"second native PPC function mapping");
  // Synthetic Xbox PPC PE fixture: no copyrighted game bytes.
  auto pe=std::vector<std::uint8_t>(0x600,0);
  auto put16=[](std::vector<std::uint8_t>& bytes,std::size_t p,std::uint16_t n){
   bytes[p]=static_cast<std::uint8_t>(n);bytes[p+1]=static_cast<std::uint8_t>(n>>8);
  };
  auto put32=[](std::vector<std::uint8_t>& bytes,std::size_t p,std::uint32_t n){
   for(int i=0;i<4;++i)bytes[p+i]=static_cast<std::uint8_t>(n>>(8*i));
  };
  put16(pe,0,0x5a4d);put32(pe,0x3c,0x80);put32(pe,0x80,0x4550);
  put16(pe,0x84,0x01f2);put16(pe,0x86,1);put16(pe,0x94,0xe0);
  put16(pe,0x98,0x10b);put32(pe,0x98+28,dotm::GuestMemory::kImageBase);
  put32(pe,0x98+60,0x200);
  std::memcpy(pe.data()+0x178,".text",5);
  put32(pe,0x178+8,0x100);put32(pe,0x178+12,0x1000);
  put32(pe,0x178+16,0x100);put32(pe,0x178+20,0x200);
  pe[0x200]=0xab;
  const auto file=std::filesystem::temp_directory_path()/
      ("dotm_synthetic_pe_"+std::to_string(reinterpret_cast<std::uintptr_t>(&m))+".bin");
  struct TempFile {std::filesystem::path path;~TempFile(){
    std::error_code ignored;std::filesystem::remove(path,ignored);
  }} cleanup{file};
  auto save=[&](const std::vector<std::uint8_t>& bytes){
   std::ofstream out(file,std::ios::binary|std::ios::trunc);
   out.write(reinterpret_cast<const char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
   if(!out)throw std::runtime_error("Cannot write synthetic PE fixture");
  };
  save(pe);m.load_pe(file);
  check(m.address(0x82001000)[0]==0xab,"Synthetic Xbox PE section mapped");
  check(m.address(0x82001001)[0]==0,"PE virtual zero-fill");
  auto reject=[&](std::vector<std::uint8_t> invalid,const char* reason){
   save(invalid);
   bool failed=false;
   try{m.load_pe(file);}catch(const std::exception&){failed=true;}
   check(failed,reason);
   check(m.address(0x82001000)[0]==0xab,
         "Malformed PE must not partially replace mapped guest section");
  };
  {auto bad=pe;put32(bad,0x3c,0xfffffff0);reject(bad,"Reject invalid NT offset");}
  {auto bad=pe;put16(bad,0x94,0x500);reject(bad,"Reject truncated optional header");}
  {auto bad=pe;put32(bad,0x178+20,0x580);reject(bad,"Reject truncated raw section");}
  {auto bad=pe;put32(bad,0x178+12,0x100);reject(bad,"Reject header overlap");}
  {auto bad=pe;put32(bad,0x178+12,0x01b0ff80);
   reject(bad,"Reject section crossing native dispatch table");}
  {auto bad=pe;put16(bad,0x86,2);
   std::memcpy(bad.data()+0x1a0,".data",5);
   put32(bad,0x1a0+8,0x100);put32(bad,0x1a0+12,0x1080);
   reject(bad,"Reject overlapping virtual sections");}
  if(argc>1){
    m.load_pe(argv[1]);
    check(m.address(0x82000100)[0]=='P',"PE sections");
    std::cout<<"Original Xbox 360 PE sections mapped successfully\n";
  }
  std::cout<<"DOTM native runtime memory test PASS\n";return 0;
 }catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}