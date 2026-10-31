#include "kernel_thread.hpp"
#include "guest_memory.hpp"
#include <chrono>
#include <algorithm>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <vector>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

namespace dotm {
namespace {
constexpr std::uint32_t kSuccess = 0;
constexpr std::uint32_t kInvalidHandle = 0xC0000008U;
constexpr std::uint32_t kInvalidParameter = 0xC000000DU;
constexpr std::uint32_t kNoMemory = 0xC0000017U;
constexpr std::uint32_t kNotSupported = 0xC00000BBU;
constexpr std::uint32_t kTimeout = 0x102U;
constexpr std::uint32_t kThreadBase = 0xF7000000U;
constexpr std::uint32_t kStackBase = 0x72000000U;
constexpr std::uint32_t kPcrBase = 0x78000000U;
constexpr std::uint32_t kStackBytes = 0x100000U;
constexpr std::uint32_t kPcrStride = 0x10000U;
constexpr std::uint32_t kMaxThreads = 32;
struct ThreadExit { std::uint32_t code; };
struct ThreadState {
    std::mutex mutex;
    std::condition_variable cv;
    std::thread worker;
    std::uint32_t handle = 0;
    std::uint32_t thread_id = 0;
    std::uint32_t suspend_count = 0;
    std::uint32_t exit_code = 0;
    bool finished = false;
    bool stop_requested = false;
    bool startup_ready = false;
};
std::mutex registry_mutex;
GuestMemory* bound_memory = nullptr;
std::unordered_map<std::uint32_t, std::shared_ptr<ThreadState>> handles;
std::vector<std::shared_ptr<ThreadState>> all_threads;
thread_local bool executing_guest_thread = false;

// Xenon hardware-thread affinity is encoded in creation flags bits 24..29.
// Map each requested logical Xenon CPU to a CPU permitted by the host process.
// Affinity must be established before the guest entry is allowed to run.
bool pin_guest_worker(std::thread& worker, unsigned guest_cpu) {
#if defined(_WIN32)
    DWORD_PTR process_mask = 0, system_mask = 0;
    if (!GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask))
        return false;
    std::vector<DWORD_PTR> allowed;
    for (unsigned bit = 0; bit < sizeof(DWORD_PTR) * 8; ++bit) {
        const DWORD_PTR mask = DWORD_PTR{1} << bit;
        if (process_mask & mask) allowed.push_back(mask);
    }
    return !allowed.empty() &&
        SetThreadAffinityMask(worker.native_handle(),
                              allowed[guest_cpu % allowed.size()]) != 0;
#elif defined(__linux__)
    cpu_set_t permitted;
    CPU_ZERO(&permitted);
    if (pthread_getaffinity_np(pthread_self(), sizeof(permitted), &permitted) != 0)
        return false;
    std::vector<int> allowed;
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
        if (CPU_ISSET(cpu, &permitted)) allowed.push_back(cpu);
    if (allowed.empty()) return false;
    cpu_set_t selected;
    CPU_ZERO(&selected);
    CPU_SET(allowed[guest_cpu % allowed.size()], &selected);
    return pthread_setaffinity_np(worker.native_handle(), sizeof(selected),
                                  &selected) == 0;
#else
    (void)worker;
    (void)guest_cpu;
    return false;
#endif
}

bool valid_guest(const GuestMemory& m, std::uint32_t address,
                 std::uint32_t length, bool writable) {
    const auto begin = std::uint64_t(address);
    const auto end = begin + length;
    const auto table = GuestMemory::kFunctionTableOffset;
    return begin >= 0x10000U && end <= GuestMemory::kAddressSpaceSize &&
        !(writable && begin < table + GuestMemory::kFunctionTableSize && end > table) &&
        m.committed_range(begin, length);
}
void write32(GuestMemory& m, std::uint32_t address, std::uint32_t value) {
    auto* p = m.address(address);
    for (int i = 0; i < 4; ++i) p[i] = std::uint8_t(value >> (24 - 8 * i));
}
std::uint32_t read32(const GuestMemory& m, std::uint32_t address) {
    const auto* p = m.address(address);
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
           (std::uint32_t(p[2]) << 8) | p[3];
}
std::uint64_t read64(const GuestMemory& m, std::uint32_t address) {
    return (std::uint64_t(read32(m, address)) << 32) | read32(m, address + 4);
}
bool valid_code(std::uint32_t address) {
    return !(address & 3U) && address >= GuestMemory::kCodeBase &&
        std::uint64_t(address) - GuestMemory::kCodeBase < GuestMemory::kCodeSize;
}
std::optional<std::chrono::nanoseconds> read_timeout(const GuestMemory& memory,
                                                       std::uint32_t address) {
    if (!address) return std::nullopt;
    const auto ticks = static_cast<std::int64_t>(read64(memory, address));
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
    return std::chrono::nanoseconds(std::int64_t(std::min(remaining, max_ticks) * 100));
}
}

void bind_kernel_threads(GuestMemory* memory) {
    std::vector<std::shared_ptr<ThreadState>> previous;
    {
        std::lock_guard lock(registry_mutex);
        previous.swap(all_threads);
        handles.clear();
        bound_memory = nullptr;
    }
    // Cancel guest workers that have not yet passed their initial suspend
    // gate. Otherwise unbinding a suspended thread deadlocks forever on join.
    // Running guest code is not forcibly interrupted: it must finish before
    // the guest address space can be released.
    for (const auto& state : previous) {
        if (state->worker.joinable() &&
            state->worker.get_id() == std::this_thread::get_id())
            throw std::logic_error("Cannot unbind guest threads from a guest worker");
        {
            std::lock_guard state_lock(state->mutex);
            state->stop_requested = true;
        }
        state->cv.notify_all();
    }
    for (const auto& state : previous)
        if (state->worker.joinable()) state->worker.join();
    std::lock_guard lock(registry_mutex);
    bound_memory = memory;
}

std::uint32_t nt_bound_create_thread(std::uint32_t handle_output,
    std::uint32_t stack_size, std::uint32_t thread_id_output,
    std::uint32_t api_startup, std::uint32_t start_address,
    std::uint32_t parameter, std::uint32_t creation_flags,
    GuestThreadEntry entry) {
    std::lock_guard lock(registry_mutex);
    if (!bound_memory || !entry || (handle_output & 3U) ||
        !valid_guest(*bound_memory, handle_output, 4, true) ||
        (thread_id_output && ((thread_id_output & 3U) ||
          !valid_guest(*bound_memory, thread_id_output, 4, true))) ||
        !valid_code(start_address) || (api_startup && !valid_code(api_startup)) ||
        stack_size > kStackBytes)
        return kInvalidParameter;
    constexpr std::uint32_t kHardwareAffinityMask = 0x3F000000U;
    if (creation_flags & ~(1U | kHardwareAffinityMask)) return kNotSupported;
    const auto affinity = (creation_flags & kHardwareAffinityMask) >> 24;
    if (affinity && (affinity & (affinity - 1U))) return kInvalidParameter;
    unsigned guest_cpu = 0;
    if (affinity) {
        while ((affinity & (1U << guest_cpu)) == 0) ++guest_cpu;
    }
    if (all_threads.size() >= kMaxThreads) return kNoMemory;
    const auto slot = static_cast<std::uint32_t>(all_threads.size());
    const auto stack = kStackBase + slot * kStackBytes;
    const auto pcr = kPcrBase + slot * kPcrStride;
    if (bound_memory->any_committed_range(stack, kStackBytes) ||
        bound_memory->any_committed_range(pcr, 0x3000))
        return kNoMemory;
    bool stack_committed = false, pcr_committed = false;
    try {
        bound_memory->commit(stack, kStackBytes);
        stack_committed = true;
        bound_memory->commit(pcr, 0x3000);
        pcr_committed = true;
        std::memset(bound_memory->address(stack), 0, kStackBytes);
        std::memset(bound_memory->address(pcr), 0, 0x3000);
        const auto top = stack + kStackBytes;
        const auto kthread = pcr + 0x1000U;
        const auto tls = pcr + 0x2000U;
        const auto id = 2U + slot;
        write32(*bound_memory, pcr, tls);
        write32(*bound_memory, pcr + 0x30U, pcr);
        write32(*bound_memory, pcr + 0x70U, top);
        write32(*bound_memory, pcr + 0x74U, stack);
        write32(*bound_memory, pcr + 0x100U, kthread);
        write32(*bound_memory, kthread + 0x5CU, top);
        write32(*bound_memory, kthread + 0x60U, stack);
        write32(*bound_memory, kthread + 0x68U, tls);
        write32(*bound_memory, kthread + 0x14CU, id);
        auto state = std::make_shared<ThreadState>();
        state->handle = kThreadBase + slot * 4U;
        state->thread_id = id;
        state->suspend_count = creation_flags & 1U;
        handles.emplace(state->handle, state);
        try {
            all_threads.push_back(state);
            try {
                auto* memory = bound_memory;
                state->worker = std::thread([state, memory, pcr, top, api_startup,
                                              start_address, parameter, entry] {
                    std::uint32_t exit_code = 0;
                    bool cancelled_before_start = false;
                    {
                        std::unique_lock lock(state->mutex);
                        state->cv.wait(lock, [&] {
                            return (state->startup_ready && state->suspend_count == 0) ||
                                   state->stop_requested;
                        });
                        cancelled_before_start = state->stop_requested;
                    }
                    if (!cancelled_before_start) {
                    executing_guest_thread = true;
                    try {
                        exit_code = entry(*memory, pcr, top - 0x100U,
                                          api_startup, start_address, parameter);
                    } catch (const ThreadExit& e) {
                        exit_code = e.code;
                    } catch (const std::exception& e) {
                        std::fprintf(stderr, "DOTM guest thread %u failed: %s\n",
                                     state->thread_id, e.what());
                        exit_code = 0xC0000005U;
                    } catch (...) {
                        std::fprintf(stderr, "DOTM guest thread %u failed: unknown exception\n",
                                     state->thread_id);
                        exit_code = 0xC0000005U;
                    }
                    executing_guest_thread = false;
                    }
                    {
                        std::lock_guard lock(state->mutex);
                        state->exit_code = cancelled_before_start ? 0xC0000120U : exit_code;
                        state->finished = true;
                    }
                    state->cv.notify_all();
                });
            } catch (...) {
                all_threads.pop_back();
                throw;
            }
        } catch (...) {
            handles.erase(state->handle);
            throw;
        }
        if (affinity && !pin_guest_worker(state->worker, guest_cpu)) {
            // A failed native affinity request must not run the guest entry.
            // Wake the startup gate, join, and roll back all guest allocations.
            {
                std::lock_guard state_lock(state->mutex);
                state->stop_requested = true;
            }
            state->cv.notify_all();
            state->worker.join();
            all_threads.pop_back();
            handles.erase(state->handle);
            bound_memory->decommit(pcr, 0x3000);
            bound_memory->decommit(stack, kStackBytes);
            return kNotSupported;
        }
        {
            std::lock_guard state_lock(state->mutex);
            state->startup_ready = true;
        }
        state->cv.notify_all();
        write32(*bound_memory, handle_output, state->handle);
        if (thread_id_output) write32(*bound_memory, thread_id_output, id);
        return kSuccess;
    } catch (const std::exception&) {
        if (pcr_committed) bound_memory->decommit(pcr, 0x3000);
        if (stack_committed) bound_memory->decommit(stack, kStackBytes);
        return kNoMemory;
    }
}

std::uint32_t nt_bound_resume_thread(std::uint32_t handle,
                                      std::uint32_t previous_suspend_count) {
    std::shared_ptr<ThreadState> state;
    {
        std::lock_guard lock(registry_mutex);
        const auto it = handles.find(handle);
        if (it == handles.end()) return kInvalidHandle;
        if (previous_suspend_count && (!bound_memory ||
            (previous_suspend_count & 3U) ||
            !valid_guest(*bound_memory, previous_suspend_count, 4, true)))
            return kInvalidParameter;
        state = it->second;
        std::lock_guard state_lock(state->mutex);
        const auto previous = state->suspend_count;
        if (previous_suspend_count) write32(*bound_memory, previous_suspend_count, previous);
        if (state->suspend_count) --state->suspend_count;
        if (previous == 1) state->cv.notify_all();
    }
    return kSuccess;
}

std::uint32_t nt_bound_wait_thread(std::uint32_t handle, std::uint32_t timeout) {
    std::shared_ptr<ThreadState> state;
    std::optional<std::chrono::nanoseconds> duration;
    {
        std::lock_guard lock(registry_mutex);
        const auto it = handles.find(handle);
        if (it == handles.end()) return kInvalidHandle;
        if (timeout && (!bound_memory || (timeout & 7U) ||
                        !valid_guest(*bound_memory, timeout, 8, false)))
            return kInvalidParameter;
        if (timeout) duration = read_timeout(*bound_memory, timeout);
        state = it->second;
    }
    std::unique_lock lock(state->mutex);
    const auto done = [&] { return state->finished; };
    if (duration) {
        if (!state->cv.wait_for(lock, *duration, done)) return kTimeout;
    } else {
        state->cv.wait(lock, done);
    }
    return kSuccess;
}

// Xbox 360 KeDelayExecutionThread consumes a signed, big-endian 100 ns
// interval. Negative values are relative; positive values are absolute
// NT system timestamps (100 ns since 1601-01-01 UTC).
std::uint32_t nt_bound_delay_execution_thread(std::uint32_t wait_mode,
    std::uint32_t alertable, std::uint32_t interval) {
    if (wait_mode > 1U || alertable > 1U || (interval & 7U))
        return kInvalidParameter;
    std::optional<std::chrono::nanoseconds> duration;
    {
        std::lock_guard lock(registry_mutex);
        if (!bound_memory || !interval ||
            !valid_guest(*bound_memory, interval, 8, false))
            return kInvalidParameter;
        duration = read_timeout(*bound_memory, interval);
    }
    // The current runtime has no queued guest APCs. An alertable delay
    // therefore completes normally rather than fabricating an APC wakeup.
    // Split extremely long waits to avoid steady_clock::now() overflow in
    // implementations of sleep_for; this remains an actual blocking delay.
    constexpr auto kMaxChunk = std::chrono::hours(24);
    auto remaining = *duration;
    while (remaining > std::chrono::nanoseconds::zero()) {
        const auto chunk = std::min(remaining,
            std::chrono::duration_cast<std::chrono::nanoseconds>(kMaxChunk));
        std::this_thread::sleep_for(chunk);
        remaining -= chunk;
    }
    if (*duration == std::chrono::nanoseconds::zero())
        std::this_thread::yield();
    return kSuccess;
}

std::uint32_t nt_bound_close_thread(std::uint32_t handle) {
    std::lock_guard lock(registry_mutex);
    return handles.erase(handle) ? kSuccess : kInvalidHandle;
}

[[noreturn]] void nt_bound_terminate_thread(std::uint32_t exit_code) {
    if (!executing_guest_thread)
        throw std::runtime_error("ExTerminateThread called outside a guest worker");
    throw ThreadExit{exit_code};
}
}
