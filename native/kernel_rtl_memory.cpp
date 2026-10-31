#include "kernel_rtl_memory.hpp"
#include "guest_memory.hpp"
#include <cstdint>
#include <algorithm>
#include <vector>
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

void rtl_bound_init_ansi_string(std::uint32_t destination, std::uint32_t source) {
    const auto table = GuestMemory::kFunctionTableOffset;
    const auto end = std::uint64_t(destination) + 8U;
    if ((destination & 3U) || !bound_memory ||
        (destination < table + GuestMemory::kFunctionTableSize && end > table) ||
        !bound_memory->committed_range(destination, 8))
        throw std::runtime_error("RtlInitAnsiString: invalid guest ANSI_STRING");
    std::uint32_t length = 0;
    if (source) {
        // Xbox ANSI_STRING length is a 16-bit byte count. Scan only committed
        // guest bytes, never follow a host pointer or read past 0xFFFE bytes.
        while (length <= 0xFFFEU) {
            const auto address = std::uint64_t(source) + length;
            if (address >= GuestMemory::kAddressSpaceSize ||
                !bound_memory->committed_range(address, 1))
                throw std::runtime_error("RtlInitAnsiString: invalid source");
            if (!bound_memory->address(address)[0]) break;
            ++length;
        }
        if (length > 0xFFFEU)
            throw std::runtime_error("RtlInitAnsiString: source too long");
    }
    auto* out = bound_memory->address(destination);
    out[0] = std::uint8_t(length >> 8);
    out[1] = std::uint8_t(length);
    const auto maximum = source ? length + 1U : 0U;
    out[2] = std::uint8_t(maximum >> 8);
    out[3] = std::uint8_t(maximum);
    out[4] = std::uint8_t(source >> 24);
    out[5] = std::uint8_t(source >> 16);
    out[6] = std::uint8_t(source >> 8);
    out[7] = std::uint8_t(source);
}

void rtl_bound_init_unicode_string(std::uint32_t destination, std::uint32_t source) {
    const auto table = GuestMemory::kFunctionTableOffset;
    const auto end = std::uint64_t(destination) + 8U;
    if ((destination & 3U) || !bound_memory ||
        (destination < table + GuestMemory::kFunctionTableSize && end > table) ||
        !bound_memory->committed_range(destination, 8))
        throw std::runtime_error("RtlInitUnicodeString: invalid guest UNICODE_STRING");
    std::uint32_t length = 0;
    if (source) {
        if (source & 1U)
            throw std::runtime_error("RtlInitUnicodeString: unaligned source");
        while (length <= 0xFFFCU) {
            const auto address = std::uint64_t(source) + length;
            if (address >= GuestMemory::kAddressSpaceSize ||
                !bound_memory->committed_range(address, 2))
                throw std::runtime_error("RtlInitUnicodeString: invalid source");
            const auto* code = bound_memory->address(address);
            if (!code[0] && !code[1]) break;
            length += 2;
        }
        if (length > 0xFFFCU)
            throw std::runtime_error("RtlInitUnicodeString: source too long");
    }
    auto* out = bound_memory->address(destination);
    out[0] = std::uint8_t(length >> 8);
    out[1] = std::uint8_t(length);
    const auto maximum = source ? length + 2U : 0U;
    out[2] = std::uint8_t(maximum >> 8);
    out[3] = std::uint8_t(maximum);
    out[4] = std::uint8_t(source >> 24);
    out[5] = std::uint8_t(source >> 16);
    out[6] = std::uint8_t(source >> 8);
    out[7] = std::uint8_t(source);
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


namespace {
constexpr std::uint32_t kTextSuccess = 0;
constexpr std::uint32_t kTextOverflow = 0x80000005U;
constexpr std::uint32_t kTextInvalidParameter = 0xC000000DU;
constexpr std::uint32_t kTextNotSupported = 0xC00000BBU;
constexpr std::uint16_t kCp1252High[] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
};
std::uint16_t decode_cp1252(std::uint8_t c) {
    return c >= 0x80 && c < 0xA0 ? kCp1252High[c - 0x80] : c;
}
std::uint8_t encode_cp1252(std::uint16_t c) {
    if (c < 0x80 || (c >= 0xA0 && c <= 0xFF)) return std::uint8_t(c);
    for (std::uint32_t i = 0; i < 32; ++i)
        if (kCp1252High[i] == c) return std::uint8_t(0x80 + i);
    return '?'; // Windows ANSI default-character replacement.
}
bool valid_text_guest(const dotm::GuestMemory& m, std::uint32_t a,
                      std::uint32_t length, bool write) {
    if (!length) return true;
    const auto start = std::uint64_t(a);
    const auto end = start + length;
    const auto table = dotm::GuestMemory::kFunctionTableOffset;
    return start >= 0x10000U && end <= dotm::GuestMemory::kAddressSpaceSize &&
           !(write && start < table + dotm::GuestMemory::kFunctionTableSize && end > table) &&
           m.committed_range(start, length);
}
void put_text_u32(dotm::GuestMemory& m, std::uint32_t a, std::uint32_t value) {
    auto* p = m.address(a);
    p[0] = std::uint8_t(value >> 24); p[1] = std::uint8_t(value >> 16);
    p[2] = std::uint8_t(value >> 8); p[3] = std::uint8_t(value);
}
}
namespace dotm {
std::uint32_t rtl_bound_multi_byte_to_unicode(std::uint32_t output,
    std::uint32_t output_max, std::uint32_t written, std::uint32_t input,
    std::uint32_t input_size) {
    if (!bound_memory) return kTextNotSupported;
    auto& m = *bound_memory;
    if ((written && ((written & 3U) || !valid_text_guest(m, written, 4, true))) ||
        !valid_text_guest(m, input, input_size, false) ||
        !valid_text_guest(m, output, output_max, true)) return kTextInvalidParameter;
    const auto converted = std::min<std::uint32_t>(input_size, output_max / 2);
    // Snapshot the source so overlapping guest input/output is deterministic.
    std::vector<std::uint8_t> source;
    if (input_size) source.assign(m.address(input), m.address(input) + converted);
    for (std::uint32_t i = 0; i < converted; ++i) {
        const auto code = decode_cp1252(source[i]);
        m.address(output)[2*i] = std::uint8_t(code >> 8);
        m.address(output)[2*i+1] = std::uint8_t(code);
    }
    if (written) put_text_u32(m, written, converted * 2);
    return converted == input_size ? kTextSuccess : kTextOverflow;
}
std::uint32_t rtl_bound_unicode_to_multi_byte(std::uint32_t output,
    std::uint32_t output_max, std::uint32_t written, std::uint32_t input,
    std::uint32_t input_size) {
    if (!bound_memory) return kTextNotSupported;
    auto& m = *bound_memory;
    if ((input_size & 1U) ||
        (written && ((written & 3U) || !valid_text_guest(m, written, 4, true))) ||
        !valid_text_guest(m, input, input_size, false) ||
        !valid_text_guest(m, output, output_max, true)) return kTextInvalidParameter;
    const auto converted = std::min<std::uint32_t>(input_size / 2, output_max);
    std::vector<std::uint8_t> source;
    if (converted) source.assign(m.address(input), m.address(input) + 2*converted);
    for (std::uint32_t i = 0; i < converted; ++i) {
        const auto code = std::uint16_t(std::uint16_t(source[2*i]) << 8 | source[2*i+1]);
        m.address(output)[i] = encode_cp1252(code);
    }
    if (written) put_text_u32(m, written, converted);
    return converted * 2 == input_size ? kTextSuccess : kTextOverflow;
}
}
