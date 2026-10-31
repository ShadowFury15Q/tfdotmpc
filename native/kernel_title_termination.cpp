#include "kernel_title_termination.hpp"
#include <algorithm>
#include <mutex>
#include <optional>
#include <stdexcept>

namespace dotm {
namespace {
std::mutex registration_mutex;
GuestMemory* guest = nullptr;
std::vector<TitleTerminationNotification> notifications;
bool terminating = false;
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
    terminating = false;
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
[[noreturn]] void dispatch_termination(PPCContext& context,
                                       std::optional<std::uint32_t> firmware_mode) {
    std::vector<TitleTerminationNotification> callbacks;
    GuestMemory* active = nullptr;
    {
        std::lock_guard lock(registration_mutex);
        if (!guest) throw std::runtime_error("XamLoaderTerminateTitle without a bound title");
        if (terminating) {
            if (firmware_mode) throw FirmwareReturnRequested(*firmware_mode);
            throw TitleTerminated();
        }
        terminating = true;
        active = guest;
        callbacks = notifications;
    }
    // Xbox kernel invokes termination callbacks by descending priority.
    // Never hold registration_mutex while executing guest PPC callbacks.
    std::stable_sort(callbacks.begin(), callbacks.end(),
        [](const auto& a, const auto& b) { return a.priority > b.priority; });
    for (const auto& callback : callbacks) {
        active->find_function(callback.callback_address)(context, active->base());
    }
    // A terminated title must not continue executing PPC after this import.
    if (firmware_mode) throw FirmwareReturnRequested(*firmware_mode);
    throw TitleTerminated();
}
[[noreturn]] void xam_bound_loader_terminate_title(PPCContext& context) {
    dispatch_termination(context, std::nullopt);
}
[[noreturn]] void hal_bound_return_to_firmware(PPCContext& context,
                                                std::uint32_t routine) {
    dispatch_termination(context, routine);
}
}
