#include "xnet_startup.hpp"
#include <algorithm>
#include <limits>
#include <cstring>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace dotm {
namespace {
std::mutex binding_mutex;
XNetStartupHost* bound_host = nullptr;
GuestMemory* bound_memory = nullptr;

bool valid_guest_params(const GuestMemory& memory, std::uint32_t address, std::size_t bytes) noexcept {
    const std::uint64_t begin = address;
    const std::uint64_t end = begin + bytes;
    const std::uint64_t table_begin = GuestMemory::kFunctionTableOffset;
    const std::uint64_t table_end = table_begin + GuestMemory::kFunctionTableSize;
    return begin >= 0x10000U && end <= GuestMemory::kAddressSpaceSize &&
           !(begin < table_end && end > table_begin) &&
           memory.committed_range(address, bytes);
}
}

namespace {
void store_be16(std::uint8_t* out, std::uint16_t value) noexcept {
    out[0] = static_cast<std::uint8_t>(value >> 8);
    out[1] = static_cast<std::uint8_t>(value);
}
}

XNetStartupHost::~XNetStartupHost() {
#ifdef _WIN32
    std::lock_guard lock(mutex_);
    while (wsa_references_ != 0) {
        WSACleanup();
        --wsa_references_;
    }
#endif
}

std::uint32_t XNetStartupHost::startup(GuestMemory& memory, std::uint32_t guest_params) {
    std::array<std::uint8_t, kXNetStartupParamsBytes> params{};
    if (guest_params != 0) {
        if (!valid_guest_params(memory, guest_params, kXNetStartupParamsBytes)) return kXNetInvalidArgument;
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

std::uint32_t XNetStartupHost::wsa_startup(GuestMemory& memory,
                                           std::uint16_t version, std::uint32_t data_ptr) {
    if (!valid_guest_params(memory, data_ptr, kXboxWsaDataBytes)) return kXNetFault;
    const auto major = static_cast<std::uint8_t>(version);
    const auto minor = static_cast<std::uint8_t>(version >> 8);
    if (major == 0 || major > 2 || minor > 2) return kXNetVersionNotSupported;
    std::array<std::uint8_t, kXboxWsaDataBytes> data{};
    std::uint16_t negotiated_version = version;
    std::uint16_t highest_version = 0x0202;
    std::uint16_t max_sockets = 0;
    std::uint16_t max_datagram = 0;
    std::lock_guard lock(mutex_);
    if (wsa_references_ == std::numeric_limits<std::uint32_t>::max())
        return kXNetInvalidArgument;
#ifdef _WIN32
    WSADATA host_data{};
    const int status = WSAStartup(version, &host_data);
    if (status != 0) return static_cast<std::uint32_t>(status);
    negotiated_version = host_data.wVersion;
    highest_version = host_data.wHighVersion;
    max_sockets = host_data.iMaxSockets;
    max_datagram = host_data.iMaxUdpDg;
    std::memcpy(data.data() + 4, host_data.szDescription, 257);
    std::memcpy(data.data() + 261, host_data.szSystemStatus, 129);
#else
    // POSIX sockets need no process-wide WSAStartup. Verify IPv4 support.
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return kXNetNetworkDown;
    ::close(fd);
    constexpr char description[] = "POSIX sockets";
    constexpr char status[] = "Running";
    std::memcpy(data.data() + 4, description, sizeof(description));
    std::memcpy(data.data() + 261, status, sizeof(status));
#endif
    store_be16(data.data(), negotiated_version);
    store_be16(data.data() + 2, highest_version);
    store_be16(data.data() + 390, max_sockets);
    store_be16(data.data() + 392, max_datagram);
    // Xbox WSADATA's 32-bit vendor pointer at 0x18C must not expose a host pointer.
    std::memcpy(memory.address(data_ptr), data.data(), data.size());
    ++wsa_references_;
    return kXNetSuccess;
}

std::uint32_t XNetStartupHost::wsa_cleanup() noexcept {
    std::lock_guard lock(mutex_);
    if (wsa_references_ == 0) return kXNetNotInitialized;
#ifdef _WIN32
    if (WSACleanup() != 0) return static_cast<std::uint32_t>(WSAGetLastError());
#endif
    --wsa_references_;
    return kXNetSuccess;
}

std::uint32_t XNetStartupHost::wsa_reference_count() const noexcept {
    std::lock_guard lock(mutex_);
    return wsa_references_;
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

namespace dotm {
std::uint32_t xnet_bound_wsa_startup(std::uint32_t caller, std::uint16_t version, std::uint32_t data_ptr) {
    (void)caller;
    std::lock_guard lock(binding_mutex);
    if (!bound_host || !bound_memory) return kXNetNotInitialized;
    return bound_host->wsa_startup(*bound_memory, version, data_ptr);
}
std::uint32_t xnet_bound_wsa_cleanup(std::uint32_t caller) {
    (void)caller;
    std::lock_guard lock(binding_mutex);
    if (!bound_host) return kXNetNotInitialized;
    return bound_host->wsa_cleanup();
}
}
