#include "native_input.hpp"
#include "xam_input.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool value,const char*message){if(!value)throw std::runtime_error(message);}
std::uint16_t be16(const std::array<std::uint8_t,16>&b,std::size_t p){
 return static_cast<std::uint16_t>((unsigned(b[p])<<8)|b[p+1]);
}
std::uint32_t be32(const std::array<std::uint8_t,16>&b,std::size_t p){
 return (std::uint32_t(b[p])<<24)|(std::uint32_t(b[p+1])<<16)|
        (std::uint32_t(b[p+2])<<8)|b[p+3];
}
}
int main(){
 try{
  dotm::NativeInput pc;
  dotm::XamInputHost xbox;
  using A=dotm::InputAction;
  std::array<std::uint8_t,16> buffer{};
  require(xbox.get_state(0,1,buffer)==dotm::kXamOk,"Xbox gamepad connected on user 0");
  require(be32(buffer,0)==0&&be16(buffer,4)==0,"Xam initial neutral input");
  require(xbox.get_state(1,1,buffer)==dotm::kXamDeviceNotConnected,
          "Only user 0 virtual native PC input connected");
  require(xbox.get_state(0,2,buffer)==dotm::kXamDeviceNotConnected,
          "Reject non-gamepad input device");
  require(xbox.get_state(0,1,{buffer.data(),15})==dotm::kXamBadArguments,
          "Reject undersized big endian gamepad output");
  require(xbox.get_state(0xFF,1,buffer)==dotm::kXamOk,
          "Xam ANY_USER maps to native Windows keyboard and mouse");

  std::array<std::uint8_t,20> caps{};
  require(xbox.get_capabilities(0,1,caps)==dotm::kXamOk,
          "GetCapabilities advertises native keyboard/mouse virtual gamepad");
  require(caps[0]==1&&caps[1]==1&&caps[2]==0&&caps[3]==0,
          "Xbox XINPUT_CAPABILITIES type/subtype and BE flags");
  require(caps[4]==0xF3&&caps[5]==0xFF&&caps[6]==255&&caps[7]==255,
          "Xbox capability supported buttons and maximum triggers");
  require(caps[16]==0&&caps[17]==0&&caps[18]==0&&caps[19]==0,
          "Do not falsely advertise rumble support");
  require(xbox.get_capabilities(1,1,caps)==dotm::kXamDeviceNotConnected,
          "Capabilities reject unconnected second player");
  require(xbox.get_capabilities(0,1,{caps.data(),19})==dotm::kXamBadArguments,
          "Capabilities reject truncated destination");
  require(xbox.get_state(1,0x40000001,buffer)==dotm::kXamOk,
          "XAM ANY_USER flag 1<<30 resolves PC player zero");
  pc.set_button('W',true);
  pc.set_button('D',true);
  pc.set_button(dotm::kMouseLeft,true);
  pc.set_button(dotm::kMouseRight,true);
  pc.set_button('F',true);
  pc.set_button(' ',true);
  pc.relative_mouse(10,-5);
  xbox.publish(pc.next_frame());
  require(xbox.get_state(0,0,buffer)==dotm::kXamOk,"Xam flags=0 default");
  require(be32(buffer,0)==1,"Gamepad packet increases after state change");
  require((be16(buffer,4)&0x9000)==0x9000,"Jump=A transform=Y");
  require(buffer[6]==255&&buffer[7]==255,"Mouse aim and shoot map to analog triggers");
  require(be16(buffer,8)>0&&be16(buffer,10)>0,
          "WASD diagonal maps to positive left stick");
  require(be16(buffer,12)>0&&static_cast<std::int16_t>(be16(buffer,14))>0,
          "Relative mouse right/up maps to right stick");

  std::array<std::uint8_t,16> unchanged{};
  xbox.get_state(0,1,unchanged);
  require(unchanged==buffer,"Repeat poll returns identical frame");
  xbox.publish(pc.next_frame());
  xbox.get_state(0,1,buffer);
  require(be32(buffer,0)==2,"Releasing relative mouse stick increments Xbox packet");

  xbox.publish(pc.next_frame());
  xbox.get_state(0,1,buffer);
  require(be32(buffer,0)==2,"Identical held buttons do not change packet");

  pc.release_all();
  xbox.publish(pc.next_frame());
  xbox.get_state(0,1,buffer);
  require(be16(buffer,4)==0&&be16(buffer,8)==0&&buffer[6]==0&&buffer[7]==0,
          "Focus loss releases controller buttons and axes");
  require(be32(buffer,0)==3,"Focus loss state change increments packet");

  pc.set_button('A',true);
  pc.relative_mouse(100000,0);
  xbox.publish(pc.next_frame());
  xbox.get_state(0,1,buffer);
  require(static_cast<std::int16_t>(be16(buffer,8))==-32768,
          "Full left input serializes Xbox -32768 big endian");
  require(be16(buffer,12)==32767,"Right-stick values clamp to 32767");
  bool rejected=false;
  try{xbox.set_mouse_stick_scale(0.f);}catch(const std::invalid_argument&){rejected=true;}
  require(rejected,"Reject invalid mouse-to-stick sensitivity");
  std::cout<<"DOTM keyboard/mouse to Xbox XamInputGetState 16-byte BE state PASS\n";
  return 0;
 }catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
