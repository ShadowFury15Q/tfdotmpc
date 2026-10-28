#include "guest_memory.hpp"
#include <array>
#include <cstring>
#include <iostream>
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
  const dotm::FunctionEntry entry{0x82500004,target};
  m.install_functions(std::span<const dotm::FunctionEntry>(&entry,1));
  check(m.find_function(0x82500004)==target,"native PPC function mapping");
  if(argc>1){
    m.load_pe(argv[1]);
    check(m.address(0x82000100)[0]=='P',"PE sections");
    std::cout<<"Original Xbox 360 PE sections mapped successfully\n";
  }
  std::cout<<"DOTM native runtime memory test PASS\n";return 0;
 }catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}