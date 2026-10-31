#include "win32_input.hpp"
#ifdef _WIN32
#include <stdexcept>
#include <vector>
namespace dotm {
void Win32Input::register_raw_mouse(HWND hwnd) {
 if(!hwnd)throw std::invalid_argument("Raw mouse requires a valid HWND");
 RAWINPUTDEVICE device{};
 device.usUsagePage=0x01; // Generic Desktop usage page
 device.usUsage=0x02; // Mouse usage
 device.dwFlags=0; // foreground-only raw mouse input
 device.hwndTarget=hwnd;
 if(!RegisterRawInputDevices(&device,1,sizeof(device)))
  throw std::runtime_error("RegisterRawInputDevices failed");
}
bool Win32Input::handle(UINT msg,WPARAM wp,LPARAM lp) {
 switch(msg) {
 case WM_KEYDOWN:
 case WM_SYSKEYDOWN:
 case WM_KEYUP:
 case WM_SYSKEYUP: {
  const auto code=static_cast<unsigned>(wp);
  if(code<256)
   state_.set_button(static_cast<std::uint16_t>(code),
                     msg==WM_KEYDOWN||msg==WM_SYSKEYDOWN);
  return true;
 }
 case WM_KILLFOCUS:
  state_.release_all();
  return true;
 case WM_ACTIVATEAPP:
  if(wp==FALSE)state_.release_all();
  return true;
 case WM_INPUT: {
  UINT size=0;
  const auto handle=reinterpret_cast<HRAWINPUT>(lp);
  if(GetRawInputData(handle,RID_INPUT,nullptr,&size,sizeof(RAWINPUTHEADER))!=0||
     size<sizeof(RAWINPUTHEADER)||size>65536)
   return false;
  std::vector<BYTE> data(size);
  if(GetRawInputData(handle,RID_INPUT,data.data(),&size,sizeof(RAWINPUTHEADER))!=size)
   return false;
  const auto* raw=reinterpret_cast<const RAWINPUT*>(data.data());
  if(raw->header.dwType!=RIM_TYPEMOUSE)return false;
  const auto& mouse=raw->data.mouse;
  if(!(mouse.usFlags&MOUSE_MOVE_ABSOLUTE))
   state_.relative_mouse(mouse.lLastX,mouse.lLastY);
  const auto flags=mouse.usButtonFlags;
  if(flags&RI_MOUSE_LEFT_BUTTON_DOWN)state_.set_button(kMouseLeft,true);
  if(flags&RI_MOUSE_LEFT_BUTTON_UP)state_.set_button(kMouseLeft,false);
  if(flags&RI_MOUSE_RIGHT_BUTTON_DOWN)state_.set_button(kMouseRight,true);
  if(flags&RI_MOUSE_RIGHT_BUTTON_UP)state_.set_button(kMouseRight,false);
  if(flags&RI_MOUSE_MIDDLE_BUTTON_DOWN)state_.set_button(kMouseMiddle,true);
  if(flags&RI_MOUSE_MIDDLE_BUTTON_UP)state_.set_button(kMouseMiddle,false);
  if(flags&RI_MOUSE_BUTTON_4_DOWN)state_.set_button(kMouseX1,true);
  if(flags&RI_MOUSE_BUTTON_4_UP)state_.set_button(kMouseX1,false);
  if(flags&RI_MOUSE_BUTTON_5_DOWN)state_.set_button(kMouseX2,true);
  if(flags&RI_MOUSE_BUTTON_5_UP)state_.set_button(kMouseX2,false);
  if(flags&RI_MOUSE_WHEEL) {
   const auto delta=static_cast<SHORT>(mouse.usButtonData);
   state_.mouse_wheel(delta/WHEEL_DELTA);
  }
  return true;
 }
 default:return false;
 }
}
}
#endif
