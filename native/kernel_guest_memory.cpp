#include "kernel_guest_memory.hpp"
#include <cstdint>
#include <chrono>
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
    bound_memory = memory && allocator ? memory : nullptr;
    bound_allocator = memory && allocator ? allocator : nullptr;
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
