#include "xnet_startup.hpp"
#include <algorithm>
#include <limits>

namespace dotm {
namespace {
std::mutex binding_mutex;
XNetStartupHost* bound_host = nullptr;
GuestMemory* bound_memory = nullptr;

bool valid_guest_params(const GuestMemory& memory, std::uint32_t address) noexcept {
    const std::uint64_t begin = address;
    const std::uint64_t end = begin + kXNetStartupParamsBytes;
    const std::uint64_t table_begin = GuestMemory::kFunctionTableOffset;
    const std::uint64_t table_end = table_begin + GuestMemory::kFunctionTableSize;
    return begin >= 0x10000U && end <= GuestMemory::kAddressSpaceSize &&
           !(begin < table_end && end > table_begin) &&
           memory.committed_range(address, kXNetStartupParamsBytes);
}
}

std::uint32_t XNetStartupHost::startup(GuestMemory& memory, std::uint32_t guest_params) {
    std::array<std::uint8_t, kXNetStartupParamsBytes> params{};
    if (guest_params != 0) {
        if (!valid_guest_params(memory, guest_params)) return kXNetInvalidArgument;
        std::copy_n(memory.address(guest_params), params.size(), params.begin());
        if (params[0] != kXNetStartupParamsBytes) return kXNetInvalidArgument;
    }
    std::lock_guard lock(mutex_);
    if (references_ == std::numeric_limits<std::uint32_t>::max())
        return kXNetInvalidArgument;
    configuration_ = params;
    ++references_;
    return kXNetSuccess;
}

std::uint32_t XNetStartupHost::cleanup() noexcept {
    std::lock_guard lock(mutex_);
    if (references_ != 0 && --references_ == 0) configuration_.fill(0);
    return kXNetSuccess;
}

std::uint32_t XNetStartupHost::reference_count() const noexcept {
    std::lock_guard lock(mutex_);
    return references_;
}

std::array<std::uint8_t, kXNetStartupParamsBytes> XNetStartupHost::configuration() const noexcept {
    std::lock_guard lock(mutex_);
    return configuration_;
}

void bind_xnet_startup(XNetStartupHost* host, GuestMemory* memory) noexcept {
    std::lock_guard lock(binding_mutex);
    bound_host = host;
    bound_memory = memory;
}

std::uint32_t xnet_bound_startup(std::uint32_t caller, std::uint32_t guest_params) {
    (void)caller;
    std::lock_guard lock(binding_mutex);
    if (!bound_host || !bound_memory) return kXNetNotInitialized;
    return bound_host->startup(*bound_memory, guest_params);
}

std::uint32_t xnet_bound_cleanup(std::uint32_t caller) {
    (void)caller;
    std::lock_guard lock(binding_mutex);
    if (!bound_host) return kXNetNotInitialized;
    return bound_host->cleanup();
}
}
