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
};
std::mutex registry_mutex;
GuestMemory* bound_memory = nullptr;
std::unordered_map<std::uint32_t, std::shared_ptr<ThreadState>> handles;
std::vector<std::shared_ptr<ThreadState>> all_threads;
thread_local bool executing_guest_thread = false;

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
    // Bits 24..29 select an Xbox hardware thread; scheduling affinity is not
    // yet enforced. Reject it rather than silently claim that it worked.
    if (creation_flags & ~1U) return kNotSupported;
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
                            return state->suspend_count == 0 || state->stop_requested;
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
