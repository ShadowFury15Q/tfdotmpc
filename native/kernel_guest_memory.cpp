#include "kernel_guest_memory.hpp"
#include <cstdint>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>
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
        1280, 720, 0, 1, 1, 0x42700000U, 1, 0x4A, 1, 0, 0, 0
    };
    for (std::size_t i = 0; i < 12; ++i)
        write_be32(*bound_memory, output_pointer + std::uint32_t(i * 4), words[i]);
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
void bind_kernel_guest_memory(GuestMemory* memory,
                              GuestVirtualAllocator* allocator) noexcept {
    std::lock_guard lock(binding_mutex);
    if (bound_memory && bound_memory != memory) {
        for (const auto& [offset, block] : physical_blocks) {
            try { bound_memory->decommit(block.guest_address, block.length); }
            catch (const std::exception&) { /* noexcept teardown */ }
        }
        physical_blocks.clear();
    }
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
}
