#include "kernel_guest_memory.hpp"
#include <cstdint>
#include <array>
#include <cstdio>
#include <string>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>
#include <unordered_map>
#include <mutex>
#include <new>
#include <stdexcept>

namespace dotm {
namespace {
constexpr std::uint32_t kMemCommit = 0x1000U;
constexpr std::uint32_t kMemReserve = 0x2000U;
constexpr std::uint32_t kMemDecommit = 0x4000U;
constexpr std::uint32_t kMemRelease = 0x8000U;
constexpr std::uint32_t kPageReadWrite = 0x04U;
constexpr std::uint32_t kXboxVideoWidth = 1280U;
constexpr std::uint32_t kXboxVideoHeight = 720U;
constexpr std::uint32_t kXboxVideoWidescreen = 1U;
// Xbox 360 flags: large 64 KiB pages, heap allocation, and NOZERO.
constexpr std::uint32_t kXboxLargePages = 0x20000000U;
constexpr std::uint32_t kXboxHeap = 0x40000000U;
constexpr std::uint32_t kXboxNoZero = 0x00800000U;
constexpr std::uint32_t kXboxKnownFlags =
    kXboxLargePages | kXboxHeap | kXboxNoZero;
constexpr std::uint64_t kXboxLargePageSize = 0x10000ULL;
std::mutex binding_mutex;
GuestMemory* bound_memory = nullptr;
GuestVirtualAllocator* bound_allocator = nullptr;
struct PoolAllocation {
    std::uint32_t requested_bytes;
    std::uint32_t tag;
};
std::unordered_map<std::uint32_t, PoolAllocation> pool_allocations;

// Physical allocations are keyed by the 512 MiB physical offset. The three
// Xbox physical views use different page granularities but share this budget.
constexpr std::uint64_t kPhysicalBytes = 0x20000000ULL;
constexpr std::uint32_t kPhysical4kView = 0xE0000000U;
constexpr std::uint32_t kPhysical64kView = 0xA0000000U;
constexpr std::uint32_t kPhysical16mView = 0xC0000000U;
struct PhysicalBlock {
    std::uint32_t guest_address;
    std::uint32_t length;
    std::uint32_t protect;
};
std::map<std::uint32_t, PhysicalBlock> physical_blocks;
bool physical_overlaps(std::uint64_t offset, std::uint64_t length) {
    auto it = physical_blocks.lower_bound(static_cast<std::uint32_t>(offset));
    if (it != physical_blocks.end() && it->first < offset + length) return true;
    if (it != physical_blocks.begin()) {
        --it;
        if (std::uint64_t(it->first) + it->second.length > offset) return true;
    }
    return false;
}

bool writable_word(const GuestMemory& memory, std::uint32_t address) {
    const std::uint64_t begin = address;
    const std::uint64_t end = begin + 4;
    const std::uint64_t table = GuestMemory::kFunctionTableOffset;
    return begin >= 0x10000U && end <= GuestMemory::kAddressSpaceSize &&
           !(begin < table + GuestMemory::kFunctionTableSize && end > table) &&
           memory.committed_range(begin, 4);
}
bool valid_words(const GuestMemory& memory, std::uint32_t base,
                 std::uint32_t size) {
    const auto b = static_cast<std::uint64_t>(base);
    const auto s = static_cast<std::uint64_t>(size);
    return writable_word(memory, base) && writable_word(memory, size) &&
           (b + 4 <= s || s + 4 <= b);
}
std::uint32_t read_be32(const GuestMemory& memory, std::uint32_t address) {
    const auto* p = memory.address(address);
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
           (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
}
void write_be32(GuestMemory& memory, std::uint32_t address,
                std::uint32_t value) {
    auto* p = memory.address(address);
    p[0] = static_cast<std::uint8_t>(value >> 24);
    p[1] = static_cast<std::uint8_t>(value >> 16);
    p[2] = static_cast<std::uint8_t>(value >> 8);
    p[3] = static_cast<std::uint8_t>(value);
}
}
std::uint32_t nt_allocate_virtual_memory_guest(
    GuestMemory& memory, GuestVirtualAllocator& allocator,
    std::uint32_t base_pointer, std::uint32_t size_pointer,
    std::uint32_t allocation_type, std::uint32_t protect,
    std::uint32_t debug_memory) {
    if (!valid_words(memory, base_pointer, size_pointer) || debug_memory)
        return kNtInvalidParameter;
    // DOTM CRT requests HEAP|LARGE_PAGES (0x60000000) and later
    // LARGE_PAGES|NOZERO (0x20800000). Unknown flags fail closed.
    const auto extra_flags = allocation_type & ~(kMemCommit | kMemReserve);
    if (extra_flags & ~kXboxKnownFlags) return kNtNotSupported;
    const bool large_pages = (extra_flags & kXboxLargePages) != 0;
    if ((extra_flags & kXboxHeap) && !large_pages) return kNtNotSupported;
    allocation_type &= ~kXboxKnownFlags;
    if (allocation_type != (kMemCommit | kMemReserve) &&
        allocation_type != kMemReserve && allocation_type != kMemCommit)
        return kNtNotSupported;
    if (protect != kPageReadWrite)
        return kNtNotSupported;
    const auto requested_base = read_be32(memory, base_pointer);
    const auto requested_size = read_be32(memory, size_pointer);
    if (!requested_size) return kNtInvalidParameter;
    const std::uint64_t page = large_pages ? kXboxLargePageSize :
        GuestVirtualAllocator::kPageSize;
    if (large_pages && requested_base && (requested_base & (page - 1)))
        return kNtInvalidParameter;
    const std::uint64_t aligned_base = std::uint64_t(requested_base) & ~(page - 1);
    const std::uint64_t offset = std::uint64_t(requested_base) - aligned_base;
    const std::uint64_t needed = std::uint64_t(requested_size) + offset;
    if (needed > 0xFFFFFFFFULL) return kNtInvalidParameter;
    const std::uint64_t rounded = (needed + page - 1) & ~(page - 1);
    if (rounded > 0xFFFFFFFFULL ||
        (requested_base && aligned_base + rounded > GuestMemory::kAddressSpaceSize))
        return kNtInvalidParameter;
    try {
        std::optional<std::uint32_t> allocated;
        if (allocation_type == kMemCommit && requested_base) {
            if (!allocator.reserved_range(static_cast<std::uint32_t>(aligned_base), rounded))
                return kNtMemoryNotAllocated;
            if (allocator.commit_reserved(static_cast<std::uint32_t>(aligned_base), rounded))
                allocated = static_cast<std::uint32_t>(aligned_base);
            else
                return kNtNotSupported;
        } else if (allocation_type == kMemReserve) {
            if (requested_base) {
                if (allocator.reserve_at(static_cast<std::uint32_t>(aligned_base), rounded))
                    allocated = static_cast<std::uint32_t>(aligned_base);
            } else {
                allocated = allocator.reserve(rounded, page);
            }
        } else if (requested_base) {
            if (allocator.allocate_at(static_cast<std::uint32_t>(aligned_base), rounded))
                allocated = static_cast<std::uint32_t>(aligned_base);
        } else {
            // Null-base MEM_COMMIT implicitly reserves and commits.
            allocated = allocator.allocate(rounded, page);
        }
        if (!allocated) return kNtNoMemory;
        write_be32(memory, base_pointer, *allocated);
        write_be32(memory, size_pointer, static_cast<std::uint32_t>(rounded));
        return kNtSuccess;
    } catch (const std::bad_alloc&) {
        return kNtNoMemory;
    } catch (const std::exception&) {
        return kNtNoMemory;
    }
}
std::uint32_t nt_query_virtual_memory_guest(
    GuestMemory& memory, const GuestVirtualAllocator& allocator,
    std::uint32_t address, std::uint32_t output_pointer) {
    constexpr std::uint32_t kOutputSize = 28U;
    const auto start = std::uint64_t(output_pointer);
    const auto end = start + kOutputSize;
    const auto table = GuestMemory::kFunctionTableOffset;
    if (start < 0x10000U || end > GuestMemory::kAddressSpaceSize ||
        (start < table + GuestMemory::kFunctionTableSize && end > table) ||
        !memory.committed_range(start, kOutputSize))
        return kNtInvalidParameter;
    const auto region = allocator.query_region(address);
    if (!region) return kNtInvalidParameter;
    const std::uint32_t words[] = {
        region->base_address, region->allocation_base,
        region->allocation_protect, region->region_size,
        region->state, region->protect, region->type};
    for (std::uint32_t index = 0; index < 7; ++index)
        write_be32(memory, output_pointer + index * 4, words[index]);
    return kNtSuccess;
}
std::uint32_t mm_query_statistics_guest(
    GuestMemory& memory, const GuestVirtualAllocator& allocator,
    std::uint32_t output_pointer) {
    constexpr std::uint32_t kStatisticsBytes = 104U;
    constexpr std::uint32_t kTotalPhysicalPages = 0x20000U;
    constexpr std::uint32_t kXboxTitleVirtualBytes = 0x2FFF0000U;
    const auto begin = std::uint64_t(output_pointer);
    const auto end = begin + kStatisticsBytes;
    const auto table = GuestMemory::kFunctionTableOffset;
    if ((output_pointer & 3U) || begin < 0x10000U ||
        end > GuestMemory::kAddressSpaceSize ||
        (begin < table + GuestMemory::kFunctionTableSize && end > table) ||
        !memory.committed_range(begin, kStatisticsBytes))
        return kNtInvalidParameter;
    if (read_be32(memory, output_pointer) != kStatisticsBytes)
        return 0xC0000023U; // STATUS_BUFFER_TOO_SMALL
    const auto image_pages = memory.has_loaded_pe() ?
        GuestMemory::kImageSize / GuestVirtualAllocator::kPageSize : 0ULL;
    const auto allocated_pages = allocator.allocated_bytes() /
        GuestVirtualAllocator::kPageSize;
    std::uint64_t physical_pages = 0;
    for (const auto& [offset, block] : physical_blocks)
        physical_pages += block.length / GuestVirtualAllocator::kPageSize;
    const auto used_pages = static_cast<std::uint32_t>(std::min<std::uint64_t>(
        kTotalPhysicalPages, image_pages + allocated_pages + physical_pages));
    const auto virtual_bytes = static_cast<std::uint32_t>(std::min<std::uint64_t>(
        kXboxTitleVirtualBytes, allocator.capacity_bytes()));
    const auto reserved_bytes = static_cast<std::uint32_t>(std::min<std::uint64_t>(
        virtual_bytes, allocator.reserved_bytes()));
    // 26 Xbox big-endian words. Report actual NT allocations and the
    // loaded image, excluding the host-only native PPC dispatch table.
    // Other kernel/physical allocations are not tracked by this allocator.
    std::uint32_t words[26]{};
    words[0] = kStatisticsBytes;
    words[1] = kTotalPhysicalPages;
    words[2] = 0;
    words[3] = kTotalPhysicalPages - used_pages;
    words[4] = virtual_bytes;
    words[5] = reserved_bytes;
    words[6] = used_pages;
    words[9] = static_cast<std::uint32_t>(image_pages);
    words[10] = static_cast<std::uint32_t>(allocated_pages);
    words[11] = static_cast<std::uint32_t>(allocated_pages);
    words[25] = kTotalPhysicalPages - 1;
    for (std::uint32_t i = 0; i < 26; ++i)
        write_be32(memory, output_pointer + i * 4, words[i]);
    return kNtSuccess;
}
std::uint32_t mm_bound_query_statistics(std::uint32_t output_pointer) {
    std::lock_guard lock(binding_mutex);
    if (!bound_memory || !bound_allocator) return kNtNotSupported;
    return mm_query_statistics_guest(*bound_memory, *bound_allocator, output_pointer);
}
namespace {
bool valid_xconfig_write(const GuestMemory& memory, std::uint32_t address,
                         std::size_t size) noexcept {
    const auto begin = std::uint64_t(address);
    const auto end = begin + size;
    const auto table = GuestMemory::kFunctionTableOffset;
    return begin >= 0x10000U && end <= GuestMemory::kAddressSpaceSize &&
           !(begin < table + GuestMemory::kFunctionTableSize && end > table) &&
           memory.committed_range(begin, size);
}
void write_xconfig_be16(GuestMemory& memory, std::uint32_t address,
                        std::uint16_t value) noexcept {
    auto* p = memory.address(address);
    p[0] = std::uint8_t(value >> 8);
    p[1] = std::uint8_t(value);
}
}
std::uint32_t ex_get_xconfig_setting_guest(GuestMemory& memory,
    std::uint16_t category, std::uint16_t setting, std::uint32_t buffer,
    std::uint16_t buffer_size, std::uint32_t required_size) {
    constexpr std::uint32_t invalid_category = 0xC00000EFU;
    constexpr std::uint32_t invalid_setting = 0xC00000F0U;
    constexpr std::uint32_t invalid_buffer = 0xC00000F1U;
    constexpr std::uint32_t buffer_too_small = 0xC0000023U;
    if (required_size && !valid_xconfig_write(memory, required_size, 2))
        return invalid_buffer;
    std::uint32_t value = 0;
    std::uint16_t bytes = 0;
    if (category == 2) {
        if (setting != 2) return invalid_setting;
        value = 0x00001000U; // Retail North America XCONFIG_SECURED_AV_REGION.
        bytes = 4;
    } else if (category == 3) {
        switch (setting) {
        case 9: value = 1; bytes = 4; break; // English XCONFIG_USER_LANGUAGE.
        case 10: value = 0x00040000U; bytes = 4; break; // HD widescreen video flags.
        case 12: value = 0; bytes = 4; break; // Retail flags.
        case 14: value = 103; bytes = 1; break; // US XCONFIG_USER_COUNTRY.
        default: return invalid_setting;
        }
    } else {
        return invalid_category;
    }
    if (required_size) write_xconfig_be16(memory, required_size, bytes);
    if (!buffer) return buffer_size ? invalid_buffer : kNtSuccess;
    if (buffer_size < bytes) return buffer_too_small;
    if (!valid_xconfig_write(memory, buffer, bytes)) return invalid_buffer;
    if (bytes == 1) memory.address(buffer)[0] = std::uint8_t(value);
    else write_be32(memory, buffer, value);
    return kNtSuccess;
}
std::uint32_t ex_bound_get_xconfig_setting(std::uint16_t category,
    std::uint16_t setting, std::uint32_t buffer, std::uint16_t buffer_size,
    std::uint32_t required_size) {
    std::lock_guard lock(binding_mutex);
    if (!bound_memory) return kNtNotSupported;
    return ex_get_xconfig_setting_guest(*bound_memory, category, setting,
        buffer, buffer_size, required_size);
}
void vd_bound_query_video_mode(std::uint32_t output_pointer) {
    std::lock_guard lock(binding_mutex);
    constexpr std::size_t kXboxVideoModeBytes = 48;
    if (!bound_memory || !valid_xconfig_write(*bound_memory,
                                               output_pointer, kXboxVideoModeBytes))
        throw std::runtime_error("Invalid Xbox X_VIDEO_MODE guest pointer");
    // Xbox 360 retail 720p HDTV output. The native renderer will present
    // this guest surface to the host monitor separately.
    constexpr std::uint32_t words[12] = {
        kXboxVideoWidth, kXboxVideoHeight, 0, kXboxVideoWidescreen, 1,
        0x42700000U, 1, 0x4A, 1, 0, 0, 0
    };
    for (std::size_t i = 0; i < 12; ++i)
        write_be32(*bound_memory, output_pointer + std::uint32_t(i * 4), words[i]);
}
void vd_bound_get_current_display_gamma(std::uint32_t type_pointer,
                                        std::uint32_t power_pointer) {
    std::lock_guard lock(binding_mutex);
    if (!bound_memory || (type_pointer & 3U) || (power_pointer & 3U) ||
        type_pointer == power_pointer ||
        !valid_xconfig_write(*bound_memory, type_pointer, 4U) ||
        !valid_xconfig_write(*bound_memory, power_pointer, 4U))
        throw std::runtime_error("Invalid Xbox display gamma output pointers");
    // The configured HDTV video output uses BT.709 gamma (type 2).
    // Power 2.22222233 is retained for callers switching to type 3.
    write_be32(*bound_memory, type_pointer, 2U);
    write_be32(*bound_memory, power_pointer, 0x400E38E4U);
}
std::uint32_t vd_bound_query_video_flags() {
    // Xbox 360 VdQueryVideoFlags: bit 0 = widescreen, bit 1 = >=1024 px,
    // bit 2 = >=1920 px. Derive from the same video mode reported to PPC.
    return (kXboxVideoWidescreen ? 1U : 0U) |
           (kXboxVideoWidth >= 1024U ? 2U : 0U) |
           (kXboxVideoWidth >= 1920U ? 4U : 0U);
}
std::uint32_t nt_free_virtual_memory_guest(
    GuestMemory& memory, GuestVirtualAllocator& allocator,
    std::uint32_t base_pointer, std::uint32_t size_pointer,
    std::uint32_t free_type, std::uint32_t debug_memory) {
    if (!valid_words(memory, base_pointer, size_pointer) || debug_memory)
        return kNtInvalidParameter;
    if (free_type != kMemRelease && free_type != kMemDecommit)
        return kNtInvalidParameter;
    const auto base = read_be32(memory, base_pointer);
    const auto size = read_be32(memory, size_pointer);
    std::uint64_t affected_size = 0;
    if (free_type == kMemRelease) {
        const auto existing = allocator.allocation_size(base);
        if (!existing) return kNtMemoryNotAllocated;
        if (size != 0) return kNtInvalidParameter;
        affected_size = *existing;
    } else {
        if (!size) return kNtNotSupported;
        if ((base & (GuestVirtualAllocator::kPageSize - 1)) ||
            (size & (GuestVirtualAllocator::kPageSize - 1)))
            return kNtInvalidParameter;
        if (!allocator.reserved_range(base, size)) return kNtMemoryNotAllocated;
        affected_size = size;
    }
    // RegionSize is an output parameter; never revoke its backing page.
    const std::uint64_t release_begin = base;
    const std::uint64_t release_end = release_begin + affected_size;
    const std::uint64_t output_begin = size_pointer;
    if (output_begin < release_end && output_begin + 4 > release_begin)
        return kNtInvalidParameter;
    try {
        if (free_type == kMemRelease) {
            if (!allocator.release(base)) return kNtMemoryNotAllocated;
        } else {
            if (!allocator.decommit_reserved(base, affected_size))
                return kNtNotSupported;
        }
    } catch (const std::exception&) {
        return kNtNoMemory;
    }
    write_be32(memory, size_pointer, static_cast<std::uint32_t>(affected_size));
    return kNtSuccess;
}
void ke_query_system_time_guest(GuestMemory& memory, std::uint32_t output,
                                std::uint64_t filetime) {
    const auto begin = std::uint64_t(output);
    const auto end = begin + 8U;
    const auto table = GuestMemory::kFunctionTableOffset;
    if ((output & 7U) || begin < 0x10000U || end > GuestMemory::kAddressSpaceSize ||
        (begin < table + GuestMemory::kFunctionTableSize && end > table) ||
        !memory.committed_range(begin, 8))
        throw std::runtime_error("KeQuerySystemTime: invalid guest LARGE_INTEGER");
    auto* p = memory.address(output);
    for (int i = 0; i < 8; ++i)
        p[i] = static_cast<std::uint8_t>(filetime >> (56 - i * 8));
}
void ke_bound_query_system_time(std::uint32_t output) {
    std::lock_guard lock(binding_mutex);
    if (!bound_memory || !bound_allocator)
        throw std::runtime_error("KeQuerySystemTime: unbound guest memory");
    constexpr std::uint64_t kFiletimeUnixEpoch = 116444736000000000ULL;
    const auto unix_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (unix_ns < 0)
        throw std::runtime_error("KeQuerySystemTime: invalid host clock");
    ke_query_system_time_guest(*bound_memory, output,
        kFiletimeUnixEpoch + static_cast<std::uint64_t>(unix_ns / 100));
}

namespace {
constexpr std::uint64_t kNtTicksPerSecond = 10000000ULL;
constexpr std::uint64_t kNtTicksPerDay = 86400ULL * kNtTicksPerSecond;
constexpr std::int64_t kNtDaysBeforeUnixEpoch = 134774;

// Civil-date arithmetic uses the proleptic Gregorian calendar. It is
// independent of the host timezone, locale, and time_t width.
struct NtCivilDate { int year; unsigned month; unsigned day; };
std::int64_t nt_days_from_civil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned year_of_era = static_cast<unsigned>(year - era * 400);
    const unsigned day_of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned day_of_era = year_of_era * 365 + year_of_era / 4 -
        year_of_era / 100 + day_of_year;
    return static_cast<std::int64_t>(era) * 146097 + day_of_era - 719468;
}
NtCivilDate nt_civil_from_days(std::int64_t days) {
    days += 719468;
    const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const unsigned day_of_era = static_cast<unsigned>(days - era * 146097);
    const unsigned year_of_era = (day_of_era - day_of_era / 1460 +
        day_of_era / 36524 - day_of_era / 146096) / 365;
    int year = static_cast<int>(year_of_era + era * 400);
    const unsigned day_of_year = day_of_era -
        (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    const unsigned month_prime = (5 * day_of_year + 2) / 153;
    const unsigned day = day_of_year - (153 * month_prime + 2) / 5 + 1;
    const unsigned month = month_prime < 10 ? month_prime + 3 : month_prime - 9;
    year += month <= 2;
    return {year, month, day};
}
bool nt_leap_year(int year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}
unsigned nt_days_in_month(int year, unsigned month) {
    static constexpr unsigned days[12] =
        {31,28,31,30,31,30,31,31,30,31,30,31};
    return days[month - 1] + (month == 2 && nt_leap_year(year) ? 1U : 0U);
}
bool nt_time_buffer(const GuestMemory& memory, std::uint32_t address,
                    std::uint32_t bytes, std::uint32_t alignment) {
    const auto begin = static_cast<std::uint64_t>(address);
    const auto end = begin + bytes;
    const auto table = GuestMemory::kFunctionTableOffset;
    return begin >= 0x10000U && (address & (alignment - 1)) == 0 &&
        end <= GuestMemory::kAddressSpaceSize &&
        !(begin < table + GuestMemory::kFunctionTableSize && end > table) &&
        memory.committed_range(address, bytes);
}
std::uint16_t nt_read_be16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((std::uint16_t(p[0]) << 8) | p[1]);
}
void nt_write_be16(std::uint8_t* p, std::uint16_t value) {
    p[0] = static_cast<std::uint8_t>(value >> 8);
    p[1] = static_cast<std::uint8_t>(value);
}
}
void rtl_time_to_time_fields_guest(GuestMemory& memory,
    std::uint32_t time_pointer, std::uint32_t fields_pointer) {
    if (!nt_time_buffer(memory, time_pointer, 8, 8) ||
        !nt_time_buffer(memory, fields_pointer, 16, 2))
        throw std::runtime_error("RtlTimeToTimeFields: invalid guest pointer");
    const auto* input = memory.address(time_pointer);
    std::uint64_t ticks = 0;
    for (int i = 0; i < 8; ++i) ticks = (ticks << 8) | input[i];
    if (ticks > 0x7FFFFFFFFFFFFFFFULL)
        throw std::runtime_error("RtlTimeToTimeFields: negative NT time");
    const std::uint64_t nt_days = ticks / kNtTicksPerDay;
    const auto civil = nt_civil_from_days(
        static_cast<std::int64_t>(nt_days) - kNtDaysBeforeUnixEpoch);
    if (civil.year < 1601 || civil.year > 32767)
        throw std::runtime_error("RtlTimeToTimeFields: year exceeds TIME_FIELDS");
    std::uint64_t remainder = ticks % kNtTicksPerDay;
    const auto hour = static_cast<std::uint16_t>(remainder / (3600 * kNtTicksPerSecond));
    remainder %= 3600 * kNtTicksPerSecond;
    const auto minute = static_cast<std::uint16_t>(remainder / (60 * kNtTicksPerSecond));
    remainder %= 60 * kNtTicksPerSecond;
    const auto second = static_cast<std::uint16_t>(remainder / kNtTicksPerSecond);
    remainder %= kNtTicksPerSecond;
    const auto millisecond = static_cast<std::uint16_t>(remainder / 10000);
    // 1601-01-01 was Monday; Windows uses Sunday=0.
    const std::uint16_t values[8] = {
        static_cast<std::uint16_t>(civil.year),
        static_cast<std::uint16_t>(civil.month),
        static_cast<std::uint16_t>(civil.day), hour, minute, second,
        millisecond, static_cast<std::uint16_t>((nt_days + 1) % 7)};
    auto* output = memory.address(fields_pointer);
    for (int i = 0; i < 8; ++i) nt_write_be16(output + 2 * i, values[i]);
}
bool rtl_time_fields_to_time_guest(GuestMemory& memory,
    std::uint32_t fields_pointer, std::uint32_t time_pointer) {
    if (!nt_time_buffer(memory, fields_pointer, 16, 2) ||
        !nt_time_buffer(memory, time_pointer, 8, 8))
        throw std::runtime_error("RtlTimeFieldsToTime: invalid guest pointer");
    const auto* input = memory.address(fields_pointer);
    std::uint16_t fields[8]{};
    for (int i = 0; i < 8; ++i) fields[i] = nt_read_be16(input + 2 * i);
    const int year = fields[0];
    const unsigned month = fields[1], day = fields[2];
    const unsigned hour = fields[3], minute = fields[4], second = fields[5];
    const unsigned millisecond = fields[6];
    if (year < 1601 || year > 32767 || month < 1 || month > 12 ||
        day < 1 || day > nt_days_in_month(year, month) ||
        hour > 23 || minute > 59 || second > 59 || millisecond > 999)
        return false;
    // Weekday (field 7) is output-only and is ignored by the NT conversion.
    const auto nt_days = nt_days_from_civil(year, month, day) +
        kNtDaysBeforeUnixEpoch;
    if (nt_days < 0) return false;
    const std::uint64_t ticks =
        static_cast<std::uint64_t>(nt_days) * kNtTicksPerDay +
        (static_cast<std::uint64_t>(hour) * 3600 + minute * 60 + second) *
            kNtTicksPerSecond + static_cast<std::uint64_t>(millisecond) * 10000;
    if (ticks > 0x7FFFFFFFFFFFFFFFULL) return false;
    auto* output = memory.address(time_pointer);
    for (int i = 0; i < 8; ++i)
        output[i] = static_cast<std::uint8_t>(ticks >> (56 - i * 8));
    return true;
}
void rtl_bound_time_to_time_fields(std::uint32_t time_pointer,
    std::uint32_t fields_pointer) {
    std::lock_guard lock(binding_mutex);
    if (!bound_memory)
        throw std::runtime_error("RtlTimeToTimeFields: guest memory not bound");
    rtl_time_to_time_fields_guest(*bound_memory, time_pointer, fields_pointer);
}
std::uint32_t rtl_bound_time_fields_to_time(std::uint32_t fields_pointer,
    std::uint32_t time_pointer) {
    std::lock_guard lock(binding_mutex);
    if (!bound_memory)
        throw std::runtime_error("RtlTimeFieldsToTime: guest memory not bound");
    return rtl_time_fields_to_time_guest(*bound_memory, fields_pointer,
        time_pointer) ? 1U : 0U;
}

void bind_kernel_guest_memory(GuestMemory* memory:
                              GuestVirtualAllocator* allocator) noexcept {
    std::lock_guard lock(binding_mutex);
    if (bound_memory && bound_memory != memory) {
        for (const auto& [offset, block] : physical_blocks) {
            try { bound_memory->decommit(block.guest_address, block.length); }
            catch (const std::exception&) { /* noexcept teardown */ }
        }
        physical_blocks.clear();
    }
    pool_allocations.clear();
    bound_memory = memory && allocator ? memory : nullptr;
    bound_allocator = memory && allocator ? allocator : nullptr;
}

std::uint32_t mm_bound_allocate_physical_memory_ex(
    std::uint32_t flags, std::uint32_t bytes, std::uint32_t protect,
    std::uint32_t min_address, std::uint32_t max_address,
    std::uint32_t alignment) {
    std::lock_guard lock(binding_mutex);
    constexpr std::uint32_t kReadOnly = 2U, kReadWrite = 4U;
    constexpr std::uint32_t kNoCache = 0x200U, kWriteCombine = 0x400U;
    constexpr std::uint32_t k16mPages = 0x10000000U, k64kPages = 0x20000000U;
    constexpr std::uint32_t kAllowed = kReadOnly | kReadWrite | kNoCache |
        kWriteCombine | k16mPages | k64kPages;
    const auto access = protect & (kReadOnly | kReadWrite);
    if (!bound_memory || flags > 2U || !bytes || (protect & ~kAllowed) ||
        (access != kReadOnly && access != kReadWrite) ||
        (protect & (k16mPages | k64kPages)) == (k16mPages | k64kPages))
        return 0;
    const std::uint64_t page = (protect & k64kPages) ? 0x10000ULL :
        (protect & k16mPages) ? 0x1000000ULL : 0x1000ULL;
    const std::uint32_t view = (protect & k64kPages) ? kPhysical64kView :
        (protect & k16mPages) ? kPhysical16mView : kPhysical4kView;
    if (alignment && (alignment & (alignment - 1))) return 0;
    const std::uint64_t effective_alignment =
        std::max<std::uint64_t>(alignment, page);
    if ((effective_alignment & (effective_alignment - 1)) != 0 ||
        effective_alignment > kPhysicalBytes)
        return 0;
    const std::uint64_t length = (std::uint64_t(bytes) + page - 1) & ~(page - 1);
    const std::uint64_t low = min_address;
    const std::uint64_t high = std::min<std::uint64_t>(
        max_address, kPhysicalBytes - 1);
    if (low > high || length > high - low + 1) return 0;
    // Xbox physical allocations prefer high addresses. Never double-book
    // physical offsets even when allocations use different page-size views.
    std::uint64_t candidate = (high + 1 - length) & ~(effective_alignment - 1);
    for (;;) {
        if (candidate < low) break;
        const std::uint64_t guest = std::uint64_t(view) + candidate;
        if (!physical_overlaps(candidate, length) &&
            !bound_memory->any_committed_range(guest,
                                                static_cast<std::size_t>(length))) {
            try {
                bound_memory->commit(guest, static_cast<std::size_t>(length));
                std::memset(bound_memory->address(static_cast<std::uint32_t>(guest)),
                            0, static_cast<std::size_t>(length));
                physical_blocks.emplace(static_cast<std::uint32_t>(candidate),
                    PhysicalBlock{static_cast<std::uint32_t>(guest),
                                  static_cast<std::uint32_t>(length), protect});
                return static_cast<std::uint32_t>(guest);
            } catch (const std::exception&) {
                return 0;
            }
        }
        if (candidate < effective_alignment) break;
        candidate -= effective_alignment;
    }
    return 0;
}
void mm_bound_free_physical_memory(std::uint32_t type,
                                   std::uint32_t guest_address) {
    std::lock_guard lock(binding_mutex);
    if (!bound_memory || type > 2U) return;
    for (auto it = physical_blocks.begin(); it != physical_blocks.end(); ++it) {
        if (it->second.guest_address != guest_address) continue;
        bound_memory->decommit(guest_address, it->second.length);
        physical_blocks.erase(it);
        return;
    }
}
std::uint32_t mm_bound_query_physical_allocation_size(
    std::uint32_t guest_address) {
    std::lock_guard lock(binding_mutex);
    if (!bound_memory) return 0;
    for (const auto& [offset, block] : physical_blocks)
        if (block.guest_address == guest_address) return block.length;
    return 0;
}
std::uint32_t mm_bound_get_physical_address(std::uint32_t guest_address) {
    std::lock_guard lock(binding_mutex);
    if (!bound_memory) return 0;
    for (const auto& [offset, block] : physical_blocks) {
        if (guest_address >= block.guest_address &&
            std::uint64_t(guest_address) - block.guest_address < block.length)
            return offset + (guest_address - block.guest_address);
    }
    return 0;
}

std::optional<std::uint32_t> mm_bound_resolve_physical_range(
    std::uint32_t physical_address, std::uint32_t length) {
    std::lock_guard lock(binding_mutex);
    if (!bound_memory || !length ||
        std::uint64_t(physical_address) + length > kPhysicalBytes)
        return std::nullopt;
    auto it = physical_blocks.upper_bound(physical_address);
    if (it == physical_blocks.begin()) return std::nullopt;
    --it;
    const auto delta = std::uint64_t(physical_address) - it->first;
    if (delta + length > it->second.length) return std::nullopt;
    const auto guest = it->second.guest_address + static_cast<std::uint32_t>(delta);
    if (!bound_memory->committed_range(guest, length)) return std::nullopt;
    return guest;
}

void ke_bound_initialize_dpc(std::uint32_t dpc, std::uint32_t routine,
                             std::uint32_t context) {
    std::lock_guard lock(binding_mutex);
    constexpr std::uint32_t kDpcSize = 0x20U;
    const std::uint64_t begin = dpc;
    const std::uint64_t end = begin + kDpcSize;
    const std::uint64_t table = GuestMemory::kFunctionTableOffset;
    if (!bound_memory || (dpc & 3U) || begin < 0x10000U ||
        end > GuestMemory::kAddressSpaceSize ||
        (begin < table + GuestMemory::kFunctionTableSize && end > table) ||
        !bound_memory->committed_range(dpc, kDpcSize) ||
        !routine || (routine & 3U) || routine < GuestMemory::kCodeBase ||
        std::uint64_t(routine) >= std::uint64_t(GuestMemory::kCodeBase) +
            GuestMemory::kCodeSize)
        throw std::runtime_error("KeInitializeDpc: invalid guest DPC or PPC routine");
    auto* data = bound_memory->address(dpc);
    std::memset(data, 0, kDpcSize);
    data[0] = 0x13U; // DpcObject
    data[1] = 1U;    // MediumImportance
    write_be32(*bound_memory, dpc + 0x0CU, routine);
    write_be32(*bound_memory, dpc + 0x10U, context);
}

std::uint32_t ex_bound_allocate_pool(std::uint32_t pool_type,
                                     std::uint32_t bytes, std::uint32_t tag) {
    std::lock_guard lock(binding_mutex);
    // Windows/Xbox POOL_TYPE: nonpaged/paged and cache-aligned variants.
    // Types 2/6 require a fatal allocation on exhaustion; reject these until
    // the kernel bugcheck path is available rather than silently returning 0.
    if (!bound_allocator || pool_type == 2U || pool_type == 3U ||
        pool_type == 6U || pool_type > 6U)
        return 0;
    try {
        const auto address = bound_allocator->allocate(bytes ? bytes : 1U);
        if (!address) return 0;
        try {
            const auto [it, inserted] = pool_allocations.emplace(
                *address, PoolAllocation{bytes, tag});
            if (!inserted) throw std::runtime_error("Duplicate Xbox pool address");
        } catch (...) {
            bound_allocator->release(*address);
            throw;
        }
        return *address;
    } catch (const std::bad_alloc&) {
        return 0;
    }
}
void ex_bound_free_pool(std::uint32_t address) {
    std::lock_guard lock(binding_mutex);
    if (!bound_allocator)
        throw std::runtime_error("ExFreePool: guest allocator not bound");
    const auto it = pool_allocations.find(address);
    if (it == pool_allocations.end())
        throw std::runtime_error("ExFreePool: invalid or already freed guest pointer");
    if (!bound_allocator->release(address))
        throw std::runtime_error("ExFreePool: guest release failed");
    pool_allocations.erase(it);
}

std::uint32_t nt_bound_allocate_virtual_memory(
    std::uint32_t base_pointer, std::uint32_t size_pointer,
    std::uint32_t allocation_type, std::uint32_t protect,
    std::uint32_t debug_memory) {
    std::lock_guard lock(binding_mutex);
    if (!bound_memory || !bound_allocator) return kNtNotSupported;
    return nt_allocate_virtual_memory_guest(*bound_memory, *bound_allocator,
        base_pointer, size_pointer, allocation_type, protect, debug_memory);
}
std::uint32_t nt_bound_query_virtual_memory(
    std::uint32_t address, std::uint32_t output_pointer) {
    std::lock_guard lock(binding_mutex);
    if (!bound_memory || !bound_allocator) return kNtNotSupported;
    return nt_query_virtual_memory_guest(*bound_memory, *bound_allocator,
                                         address, output_pointer);
}
std::uint32_t nt_bound_free_virtual_memory(
    std::uint32_t base_pointer, std::uint32_t size_pointer,
    std::uint32_t free_type, std::uint32_t debug_memory) {
    std::lock_guard lock(binding_mutex);
    if (!bound_memory || !bound_allocator) return kNtNotSupported;
    return nt_free_virtual_memory_guest(*bound_memory, *bound_allocator,
        base_pointer, size_pointer, free_type, debug_memory);
}

std::uint32_t ke_bound_dbg_print(std::uint32_t format,
                                 const std::array<std::uint32_t, 7>& args) {
    std::lock_guard lock(binding_mutex);
    if (!bound_memory)
        throw std::runtime_error("DbgPrint requires bound Xbox guest memory");
    const auto guest_string = [&](std::uint32_t pointer) {
        std::string result;
        for (std::uint32_t i = 0; i < 2048; ++i) {
            const auto address = std::uint64_t(pointer) + i;
            if (address < 0x10000U || address >= GuestMemory::kAddressSpaceSize ||
                !bound_memory->committed_range(address, 1))
                throw std::runtime_error("DbgPrint string points outside committed guest memory");
            const char ch = static_cast<char>(
                bound_memory->address(static_cast<std::uint32_t>(address))[0]);
            if (!ch) return result;
            result.push_back(ch);
        }
        throw std::runtime_error("DbgPrint guest string exceeds 2048 bytes");
    };
    const auto fmt = guest_string(format);
    std::string output;
    std::size_t argument = 0;
    for (std::size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] != '%') {
            output.push_back(fmt[i]);
            continue;
        }
        if (++i >= fmt.size())
            throw std::runtime_error("DbgPrint unterminated format");
        if (fmt[i] == '%') {
            output.push_back('%');
            continue;
        }
        std::string spec = "%";
        while (i < fmt.size() &&
               (fmt[i] == '-' || fmt[i] == '+' || fmt[i] == ' ' ||
                fmt[i] == '#' || fmt[i] == '0')) {
            spec.push_back(fmt[i++]);
        }
        std::uint32_t width = 0;
        while (i < fmt.size() && fmt[i] >= '0' && fmt[i] <= '9') {
            width = width * 10 + static_cast<unsigned>(fmt[i++] - '0');
            if (width > 64)
                throw std::runtime_error("DbgPrint field width exceeds 64");
            spec.push_back(fmt[i - 1]);
        }
        if (i < fmt.size() && fmt[i] == '.') {
            spec.push_back(fmt[i++]);
            std::uint32_t precision = 0;
            while (i < fmt.size() && fmt[i] >= '0' && fmt[i] <= '9') {
                precision = precision * 10 + static_cast<unsigned>(fmt[i++] - '0');
                if (precision > 64)
                    throw std::runtime_error("DbgPrint precision exceeds 64");
                spec.push_back(fmt[i - 1]);
            }
        }
        if (i >= fmt.size())
            throw std::runtime_error("DbgPrint unterminated conversion");
        // The retail DOTM call sites use 32-bit PPC varargs. Never reinterpret
        // a 64-bit guest argument as two independent 32-bit arguments.
        if (fmt[i] == 'l' || fmt[i] == 'h' || fmt[i] == 'L' || fmt[i] == 'I')
            throw std::runtime_error("DbgPrint 64-bit/wide conversion needs PPC ABI support");
        const char conversion = fmt[i];
        if (conversion != 's' && conversion != 'd' && conversion != 'i' &&
            conversion != 'u' && conversion != 'x' && conversion != 'X' &&
            conversion != 'o' && conversion != 'c' && conversion != 'p')
            throw std::runtime_error("Unsupported Xbox DbgPrint format conversion");
        if (argument == args.size())
            throw std::runtime_error("DbgPrint requires PPC stack varargs beyond r10");
        const auto value = args[argument++];
        if (conversion == 's') {
            const auto string = value ? guest_string(value) : std::string("(null)");
            if (string.size() < width && spec.find('-') == std::string::npos)
                output.append(width - string.size(), ' ');
            output += string;
            if (string.size() < width && spec.find('-') != std::string::npos)
                output.append(width - string.size(), ' ');
        } else {
            char buffer[128]{};
            if (conversion == 'p') {
                std::snprintf(buffer, sizeof(buffer), "0x%08X", value);
            } else {
                spec.push_back(conversion);
                if (conversion == 'd' || conversion == 'i')
                    std::snprintf(buffer, sizeof(buffer), spec.c_str(),
                                  static_cast<int>(value));
                else
                    std::snprintf(buffer, sizeof(buffer), spec.c_str(), value);
            }
            output += buffer;
        }
        if (output.size() > 8192)
            throw std::runtime_error("DbgPrint output exceeds 8192 bytes");
    }
    if (!output.empty())
        std::fwrite(output.data(), 1, output.size(), stderr);
    std::fflush(stderr);
    return static_cast<std::uint32_t>(output.size());
}
}
