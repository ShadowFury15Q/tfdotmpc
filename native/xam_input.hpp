#pragma once
#include "native_input.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>

namespace dotm {
// Xbox 360 XAM X_INPUT_STATE is 16 big-endian bytes:
// DWORD packet; WORD buttons; BYTE triggers[2]; SHORT sticks[4].
// The *game's* action-to-button layout is not yet known; this mapping is
// provisional. No physical gamepad or console emulator is required.
struct XboxGamepad {
    std::uint16_t buttons=0;
    std::uint8_t left_trigger=0, right_trigger=0;
    std::int16_t lx=0,ly=0,rx=0,ry=0;
    bool operator==(const XboxGamepad&) const = default;
};
constexpr std::uint32_t kXamOk=0;
constexpr std::uint32_t kXamBadArguments=160; // X_ERROR_BAD_ARGUMENTS (0xA0), not ERROR_INVALID_PARAMETER (0x57)
constexpr std::uint32_t kXamDeviceNotConnected=1167;
constexpr std::size_t kXboxInputStateBytes=16;
class XamInputHost {
public:
    // Store a new PC input frame. Only increment packet when BUTTON/AXIS
    // values changed, per the XINPUT_STATE contract.
    void publish(const InputFrame& frame);
    // flags == 0 or gamepad (1); index 0 and ANY_USER(0xFF) map to PC.
    // Invalid devices/users fail without modifying output bytes.
    std::uint32_t get_state(std::uint32_t user, std::uint32_t flags,
                             std::span<std::uint8_t> output) const;
    XboxGamepad snapshot() const;
    std::uint32_t packet_number() const;
    void set_mouse_stick_scale(float stick_units_per_pixel);
    void set_vehicle_mode(bool vehicle_mode);
private:
    mutable std::mutex mutex_;
    XboxGamepad state_{};
    std::uint32_t packet_=0;
    float mouse_scale_=1200.0f;
    bool vehicle_mode_=false;
};
}
