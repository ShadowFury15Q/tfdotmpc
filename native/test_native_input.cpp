#include "native_input.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
void verify(bool condition,const char* description){
 if(!condition)throw std::runtime_error(description);
}
bool eq(float a,float b){return std::abs(a-b)<0.0001f;}
}
int main(){
 try {
  dotm::NativeInput input;
  using A=dotm::InputAction;
  verify(input.binding(A::MoveForward)=='W',"Default WASD");
  verify(input.binding(A::Transform)=='F',"Default transform");
  verify(input.binding(A::FirePrimary)==dotm::kMouseLeft,"Default left mouse fire");
  input.set_button('W',true);
  input.set_button('D',true);
  input.set_button(dotm::kMouseLeft,true);
  input.relative_mouse(13,-7);
  input.set_sensitivity(2.0f);
  auto first=input.next_frame();
  verify(first[A::MoveForward].pressed&&first[A::MoveRight].pressed,"First-frame action edges");
  verify(first[A::FirePrimary].down&&first[A::FirePrimary].pressed,"Mouse fire pressed");
  verify(eq(first.move_x,0.70710678f)&&eq(first.move_y,0.70710678f),
         "Normalize diagonal WASD movement");
  verify(eq(first.look_x,26)&&eq(first.look_y,-14),"Relative mouse sensitivity");
  auto second=input.next_frame();
  verify(second[A::MoveForward].down&&!second[A::MoveForward].pressed,
         "No repeated pressed edge");
  verify(eq(second.look_x,0)&&eq(second.look_y,0),"Clear relative mouse per frame");
  input.set_button('W',false);
  input.set_button('D',false);
  input.set_button(dotm::kMouseLeft,false);
  auto released=input.next_frame();
  verify(released[A::FirePrimary].released&&released[A::MoveForward].released,
         "Key and mouse release edges");
  input.bind(A::Transform,'T');
  input.set_button('T',true);
  verify(input.next_frame()[A::Transform].pressed,"User binding override");
  input.relative_mouse(0,10);
  input.set_invert_y(true);
  verify(eq(input.next_frame().look_y,-20),"Inverted mouse vertical axis");
  input.mouse_wheel(1);
  input.mouse_wheel(-3);
  verify(input.next_frame().wheel==-2&&input.next_frame().wheel==0,
         "Mouse wheel accumulates and clears");
  input.set_button('A',true);
  verify(input.next_frame()[A::MoveLeft].down,"Move left held");
  input.release_all();
  auto focus=input.next_frame();
  verify(focus[A::MoveLeft].released&&focus[A::Transform].released,
         "Releases stuck controls when window loses focus");
  bool threw=false;
  try {input.set_sensitivity(std::numeric_limits<float>::quiet_NaN());}
  catch(const std::invalid_argument&){threw=true;}
  verify(threw,"Reject NaN sensitivity");
  threw=false;
  try {input.bind(A::Jump,999);}
  catch(const std::invalid_argument&){threw=true;}
  verify(threw,"Reject invalid input key");
  std::cout<<"DOTM native PC keyboard/mouse input tests PASS\n";
  return 0;
 }catch(const std::exception& e) {
  std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;
 }
}
