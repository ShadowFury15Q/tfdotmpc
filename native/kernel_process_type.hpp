#pragma once
#include <cstdint>
#include <stdexcept>

namespace dotm {
// Xbox 360 process types: idle (0), title (1), system (2).
// This is per guest thread: kernel callbacks may temporarily switch the
// caller's process context without changing unrelated title threads.
inline thread_local std::uint32_t guest_current_process_type = 1U;

inline std::uint32_t ke_get_current_process_type() noexcept {
    return guest_current_process_type;
}

inline void ke_set_current_process_type(std::uint32_t type) {
    if (type > 2U)
        throw std::invalid_argument("KeSetCurrentProcessType: invalid process type");
    guest_current_process_type = type;
}
} // namespace dotm
