#include "kernel_critical_sections.hpp"
#include "guest_memory.hpp"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
namespace dotm {
namespace {
struct Section {
    std::recursive_mutex mutex;
    std::thread::id owner{};
    std::uint32_t recursion = 0;
    std::uint32_t spin_count = 0;
};
std::mutex registry_mutex;
GuestMemory* guest_memory = nullptr;
std::unordered_map<std::uint32_t, std::shared_ptr<Section>> registry;
std::atomic<std::uint32_t> next_thread_token{1};
thread_local std::uint32_t thread_token = next_thread_token.fetch_add(1);
constexpr std::uint32_t kSectionBytes = 28;
void validate(std::uint32_t addr) {
    if (!guest_memory || !addr || (addr & 3) ||
        !guest_memory->committed_range(addr, kSectionBytes))
        throw std::runtime_error("Xbox critical section address is not committed/aligned");
}
void write_u32(std::uint32_t address, std::uint32_t value) {
    auto* b = guest_memory->address(address);
    b[0] = static_cast<std::uint8_t>(value >> 24);
    b[1] = static_cast<std::uint8_t>(value >> 16);
    b[2] = static_cast<std::uint8_t>(value >> 8);
    b[3] = static_cast<std::uint8_t>(value);
}
std::uint32_t read_u32(std::uint32_t address) {
    const auto* b = guest_memory->address(address);
    return (std::uint32_t(b[0]) << 24) | (std::uint32_t(b[1]) << 16) |
           (std::uint32_t(b[2]) << 8) | std::uint32_t(b[3]);
}
void publish(std::uint32_t addr, const Section& s) {
    // 360 dispatcher header: 16 bytes, followed by lock count, depth, owner.
    write_u32(addr + 16, s.recursion ? s.recursion - 1 : 0xffffffffU);
    write_u32(addr + 20, s.recursion);
    // Placeholder thread identity is local to the native runtime, not a
    // guest KTHREAD address. Future guest threads must supply that address.
    write_u32(addr + 24, s.recursion ? thread_token : 0U);
}
std::shared_ptr<Section> get(std::uint32_t addr) {
    std::lock_guard lock(registry_mutex);
    validate(addr);
    const auto it = registry.find(addr);
    if (it != registry.end()) return it->second;
    // Images can have preinitialized sections, but never accept random bytes.
    if (guest_memory->address(addr)[0] != 1 || read_u32(addr + 16) != 0xffffffffU)
        throw std::runtime_error("Xbox critical section was never initialized");
    auto section = std::make_shared<Section>();
    section->spin_count = std::uint32_t(guest_memory->address(addr)[1]) << 8;
    registry.emplace(addr, section);
    return section;
}
}
void bind_kernel_critical_sections(GuestMemory* memory) {
    std::lock_guard lock(registry_mutex);
    registry.clear();
    guest_memory = memory;
}
void kernel_initialize_critical_section(std::uint32_t addr, std::uint32_t spin_count) {
    std::lock_guard lock(registry_mutex);
    validate(addr);
    const auto previous = registry.find(addr);
    if (previous != registry.end()) {
        if (!previous->second->mutex.try_lock())
            throw std::runtime_error("Cannot reinitialize owned Xbox critical section");
        const bool busy = previous->second->recursion != 0;
        previous->second->mutex.unlock();
        if (busy) throw std::runtime_error("Cannot reinitialize owned Xbox critical section");
    }
    auto state = std::make_shared<Section>();
    state->spin_count = spin_count;
    registry[addr] = state;
    std::memset(guest_memory->address(addr), 0, kSectionBytes);
    auto* header = guest_memory->address(addr);
    header[0] = 1;
    header[1] = static_cast<std::uint8_t>((std::min(spin_count, 0xff00U) + 255U) >> 8);
    publish(addr, *state);
}
void kernel_enter_critical_section(std::uint32_t addr) {
    auto s = get(addr);
    s->mutex.lock();
    s->owner = std::this_thread::get_id();
    ++s->recursion;
    publish(addr, *s);
}
bool kernel_try_enter_critical_section(std::uint32_t addr) {
    auto s = get(addr);
    if (!s->mutex.try_lock()) return false;
    s->owner = std::this_thread::get_id();
    ++s->recursion;
    publish(addr, *s);
    return true;
}
void kernel_leave_critical_section(std::uint32_t addr) {
    auto s = get(addr);
    if (!s->recursion || s->owner != std::this_thread::get_id())
        throw std::runtime_error("Xbox critical section released by non-owner");
    --s->recursion;
    if (!s->recursion) s->owner = {};
    publish(addr, *s);
    s->mutex.unlock();
}
}
