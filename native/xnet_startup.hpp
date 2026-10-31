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

class XNetStartupHost {
public:
    std::uint32_t startup(GuestMemory& memory, std::uint32_t guest_params);
    std::uint32_t cleanup() noexcept;
    std::uint32_t reference_count() const noexcept;
    std::array<std::uint8_t, kXNetStartupParamsBytes> configuration() const noexcept;
private:
    mutable std::mutex mutex_;
    std::array<std::uint8_t, kXNetStartupParamsBytes> configuration_{};
    std::uint32_t references_ = 0;
};

void bind_xnet_startup(XNetStartupHost* host, GuestMemory* memory) noexcept;
std::uint32_t xnet_bound_startup(std::uint32_t caller, std::uint32_t guest_params);
std::uint32_t xnet_bound_cleanup(std::uint32_t caller);
}
