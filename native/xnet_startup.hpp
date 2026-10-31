#pragma once
#include "guest_memory.hpp"
#include <array>
#include <cstdint>
#include <mutex>

namespace dotm {
constexpr std::size_t kXNetStartupParamsBytes = 13;
constexpr std::uint32_t kXNetSuccess = 0;
constexpr std::uint32_t kXNetInvalidArgument = 10022;
constexpr std::uint32_t kXNetNotInitialized = 10093;
constexpr std::uint32_t kXNetFault = 10014;
constexpr std::uint32_t kXNetVersionNotSupported = 10092;
constexpr std::uint32_t kXNetNetworkDown = 10050;
constexpr std::size_t kXboxWsaDataBytes = 0x190;

class XNetStartupHost {
public:
    ~XNetStartupHost();
    std::uint32_t startup(GuestMemory& memory, std::uint32_t guest_params);
    std::uint32_t cleanup() noexcept;
    std::uint32_t wsa_startup(GuestMemory& memory, std::uint16_t version, std::uint32_t data_ptr);
    std::uint32_t wsa_cleanup() noexcept;
    std::uint32_t wsa_reference_count() const noexcept;
    std::uint32_t reference_count() const noexcept;
    std::array<std::uint8_t, kXNetStartupParamsBytes> configuration() const noexcept;
private:
    mutable std::mutex mutex_;
    std::array<std::uint8_t, kXNetStartupParamsBytes> configuration_{};
    std::uint32_t references_ = 0;
    std::uint32_t wsa_references_ = 0;
};

void bind_xnet_startup(XNetStartupHost* host, GuestMemory* memory) noexcept;
std::uint32_t xnet_bound_startup(std::uint32_t caller, std::uint32_t guest_params);
std::uint32_t xnet_bound_cleanup(std::uint32_t caller);
std::uint32_t xnet_bound_wsa_startup(std::uint32_t caller, std::uint16_t version, std::uint32_t data_ptr);
std::uint32_t xnet_bound_wsa_cleanup(std::uint32_t caller);
}
