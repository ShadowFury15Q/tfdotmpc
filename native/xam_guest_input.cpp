#include "xam_guest_input.hpp"
#include <array>
#include <mutex>
#include <cstdint>

namespace dotm {
namespace {
std::mutex active_mutex;
XamInputHost* active_input=nullptr;
GuestMemory* active_memory=nullptr;
bool valid_write(std::uint32_t address,std::size_t bytes) {
    const std::uint64_t n=bytes;
    constexpr std::uint64_t table_begin=GuestMemory::kFunctionTableOffset;
    constexpr std::uint64_t table_end=table_begin+GuestMemory::kFunctionTableSize;
    const std::uint64_t begin=address;
    const std::uint64_t end=begin+n;
    return begin>=0x10000u &&
           end<=GuestMemory::kAddressSpaceSize &&
           !(begin<table_end && end>table_begin);
}
}
std::uint32_t xam_input_get_state_guest(
    XamInputHost& input,GuestMemory& memory,
    std::uint32_t user,std::uint32_t flags,std::uint32_t guest_address) {
    if(guest_address==0) {
        // Original XAM accepts null pointers as presence queries in some
        // titles; return the controller connection state without guest writes.
        std::array<std::uint8_t,kXboxInputStateBytes> scratch{};
        return input.get_state(user,flags,scratch);
    }
    if(!valid_write(guest_address,kXboxInputStateBytes))return kXamBadArguments;
    std::array<std::uint8_t,kXboxInputStateBytes> data{};
    const auto status=input.get_state(user,flags,data);
    if(status!=kXamOk)return status;
    memory.copy_to_guest(guest_address,data);
    return kXamOk;
}

std::uint32_t xam_input_get_capabilities_guest(
    XamInputHost& input,GuestMemory& memory,
    std::uint32_t user,std::uint32_t flags,std::uint32_t guest_address) {
    // Unlike GetState, original XAM requires a non-null caps pointer.
    if(!valid_write(guest_address,kXboxInputCapabilitiesBytes))
        return kXamBadArguments;
    std::array<std::uint8_t,kXboxInputCapabilitiesBytes> data{};
    const auto status=input.get_capabilities(user,flags,data);
    if(status!=kXamOk)return status;
    memory.copy_to_guest(guest_address,data);
    return kXamOk;
}
void bind_xam_guest_input(XamInputHost* input,GuestMemory* memory) noexcept {
    std::lock_guard lock(active_mutex);
    // Protect the pair so a guest never sees a half-updated binding.
    active_input=input;
    active_memory=memory;
}

std::uint32_t xam_bound_input_get_capabilities(
    std::uint32_t user,std::uint32_t flags,std::uint32_t guest_address) {
    std::lock_guard lock(active_mutex);
    if(!active_input||!active_memory)return kXamDeviceNotConnected;
    return xam_input_get_capabilities_guest(
        *active_input,*active_memory,user,flags,guest_address);
}
std::uint32_t xam_bound_input_get_state(
    std::uint32_t user,std::uint32_t flags,std::uint32_t guest_address) {
    std::lock_guard lock(active_mutex);
    auto* const in=active_input;
    auto* const mem=active_memory;
    if(!in||!mem)return kXamDeviceNotConnected;
    return xam_input_get_state_guest(*in,*mem,user,flags,guest_address);
}
}
