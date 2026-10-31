#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace dotm {
// These are NATIVE Windows keyboard virtual-key codes, NOT Xbox gamepad state.
// The original DOTM input action semantics still need a game-runtime adapter.
enum class InputAction : std::uint8_t {
 MoveForward, MoveBackward, MoveLeft, MoveRight, Jump,
 FirePrimary, FireSecondary, Aim, Transform, Ability, Melee,
 Dash, Interact, Reload, Pause, Count
};
constexpr std::size_t kInputActionCount=static_cast<std::size_t>(InputAction::Count);
constexpr std::uint16_t kMouseLeft=256,kMouseRight=257,kMouseMiddle=258,
                        kMouseX1=259,kMouseX2=260,kKeyCount=261;
struct ActionState { bool down=false, pressed=false, released=false; };
struct InputFrame {
 std::array<ActionState,kInputActionCount> actions{};
 float move_x=0,move_y=0,look_x=0,look_y=0;
 int wheel=0;
 const ActionState& operator[](InputAction action) const noexcept {
  return actions[static_cast<std::size_t>(action)];
 }
};
class NativeInput {
public:
 NativeInput();
 void bind(InputAction action,std::uint16_t vk_or_mouse);
 std::uint16_t binding(InputAction action) const;
 void set_button(std::uint16_t vk_or_mouse,bool down);
 void relative_mouse(int dx,int dy);
 void mouse_wheel(int vertical_steps);
 void set_sensitivity(float value);
 void set_invert_y(bool invert) noexcept {invert_y_=invert;}
 void release_all() noexcept;
 InputFrame next_frame();
private:
 std::array<std::uint16_t,kInputActionCount> bindings_{};
 std::array<bool,kKeyCount> down_{};
 std::array<bool,kInputActionCount> last_action_state_{};
 double mouse_x_=0,mouse_y_=0;
 int wheel_=0;
 float sensitivity_=1.0f;
 bool invert_y_=false;
};
}
