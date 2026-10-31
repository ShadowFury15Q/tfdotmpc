#include "xam_launch_data.hpp"
#include "guest_memory.hpp"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <stdexcept>
namespace dotm {
namespace {
constexpr std::uint32_t kSuccess = 0;
constexpr std::uint32_t kInvalidParameter = 87;
constexpr std::uint32_t kNotFound = 1168;
constexpr std::uint32_t kMaxLaunchBytes = 1024U * 1024U;
std::atomic<XamLaunchDataHost*> bound_host{nullptr};
std::atomic<GuestMemory*> bound_memory{nullptr};
std::uint32_t read_validated_size(std::uint32_t n) {
    return n <= kMaxLaunchBytes ? kSuccess : kInvalidParameter;
}
void store_be32(GuestMemory& memory, std::uint32_t address, std::uint32_t size) {
    auto* out = memory.address(address);
    out[0] = static_cast<std::uint8_t>(size >> 24);
    out[1] = static_cast<std::uint8_t>(size >> 16);
    out[2] = static_cast<std::uint8_t>(size >> 8);
    out[3] = static_cast<std::uint8_t>(size);
}
std::pair<XamLaunchDataHost*, GuestMemory*> bindings() {
    auto* host = bound_host.load(std::memory_order_acquire);
    auto* memory = bound_memory.load(std::memory_order_acquire);
    if (!host || !memory)
        throw std::runtime_error("XAM launch-data runtime not bound");
    return {host, memory};
}
}
std::uint32_t XamLaunchDataHost::set(GuestMemory& memory,
                                     std::uint32_t guest_address,
                                     std::uint32_t length) {
    if (read_validated_size(length) != kSuccess ||
        (length && !memory.committed_range(guest_address, length)))
        return kInvalidParameter;
    std::vector<std::uint8_t> new_data;
    if (length) {
        const auto* begin = memory.address(guest_address);
        new_data.assign(begin, begin + length);
    }
    std::lock_guard lock(mutex_);
    bytes_ = std::move(new_data);
    present_ = length != 0;
    return kSuccess;
}
std::uint32_t XamLaunchDataHost::get_size(GuestMemory& memory,
                                          std::uint32_t output_address) {
    if (!memory.committed_range(output_address, 4))
        return kInvalidParameter;
    std::lock_guard lock(mutex_);
    store_be32(memory, output_address,
               static_cast<std::uint32_t>(bytes_.size()));
    return present_ ? kSuccess : kNotFound;
}
std::uint32_t XamLaunchDataHost::get(GuestMemory& memory,
                                     std::uint32_t guest_address,
                                     std::uint32_t capacity) {
    std::lock_guard lock(mutex_);
    if (!present_) return kNotFound;
    const auto copy_size = static_cast<std::uint32_t>(
        std::min<std::size_t>(bytes_.size(), capacity));
    if (copy_size && !memory.committed_range(guest_address, copy_size))
        return kInvalidParameter;
    if (copy_size)
        std::memcpy(memory.address(guest_address), bytes_.data(), copy_size);
    return kSuccess;
}
void bind_xam_launch_data(XamLaunchDataHost* host, GuestMemory* memory) noexcept {
    bound_memory.store(memory, std::memory_order_release);
    bound_host.store(host, std::memory_order_release);
}
std::uint32_t xam_bound_launch_set(std::uint32_t address,
                                   std::uint32_t length) {
    const auto [host, memory] = bindings();
    return host->set(*memory, address, length);
}
std::uint32_t xam_bound_launch_size(std::uint32_t output_address) {
    const auto [host, memory] = bindings();
    return host->get_size(*memory, output_address);
}
std::uint32_t xam_bound_launch_get(std::uint32_t address,
                                   std::uint32_t capacity) {
    const auto [host, memory] = bindings();
    return host->get(*memory, address, capacity);
}
}
