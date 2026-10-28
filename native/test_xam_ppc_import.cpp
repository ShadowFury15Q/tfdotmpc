#include "ppc_recomp_shared.h"
#include "guest_memory.hpp"
#include "native_input.hpp"
#include "xam_guest_input.hpp"
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>

PPC_EXTERN_FUNC(__imp__XamInputGetState);
PPC_EXTERN_FUNC(__imp__XamInputGetCapabilities);

namespace {
void require(bool pass,const char* message){if(!pass)throw std::runtime_error(message);}
}
int main(){
 try {
    dotm::NativeInput host;
    dotm::XamInputHost service;
    dotm::GuestMemory memory;
    dotm::bind_xam_guest_input(&service,&memory);

    constexpr std::uint32_t guest_ptr=0x82032000u;
    PPCContext ctx{};
    ctx.r3.u64=0; // controller user
    ctx.r4.u64=1; // GAMEPAD
    ctx.r5.u64=guest_ptr;
    host.set_button('F',true); // transform (provisional Xbox Y)
    host.set_button(dotm::kMouseLeft,true);
    service.publish(host.next_frame());

    __imp__XamInputGetState(ctx,memory.base());
    require(ctx.r3.u64==dotm::kXamOk,"PPC function returns Xbox status in r3");
    const auto* out=memory.address(guest_ptr);
    require(out[0]==0&&out[1]==0&&out[2]==0&&out[3]==1,
            "PPC import writes big-endian packet number to guest memory");
    require(out[4]==0x80&&out[5]==0x00,"PPC import writes Y transform button");
    require(out[7]==255,"PPC import writes right trigger fire");
    ctx.r3.u64=1; // unconnected second controller
    __imp__XamInputGetState(ctx,memory.base());
    require(ctx.r3.u64==dotm::kXamDeviceNotConnected,
            "Unavailable Xbox users return proper nonconnected status");
    ctx.r3.u64=0;
    ctx.r5.u64=0;
    __imp__XamInputGetState(ctx,memory.base());
    require(ctx.r3.u64==dotm::kXamOk,
            "Null Xam output pointer supports connection-presence query");
    ctx.r3.u64=0;
    ctx.r5.u64=dotm::GuestMemory::kFunctionTableOffset;
    __imp__XamInputGetState(ctx,memory.base());
    require(ctx.r3.u64==dotm::kXamBadArguments,
            "Disallow Xbox input writes into native PPC dispatch table");

    constexpr std::uint32_t caps_ptr=0x82033000u;
    ctx.r3.u64=0;
    ctx.r4.u64=1;
    ctx.r5.u64=caps_ptr;
    __imp__XamInputGetCapabilities(ctx,memory.base());
    require(ctx.r3.u64==dotm::kXamOk,"Original PPC capabilities import returns success");
    const auto* caps=memory.address(caps_ptr);
    require(caps[0]==1&&caps[1]==1&&caps[4]==0xF3&&caps[5]==0xFF,
            "PPC XamInputGetCapabilities wrote big endian 20-byte guest struct");
    ctx.r3.u64=0;
    ctx.r5.u64=0;
    __imp__XamInputGetCapabilities(ctx,memory.base());
    require(ctx.r3.u64==dotm::kXamBadArguments,
            "XAM capabilities cannot write to null guest destination");
    dotm::bind_xam_guest_input(nullptr,nullptr);
    ctx.r3.u64=0;
    ctx.r5.u64=guest_ptr;
    __imp__XamInputGetState(ctx,memory.base());
    require(ctx.r3.u64==dotm::kXamDeviceNotConnected,
            "Unbound native XAM device does not pretend input is connected");
    std::cout<<"DOTM original PPC import XamInputGetState host ABI bridge PASS\n";
    return 0;
 }catch(const std::exception& e) {
    dotm::bind_xam_guest_input(nullptr,nullptr);
    std::cerr<<"FAIL: "<<e.what()<<"\n";
    return 1;
 }
}
