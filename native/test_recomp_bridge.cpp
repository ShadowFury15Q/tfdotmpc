#include "recomp_bridge.hpp"
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>
// A minimal synthetic CPU context with the SAME opaque ABI parameter shape.
// This test uses no proprietary game code or resources.
struct PPCContext { unsigned calls=0; };
namespace {
void missing(PPCContext& ctx,std::uint8_t*){ctx.calls+=1000;}
void game_start(PPCContext& ctx,std::uint8_t* base) {
    // Emulate a tiny synthetically recompiled PPC store: one byte in guest image.
    base[0x82001123]=0x5a;
    ctx.calls+=1;
}
void another(PPCContext& ctx,std::uint8_t*){ctx.calls+=2;}
void check(bool pass,const char* message){if(!pass)throw std::runtime_error(message);}
}
int main() {
 try {
    dotm::GuestMemory memory;
    memory.commit(0x82001000,4096);
    constexpr std::uint32_t entry=0x8277F670;
    const std::array<dotm::RecompiledFunction,2> map{{
        {0x82500000,another},{entry,game_start}
    }};
    const auto result=dotm::install_recompiled_dispatch(memory,map,entry,missing);
    check(result.mapped_functions==2 && result.guest_entry==entry,"Mapped entry and counts");
    check(memory.find_function(entry)==game_start,"Actual entry point registered");
    PPCContext ctx{};
    memory.find_function(entry)(ctx,memory.base());
    check(ctx.calls==1 && memory.address(0x82001123)[0]==0x5a,"Native host code executed with guest memory");
    memory.find_function(0x82500004)(ctx,memory.base());
    check(ctx.calls==1001,"Unmapped guest address has deterministic fallback");
    const std::array<dotm::RecompiledFunction,2> duplicate{{
        {entry,game_start},{entry,another}
    }};
    bool rejected=false;
    try{(void)dotm::install_recompiled_dispatch(memory,duplicate,entry,missing);}
    catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"Duplicate address rejected");
    rejected=false;
    try{(void)dotm::install_recompiled_dispatch(memory,map,0x82500008,missing);}
    catch(const std::runtime_error&){rejected=true;}
    check(rejected,"Missing entry-point rejected before table mutation");
    rejected=false;
    const std::array<dotm::RecompiledFunction,1> outside{{{0xC0000000,another}}};
    try{(void)dotm::install_recompiled_dispatch(memory,outside,0xC0000000,missing);}
    catch(const std::out_of_range&){rejected=true;}
    check(rejected,"Out-of-image mapping rejected");
    std::cout<<"DOTM native PPC dispatch bridge PASS (synthetic code, not game startup)\n";
    return 0;
 } catch(const std::exception& e) {
    std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;
 }
}
