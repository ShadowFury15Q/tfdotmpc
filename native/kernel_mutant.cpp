#include "kernel_mutant.hpp"
#include "kernel_thread.hpp"
#include "guest_memory.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

namespace dotm {
namespace {
constexpr std::uint32_t kSuccess = 0;
constexpr std::uint32_t kInvalidHandle = 0xC0000008U;
constexpr std::uint32_t kInvalidParameter = 0xC000000DU;
constexpr std::uint32_t kNoMemory = 0xC0000017U;
constexpr std::uint32_t kNameCollision = 0xC0000035U;
constexpr std::uint32_t kTypeMismatch = 0xC0000024U;
constexpr std::uint32_t kMutantNotOwned = 0xC0000046U;
constexpr std::uint32_t kNotSupported = 0xC00000BBU;
constexpr std::uint32_t kTimeout = 0x00000102U;
constexpr std::uint32_t kCancelled = 0xC0000120U;
constexpr std::uint32_t kNameExists = 0x40000000U;
constexpr std::uint32_t kMutantLimitExceeded = 0xC0000191U;
constexpr std::uint32_t kFirstHandle = 0xF8000000U;
struct Mutant {
    std::mutex mutex;
    std::condition_variable ready;
    std::thread::id owner{};
    std::uint32_t depth = 0;
    std::atomic<bool> closing{false};
};
struct Event {
    std::mutex mutex;
    std::condition_variable ready;
    bool manual_reset = false;
    bool signaled = false;
    std::atomic<bool> closing{false};
    std::uint64_t pulse_epoch = 0;
    std::uint32_t pulse_tokens = 0;
    std::uint32_t waiters = 0;
};
std::mutex registry_mutex;
GuestMemory* bound_memory = nullptr;
std::uint32_t next_handle = kFirstHandle;
std::unordered_map<std::uint32_t, std::shared_ptr<Mutant>> handles;
std::unordered_map<std::string, std::weak_ptr<Mutant>> named;
std::unordered_map<std::uint32_t, std::shared_ptr<Event>> event_handles;
std::unordered_map<std::string, std::weak_ptr<Event>> named_events;

bool valid_guest(const GuestMemory& memory, std::uint32_t addr,
                 std::uint32_t size, bool writable) {
    const auto begin = std::uint64_t(addr);
    const auto end = begin + size;
    const auto table = GuestMemory::kFunctionTableOffset;
    return begin >= 0x10000U && end <= GuestMemory::kAddressSpaceSize &&
           !(writable && begin < table + GuestMemory::kFunctionTableSize && end > table) &&
           memory.committed_range(begin, size);
}
std::uint16_t read16(const GuestMemory& m, std::uint32_t a) {
    const auto* p = m.address(a);
    return std::uint16_t(std::uint16_t(p[0]) << 8 | p[1]);
}
std::uint32_t read32(const GuestMemory& m, std::uint32_t a) {
    const auto* p = m.address(a);
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
           (std::uint32_t(p[2]) << 8) | p[3];
}
std::uint64_t read64(const GuestMemory& m, std::uint32_t a) {
    return (std::uint64_t(read32(m, a)) << 32) | read32(m, a + 4);
}
void write32(GuestMemory& m, std::uint32_t a, std::uint32_t value) {
    auto* p = m.address(a);
    p[0] = std::uint8_t(value >> 24);
    p[1] = std::uint8_t(value >> 16);
    p[2] = std::uint8_t(value >> 8);
    p[3] = std::uint8_t(value);
}
// DOTM's PPC CRT constructs a 12-byte Xbox OBJECT_ATTRIBUTES:
// RootDirectory, PANSI_STRING ObjectName, Attributes (OBJ_OPENIF = 0x80).
std::uint32_t parse_name(const GuestMemory& m, std::uint32_t attributes,
                         std::optional<std::string>& result, bool& open_if) {
    open_if = false;
    if (!attributes) return kSuccess;
    if ((attributes & 3U) || !valid_guest(m, attributes, 12, false))
        return kInvalidParameter;
    const auto root = read32(m, attributes);
    const auto name = read32(m, attributes + 4);
    const auto flags = read32(m, attributes + 8);
    if ((root != 0 && root != 0xFFFFFFFCU) || (flags & ~0xC0U))
        return kNotSupported;
    open_if = (flags & 0x80U) != 0;
    if (!name) return kSuccess;
    if ((name & 3U) || !valid_guest(m, name, 8, false))
        return kInvalidParameter;
    const auto length = read16(m, name);
    const auto capacity = read16(m, name + 2);
    const auto data = read32(m, name + 4);
    if (length > capacity || length > 512 ||
        (length && !valid_guest(m, data, length, false)))
        return kInvalidParameter;
    if (!length) return kSuccess;
    std::string value(reinterpret_cast<const char*>(m.address(data)), length);
    if (flags & 0x40U) {
        for (auto& c : value)
            if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    }
    result = std::move(value);
    return kSuccess;
}
std::shared_ptr<Event> find_event(std::uint32_t handle) {
    std::lock_guard lock(registry_mutex);
    const auto it = event_handles.find(handle);
    return it == event_handles.end() ? nullptr : it->second;
}
std::shared_ptr<Mutant> find_mutant(std::uint32_t handle) {
    std::lock_guard lock(registry_mutex);
    const auto it = handles.find(handle);
    return it == handles.end() ? nullptr : it->second;
}
}
// A guest worker may be blocked in an infinite NtWait when the main PPC
// entry throws. Wake these waits before joining the workers: otherwise the
// host cannot report the original error and hangs during stack unwinding.
void cancel_kernel_mutant_waiters() noexcept {
    std::lock_guard lock(registry_mutex);
    for (auto& [handle, object] : handles) {
        object->closing.store(true, std::memory_order_release);
        object->ready.notify_all();
    }
    for (auto& [handle, object] : event_handles) {
        object->closing.store(true, std::memory_order_release);
        object->ready.notify_all();
    }
}

void bind_kernel_mutants(GuestMemory* memory) noexcept {
    std::lock_guard lock(registry_mutex);
    handles.clear();
    named.clear();
    event_handles.clear();
    named_events.clear();
    next_handle = kFirstHandle;
    bound_memory = memory;
}
std::uint32_t nt_bound_create_mutant(std::uint32_t output_handle,
                                     std::uint32_t object_attributes,
                                     std::uint32_t initial_owner) {
    std::lock_guard lock(registry_mutex);
    if (!bound_memory || (output_handle & 3U) ||
        !valid_guest(*bound_memory, output_handle, 4, true))
        return kInvalidParameter;
    std::optional<std::string> name;
    bool open_if = false;
    const auto status = parse_name(*bound_memory, object_attributes, name, open_if);
    if (status != kSuccess) return status;
    if (next_handle > 0xFFFFFFFBU) return kNoMemory;
    try {
        std::shared_ptr<Mutant> object;
        bool existed = false;
        if (name) {
            const auto event_it = named_events.find(*name);
            if (event_it != named_events.end() && !event_it->second.expired())
                return kTypeMismatch;
            const auto it = named.find(*name);
            if (it != named.end()) object = it->second.lock();
            existed = static_cast<bool>(object);
            if (existed && !open_if) return kNameCollision;
        }
        if (!object) {
            object = std::make_shared<Mutant>();
            if (initial_owner) {
                object->owner = std::this_thread::get_id();
                object->depth = 1;
            }
        }
        const auto handle = next_handle;
        handles.emplace(handle, object);
        try {
            if (name) named[*name] = object;
        } catch (...) {
            handles.erase(handle);
            throw;
        }
        next_handle += 4;
        write32(*bound_memory, output_handle, handle);
        return existed ? kNameExists : kSuccess;
    } catch (const std::bad_alloc&) {
        return kNoMemory;
    }
}
std::uint32_t nt_bound_create_event(std::uint32_t output_handle,
                                     std::uint32_t object_attributes,
                                     std::uint32_t event_type,
                                     std::uint32_t initial_state) {
    std::lock_guard lock(registry_mutex);
    if (!bound_memory || (output_handle & 3U) ||
        !valid_guest(*bound_memory, output_handle, 4, true) ||
        event_type > 1 || initial_state > 1)
        return kInvalidParameter;
    std::optional<std::string> name;
    bool open_if = false;
    const auto status = parse_name(*bound_memory, object_attributes, name, open_if);
    if (status != kSuccess) return status;
    if (next_handle > 0xFFFFFFFBU) return kNoMemory;
    try {
        std::shared_ptr<Event> object;
        bool existed = false;
        if (name) {
            const auto mutant_it = named.find(*name);
            if (mutant_it != named.end() && !mutant_it->second.expired())
                return kTypeMismatch;
            const auto it = named_events.find(*name);
            if (it != named_events.end()) object = it->second.lock();
            existed = static_cast<bool>(object);
            if (existed && !open_if) return kNameCollision;
        }
        if (!object) {
            object = std::make_shared<Event>();
            object->manual_reset = event_type == 0;
            object->signaled = initial_state != 0;
        }
        const auto handle = next_handle;
        event_handles.emplace(handle, object);
        try {
            if (name) named_events[*name] = object;
        } catch (...) {
            event_handles.erase(handle);
            throw;
        }
        next_handle += 4;
        write32(*bound_memory, output_handle, handle);
        return existed ? kNameExists : kSuccess;
    } catch (const std::bad_alloc&) {
        return kNoMemory;
    }
}

std::uint32_t nt_bound_set_event(std::uint32_t handle, std::uint32_t previous_state) {
    auto event = find_event(handle);
    if (!event) return kInvalidHandle;
    {
        std::lock_guard lock(registry_mutex);
        if (previous_state && (!bound_memory || (previous_state & 3U) ||
            !valid_guest(*bound_memory, previous_state, 4, true)))
            return kInvalidParameter;
    }
    std::unique_lock lock(event->mutex);
    const bool was_signaled = event->signaled;
    event->signaled = true;
    if (previous_state) {
        std::lock_guard registry_lock(registry_mutex);
        write32(*bound_memory, previous_state, was_signaled ? 1U : 0U);
    }
    lock.unlock();
    if (event->manual_reset) event->ready.notify_all();
    else event->ready.notify_one();
    return kSuccess;
}

std::uint32_t nt_bound_clear_event(std::uint32_t handle) {
    auto event = find_event(handle);
    if (!event) return kInvalidHandle;
    std::lock_guard lock(event->mutex);
    event->signaled = false;
    return kSuccess;
}

std::uint32_t nt_bound_pulse_event(std::uint32_t handle, std::uint32_t previous_state) {
    auto event = find_event(handle);
    if (!event) return kInvalidHandle;
    {
        std::lock_guard lock(registry_mutex);
        if (previous_state && (!bound_memory || (previous_state & 3U) ||
            !valid_guest(*bound_memory, previous_state, 4, true)))
            return kInvalidParameter;
    }
    std::unique_lock lock(event->mutex);
    const bool was_signaled = event->signaled;
    event->signaled = false;
    ++event->pulse_epoch;
    event->pulse_tokens = event->manual_reset ? event->waiters
                                               : std::min(event->waiters, 1U);
    if (previous_state) {
        std::lock_guard registry_lock(registry_mutex);
        write32(*bound_memory, previous_state, was_signaled ? 1U : 0U);
    }
    lock.unlock();
    event->ready.notify_all();
    return kSuccess;
}

std::uint32_t nt_bound_release_mutant(std::uint32_t handle,
                                      std::uint32_t previous_count) {
    auto object = find_mutant(handle);
    if (!object) return kInvalidHandle;
    {
        std::lock_guard lock(registry_mutex);
        if (previous_count && (!bound_memory || (previous_count & 3U) ||
            !valid_guest(*bound_memory, previous_count, 4, true)))
            return kInvalidParameter;
    }
    std::unique_lock lock(object->mutex);
    if (!object->depth || object->owner != std::this_thread::get_id())
        return kMutantNotOwned;
    if (previous_count) {
        std::lock_guard registry_lock(registry_mutex);
        write32(*bound_memory, previous_count, 1U - object->depth);
    }
    --object->depth;
    if (!object->depth) {
        object->owner = {};
        lock.unlock();
        object->ready.notify_one();
    }
    return kSuccess;
}
std::uint32_t nt_bound_wait_for_single_object_ex(std::uint32_t handle,
                                                  std::uint32_t wait_mode,
                                                  std::uint32_t alertable,
                                                  std::uint32_t timeout) {
    if (wait_mode > 1U || alertable > 1U) return kInvalidParameter;
    // APC delivery is not implemented. Do not claim an alertable wait worked.
    if (alertable) return kNotSupported;
    std::optional<std::chrono::nanoseconds> duration;
    {
        std::lock_guard lock(registry_mutex);
        if (timeout) {
            if (!bound_memory || (timeout & 7U) ||
                !valid_guest(*bound_memory, timeout, 8, false))
                return kInvalidParameter;
            const auto ticks = static_cast<std::int64_t>(read64(*bound_memory, timeout));
            std::uint64_t remaining = 0;
            if (ticks < 0) {
                remaining = std::uint64_t(-(ticks + 1)) + 1;
            } else if (ticks > 0) {
                using Hectonanoseconds = std::chrono::duration<std::int64_t, std::ratio<1, 10000000>>;
                constexpr std::int64_t epoch_delta = 116444736000000000LL;
                const auto now = std::chrono::duration_cast<Hectonanoseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                const auto absolute_now = now + epoch_delta;
                if (ticks > absolute_now) remaining = std::uint64_t(ticks - absolute_now);
            }
            constexpr auto max_ticks = std::uint64_t(std::numeric_limits<std::int64_t>::max() / 100);
            duration = std::chrono::nanoseconds(std::int64_t(std::min(remaining, max_ticks) * 100));
        }
    }
    auto object = find_mutant(handle);
    if (!object) {
        auto event = find_event(handle);
        if (!event) return nt_bound_wait_thread(handle, timeout);
        std::unique_lock lock(event->mutex);
        const auto pulse_at_start = event->pulse_epoch;
        ++event->waiters;
        const auto available = [&] {
            return event->closing.load(std::memory_order_acquire) ||
                event->signaled ||
                (event->pulse_epoch != pulse_at_start && event->pulse_tokens != 0);
        };
        bool awakened = true;
        if (duration) awakened = event->ready.wait_for(lock, *duration, available);
        else event->ready.wait(lock, available);
        --event->waiters;
        if (event->closing.load(std::memory_order_acquire)) {
            lock.unlock();
            if (kernel_guest_thread_active()) nt_bound_terminate_thread(kCancelled);
            return kCancelled;
        }
        if (!awakened) return kTimeout;
        if (event->pulse_epoch != pulse_at_start && event->pulse_tokens) {
            --event->pulse_tokens;
        } else if (!event->manual_reset) {
            event->signaled = false;
        }
        return kSuccess;
    }
    std::unique_lock lock(object->mutex);
    const auto current = std::this_thread::get_id();
    const auto available = [&] {
        return object->closing.load(std::memory_order_acquire) ||
            !object->depth || object->owner == current;
    };
    if (duration) {
        if (!object->ready.wait_for(lock, *duration, available)) return kTimeout;
    } else {
        object->ready.wait(lock, available);
    }
    if (object->closing.load(std::memory_order_acquire)) {
        lock.unlock();
        if (kernel_guest_thread_active()) nt_bound_terminate_thread(kCancelled);
        return kCancelled;
    }
    if (object->depth == 0x7FFFFFFFU) return kMutantLimitExceeded;
    object->owner = current;
    ++object->depth;
    return kSuccess;
}
std::uint32_t nt_bound_close_mutant(std::uint32_t handle) {
    {
        std::lock_guard lock(registry_mutex);
        if (handles.erase(handle) || event_handles.erase(handle)) return kSuccess;
    }
    return nt_bound_close_thread(handle);
}
}
