#include "xam_guest_input.hpp"
#include <array>
#include <atomic>
#include <cstdint>

namespace dotm {
namespace {
std::atomic<XamInputHost*> active_input{nullptr};
std::atomic<GuestMemory*> active_memory{nullptr};
bool valid_write(std::uint32_t address) {
    constexpr std::uint64_t n=kXboxInputStateBytes;
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
    if(!valid_write(guest_address))return kXamBadArguments;
    std::array<std::uint8_t,kXboxInputStateBytes> data{};
    const auto status=input.get_state(user,flags,data);
    if(status!=kXamOk)return status;
    memory.copy_to_guest(guest_address,data);
    return kXamOk;
}
void bind_xam_guest_input(XamInputHost* input,GuestMemory* memory) noexcept {
    // Host must unbind before releasing either service, and the caller must
    // synchronize replacement against any concurrently executing guest calls.
    active_input.store(input,std::memory_order_release);
    active_memory.store(memory,std::memory_order_release);
}
std::uint32_t xam_bound_input_get_state(
    std::uint32_t user,std::uint32_t flags,std::uint32_t guest_address) {
    auto* const in=active_input.load(std::memory_order_acquire);
    auto* const mem=active_memory.load(std::memory_order_acquire);
    if(!in||!mem)return kXamDeviceNotConnected;
    return xam_input_get_state_guest(*in,*mem,user,flags,guest_address);
}
}
