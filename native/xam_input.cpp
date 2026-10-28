#include "xam_input.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace dotm {
namespace {
constexpr std::uint16_t kDpadUp=0x0001,kStart=0x0010,kRightThumb=0x0080;
constexpr std::uint16_t kLeftShoulder=0x0100,kRightShoulder=0x0200;
constexpr std::uint16_t kA=0x1000,kB=0x2000,kX=0x4000,kY=0x8000;
std::int16_t stick(float v) {
    if(!std::isfinite(v))return 0;
    if(v>=1)return 32767;
    if(v<=-1)return -32768;
    return static_cast<std::int16_t>(std::lround(v*32767.f));
}
void write16(std::span<std::uint8_t> out,std::size_t at,std::uint16_t value) {
    out[at]=static_cast<std::uint8_t>(value>>8);
    out[at+1]=static_cast<std::uint8_t>(value);
}
void write32(std::span<std::uint8_t> out,std::size_t at,std::uint32_t value) {
    out[at]=static_cast<std::uint8_t>(value>>24);
    out[at+1]=static_cast<std::uint8_t>(value>>16);
    out[at+2]=static_cast<std::uint8_t>(value>>8);
    out[at+3]=static_cast<std::uint8_t>(value);
}
}
void XamInputHost::publish(const InputFrame& frame) {
    std::lock_guard lock(mutex_);
    auto down=[&](InputAction action){return frame[action].down;};
    XboxGamepad next;
    if(down(InputAction::Jump))next.buttons|=kA;
    if(down(InputAction::Dash))next.buttons|=kB;
    if(down(InputAction::Interact))next.buttons|=kX;
    if(down(InputAction::Transform))next.buttons|=kY;
    if(down(InputAction::Ability))next.buttons|=kLeftShoulder;
    if(down(InputAction::FireSecondary))next.buttons|=kRightShoulder;
    if(down(InputAction::Melee))next.buttons|=kRightThumb;
    if(down(InputAction::Reload))next.buttons|=kDpadUp;
    if(down(InputAction::Pause))next.buttons|=kStart;
    next.left_trigger=down(InputAction::Aim)?255:0;
    next.right_trigger=down(InputAction::FirePrimary)?255:0;
    next.lx=stick(frame.move_x);
    next.ly=stick(frame.move_y);
    if(vehicle_mode_) {
        // Still maps movement onto the XInput stick: game-specific vehicle
        // steering/braking must be verified from the original controller logic.
    }
    next.rx=stick(frame.look_x*mouse_scale_/32767.f);
    next.ry=stick(-frame.look_y*mouse_scale_/32767.f);
    if(next!=state_) {
        state_=next;
        ++packet_;
    }
}
std::uint32_t XamInputHost::get_state(
    std::uint32_t user,std::uint32_t flags,std::span<std::uint8_t> out) const {
    if((flags&0xffU)!=0 && (flags&1U)==0)return kXamDeviceNotConnected;
    if(user!=0 && (user&0xffU)!=0xffU && !(flags&0x40000000U))
        return kXamDeviceNotConnected;
    if(out.size()<kXboxInputStateBytes)return kXamBadArguments;
    std::lock_guard lock(mutex_);
    write32(out,0,packet_);
    write16(out,4,state_.buttons);
    out[6]=state_.left_trigger;
    out[7]=state_.right_trigger;
    write16(out,8,static_cast<std::uint16_t>(state_.lx));
    write16(out,10,static_cast<std::uint16_t>(state_.ly));
    write16(out,12,static_cast<std::uint16_t>(state_.rx));
    write16(out,14,static_cast<std::uint16_t>(state_.ry));
    return kXamOk;
}
XboxGamepad XamInputHost::snapshot() const {
    std::lock_guard lock(mutex_);
    return state_;
}
std::uint32_t XamInputHost::packet_number() const {
    std::lock_guard lock(mutex_);
    return packet_;
}
void XamInputHost::set_mouse_stick_scale(float value){
    if(!std::isfinite(value)||value<1.f||value>32767.f)
        throw std::invalid_argument("Invalid mouse to Xbox stick scale");
    std::lock_guard lock(mutex_);
    mouse_scale_=value;
}
void XamInputHost::set_vehicle_mode(bool value) {
    std::lock_guard lock(mutex_);
    vehicle_mode_=value;
}
}
