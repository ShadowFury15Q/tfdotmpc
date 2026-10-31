#pragma once
#include <cstdint>
#include <mutex>
#include <vector>
namespace dotm {
class GuestMemory;
class XamLaunchDataHost {
public:
    std::uint32_t set(GuestMemory& memory, std::uint32_t guest_address,
                      std::uint32_t length);
    std::uint32_t get_size(GuestMemory& memory, std::uint32_t output_address);
    std::uint32_t get(GuestMemory& memory, std::uint32_t guest_address,
                      std::uint32_t capacity);
private:
    std::mutex mutex_;
    std::vector<std::uint8_t> bytes_;
    bool present_ = false;
};
void bind_xam_launch_data(XamLaunchDataHost* host, GuestMemory* memory) noexcept;
std::uint32_t xam_bound_launch_set(std::uint32_t address, std::uint32_t length);
std::uint32_t xam_bound_launch_size(std::uint32_t output_address);
std::uint32_t xam_bound_launch_get(std::uint32_t address, std::uint32_t capacity);
}
