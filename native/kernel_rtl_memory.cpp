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
std::uint32_t rtl_ntstatus_to_dos_error(std::uint32_t status) noexcept {
    // Documented Windows NTSTATUS -> Win32 errors used by Xbox 360 CRT/XAM.
    switch (status) {
    case 0x00000000U: return 0U;     // STATUS_SUCCESS / ERROR_SUCCESS
    case 0x00000103U: return 997U;   // STATUS_PENDING / ERROR_IO_PENDING
    case 0xC0000005U: return 998U;   // STATUS_ACCESS_VIOLATION / ERROR_NOACCESS
    case 0xC0000008U: return 6U;     // STATUS_INVALID_HANDLE
    case 0xC000000DU: return 87U;    // STATUS_INVALID_PARAMETER
    case 0xC000000FU: return 2U;     // STATUS_NO_SUCH_FILE
    case 0xC0000017U: return 8U;     // STATUS_NO_MEMORY
    case 0xC0000023U: return 122U;   // STATUS_BUFFER_TOO_SMALL
    case 0xC00000A0U: return 487U;   // STATUS_MEMORY_NOT_ALLOCATED
    case 0xC00000BBU: return 50U;    // STATUS_NOT_SUPPORTED
    case 0xC0000135U: return 126U;   // STATUS_DLL_NOT_FOUND
    case 0xC0000225U: return 1168U;  // STATUS_NOT_FOUND
    case 0xC0000262U: return 182U;   // STATUS_DRIVER_ORDINAL_NOT_FOUND
    case 0xC0000263U: return 127U;   // STATUS_DRIVER_ENTRYPOINT_NOT_FOUND
    default: return 317U;            // ERROR_MR_MID_NOT_FOUND
    }
}
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
