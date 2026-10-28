#include "native_input.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace dotm {
NativeInput::NativeInput() {
 // Initial convenient PC bindings; actual gameplay actions to be validated
 // against the title's original context-sensitive robot/vehicle controls.
 bindings_ = {
  'W','S','A','D',0x20, // movement and space
  kMouseLeft,kMouseRight,0x02, // primary, secondary, aim (will rebind below)
  'F','Q','V',0x10, // transform, ability, melee, shift/dash
  'E','R',0x1B // interact, reload, escape
 };
 bindings_[static_cast<std::size_t>(InputAction::Aim)] = kMouseMiddle;
}
void NativeInput::bind(InputAction action,std::uint16_t code) {
 if(code>=kKeyCount) throw std::invalid_argument("Unsupported Windows input binding");
 if(static_cast<std::size_t>(action)>=kInputActionCount)
  throw std::invalid_argument("Invalid action binding");
 bindings_[static_cast<std::size_t>(action)]=code;
 // Reset edge detector so a rebind doesn't synthesize an old held state.
 last_action_state_[static_cast<std::size_t>(action)]=false;
}
std::uint16_t NativeInput::binding(InputAction action) const {
 auto index=static_cast<std::size_t>(action);
 if(index>=kInputActionCount)throw std::invalid_argument("Invalid action lookup");
 return bindings_[index];
}
void NativeInput::set_button(std::uint16_t code,bool pressed){
 if(code>=kKeyCount)throw std::invalid_argument("Unsupported Windows input code");
 down_[code]=pressed;
}
void NativeInput::relative_mouse(int dx,int dy){
 mouse_x_+=dx;
 mouse_y_+=dy;
}
void NativeInput::mouse_wheel(int vertical_steps){
 if(vertical_steps>0 && wheel_>std::numeric_limits<int>::max()-vertical_steps)
  wheel_=std::numeric_limits<int>::max();
 else if(vertical_steps<0 && wheel_<std::numeric_limits<int>::min()-vertical_steps)
  wheel_=std::numeric_limits<int>::min();
 else wheel_+=vertical_steps;
}
void NativeInput::set_sensitivity(float value){
 if(!std::isfinite(value)||value<0.01f||value>20.f)
  throw std::invalid_argument("Mouse sensitivity must be between 0.01 and 20");
 sensitivity_=value;
}
void NativeInput::release_all() noexcept {
 down_.fill(false);
 mouse_x_=mouse_y_=0;
 wheel_=0;
 // Preserve the previous action edges so the next frame emits releases.
}
InputFrame NativeInput::next_frame() {
 InputFrame frame;
 for(std::size_t i=0;i<kInputActionCount;++i){
  bool active=down_[bindings_[i]];
  frame.actions[i]={active,active&&!last_action_state_[i],
                    !active&&last_action_state_[i]};
  last_action_state_[i]=active;
 }
 auto active=[&](InputAction action){return frame[action].down?1.f:0.f;};
 frame.move_x=active(InputAction::MoveRight)-active(InputAction::MoveLeft);
 frame.move_y=active(InputAction::MoveForward)-active(InputAction::MoveBackward);
 const float mag=std::sqrt(frame.move_x*frame.move_x+frame.move_y*frame.move_y);
 if(mag>1.f){frame.move_x/=mag;frame.move_y/=mag;}
 frame.look_x=static_cast<float>(mouse_x_*sensitivity_);
 frame.look_y=static_cast<float>(mouse_y_*sensitivity_*(invert_y_?-1.0:1.0));
 frame.wheel=wheel_;
 mouse_x_=mouse_y_=0;
 wheel_=0;
 return frame;
}
}
