#include "kernel_tls.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
namespace dotm {
namespace {
constexpr std::size_t kMaxSlots = 256;
struct Slot { bool allocated = false; std::uint64_t epoch = 0; };
struct ThreadValue { std::uint64_t epoch = 0; std::uint32_t value = 0; };
std::mutex slots_mutex;
std::array<Slot, kMaxSlots> slots{};
thread_local std::array<ThreadValue, kMaxSlots> thread_values{};
}
std::uint32_t kernel_tls_alloc() noexcept {
    std::lock_guard lock(slots_mutex);
    for (std::size_t i = 0; i < kMaxSlots; ++i) {
        if (!slots[i].allocated) {
            slots[i].allocated = true;
            ++slots[i].epoch;
            return static_cast<std::uint32_t>(i);
        }
    }
    return kInvalidTlsIndex;
}
bool kernel_tls_free(std::uint32_t index) noexcept {
    std::lock_guard lock(slots_mutex);
    if (index >= kMaxSlots || !slots[index].allocated) return false;
    slots[index].allocated = false;
    ++slots[index].epoch;
    return true;
}
std::uint32_t kernel_tls_get_value(std::uint32_t index) noexcept {
    std::lock_guard lock(slots_mutex);
    if (index >= kMaxSlots || !slots[index].allocated) return 0;
    const auto& value = thread_values[index];
    return value.epoch == slots[index].epoch ? value.value : 0;
}
bool kernel_tls_set_value(std::uint32_t index, std::uint32_t value) noexcept {
    std::lock_guard lock(slots_mutex);
    if (index >= kMaxSlots || !slots[index].allocated) return false;
    thread_values[index] = {slots[index].epoch, value};
    return true;
}
}
