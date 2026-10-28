#include "compiled_ppc_adapter.hpp"
#include "ppc_context.h"
#include <cstdint>
#include <cstdio>
#include <stdexcept>
namespace {
void entry(PPCContext& ctx,std::uint8_t* base) {ctx.calls++;base[0x82001234]=0x87;}
void helper(PPCContext& ctx,std::uint8_t*){ctx.calls+=2;}
void missing(PPCContext& ctx,std::uint8_t*){ctx.calls+=10;}
void verify(bool cond,const char* msg){if(!cond)throw std::runtime_error(msg);}
}
PPCFuncMapping PPCFuncMappings[]={
  {0x82500000,helper},
  {0x8277F670,entry},
  {0,nullptr}
};
int main(){
 try {
  dotm::GuestMemory m;
  m.commit(0x82001000,4096);
  const auto summary=dotm::install_actual_xenon_table(m,missing);
  verify(summary.mapped_functions==2,"Compiled mapping entry count");
  verify(summary.guest_entry==0x8277F670,"_xstart mapped");
  PPCContext ctx{};
  m.find_function(summary.guest_entry)(ctx,m.base());
  verify(ctx.calls==1&&m.address(0x82001234)[0]==0x87,"Real XenonRecomp-style map invoked");
  m.find_function(0x82500004)(ctx,m.base());
  verify(ctx.calls==11,"Unmapped address falls back safely");
  std::puts("DOTM synthetic generated-PPC-mapping bridge PASS");
  return 0;
 }catch(const std::exception&e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
