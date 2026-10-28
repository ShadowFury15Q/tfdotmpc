#pragma once
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include "native_input.hpp"
namespace dotm {
// Win32 host adapter. This is a REAL Windows Raw Input mouse device
// (not a gamepad emulator); it does not call Xbox game code yet.
class Win32Input {
public:
 explicit Win32Input(NativeInput& state) noexcept:state_(state){}
 void register_raw_mouse(HWND owner);
 // Invoke from the application's WndProc; returns whether input was recognized.
 bool handle(UINT message,WPARAM wparam,LPARAM lparam);
private:
 NativeInput& state_;
};
}
#endif
