#include "kernel_rtl_memory.hpp"
#include "guest_memory.hpp"
#include <cstdint>
#include <stdexcept>

namespace {
thread_local dotm::GuestMemory* bound_memory = nullptr;

dotm::GuestMemory& checked_range(std::uint32_t address, std::uint32_t bytes) {
    if (!bound_memory || !bound_memory->committed_range(address, bytes))
        throw std::runtime_error("Xbox RTL memory import accessed uncommitted guest memory");
    return *bound_memory;
}
}

namespace dotm {
void bind_kernel_rtl_memory(GuestMemory* memory) noexcept {
    bound_memory = memory;
}

void rtl_bound_fill_memory_ulong(std::uint32_t destination,
                                 std::uint32_t length,
                                 std::uint32_t pattern) {
    // RtlFillMemoryUlong writes complete ULONGs only; any trailing 1-3 bytes
    // remain untouched. A zero-length call does not dereference Destination.
    const auto bytes = length & ~std::uint32_t{3};
    if (!bytes) return;
    if (destination & 3U)
        throw std::runtime_error("RtlFillMemoryUlong requires DWORD alignment");
    auto& memory = checked_range(destination, bytes);
    auto* target = memory.address(destination);
    for (std::uint32_t i = 0; i < bytes; i += 4) {
        // The Xbox 360 guest is big-endian regardless of the host CPU.
        target[i]     = static_cast<std::uint8_t>(pattern >> 24);
        target[i + 1] = static_cast<std::uint8_t>(pattern >> 16);
        target[i + 2] = static_cast<std::uint8_t>(pattern >> 8);
        target[i + 3] = static_cast<std::uint8_t>(pattern);
    }
}

std::uint32_t rtl_bound_compare_memory_ulong(std::uint32_t source,
                                              std::uint32_t length,
                                              std::uint32_t pattern) {
    // Documented NT contract: invalid alignment/length returns zero.
    if ((source & 3U) || (length & 3U) || !length) return 0;
    auto& memory = checked_range(source, length);
    const auto* data = memory.address(source);
    for (std::uint32_t i = 0; i < length; i += 4) {
        if (data[i] != static_cast<std::uint8_t>(pattern >> 24) ||
            data[i + 1] != static_cast<std::uint8_t>(pattern >> 16) ||
            data[i + 2] != static_cast<std::uint8_t>(pattern >> 8) ||
            data[i + 3] != static_cast<std::uint8_t>(pattern))
            return i;
    }
    return length;
}
}
