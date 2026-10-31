#include "kernel_title_termination.hpp"
#include <algorithm>
#include <mutex>
#include <stdexcept>

namespace dotm {
namespace {
std::mutex registration_mutex;
GuestMemory* guest = nullptr;
std::vector<TitleTerminationNotification> notifications;
std::uint32_t be32(std::uint32_t address) {
    const auto* b = guest->address(address);
    return (std::uint32_t(b[0]) << 24) | (std::uint32_t(b[1]) << 16) |
           (std::uint32_t(b[2]) << 8) | b[3];
}
}
void bind_title_termination(GuestMemory* memory) noexcept {
    std::lock_guard lock(registration_mutex);
    guest = memory;
    notifications.clear();
}
void ex_bound_register_title_termination(std::uint32_t registration,
                                         std::uint32_t create) {
    std::lock_guard lock(registration_mutex);
    if (!guest || (registration & 3U) || registration < 0x10000U ||
        !guest->committed_range(registration, 16))
        throw std::runtime_error("Invalid Xbox title-termination registration");
    const auto callback = be32(registration);
    if (!callback || (callback & 3U) ||
        !guest->committed_range(callback, 4))
        throw std::runtime_error("Invalid Xbox title-termination callback");
    if (!create) {
        const auto it = std::find_if(notifications.begin(), notifications.end(),
            [callback](const auto& item) { return item.callback_address == callback; });
        if (it != notifications.end()) notifications.erase(it);
        return;
    }
    const auto priority = be32(registration + 4);
    const auto it = std::find_if(notifications.begin(), notifications.end(),
        [registration](const auto& item) { return item.registration_address == registration; });
    if (it != notifications.end()) {
        it->callback_address = callback;
        it->priority = priority;
    } else {
        notifications.push_back({registration, callback, priority});
    }
}
std::vector<TitleTerminationNotification> title_termination_snapshot() {
    std::lock_guard lock(registration_mutex);
    return notifications;
}
}
