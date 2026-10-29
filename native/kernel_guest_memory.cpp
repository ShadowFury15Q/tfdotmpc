#include "kernel_guest_memory.hpp"
#include <cstdint>
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
    // Reservation-only, commit-only, reset, large pages, protection changes
    // and devkit memory are not implemented; never pretend they succeeded.
    if (allocation_type != (kMemCommit | kMemReserve) ||
        protect != kPageReadWrite)
        return kNtNotSupported;
    const auto requested_base = read_be32(memory, base_pointer);
    const auto requested_size = read_be32(memory, size_pointer);
    if (!requested_size) return kNtInvalidParameter;
    constexpr std::uint64_t page = GuestVirtualAllocator::kPageSize;
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
        if (requested_base) {
            if (allocator.allocate_at(static_cast<std::uint32_t>(aligned_base),
                                      rounded))
                allocated = static_cast<std::uint32_t>(aligned_base);
        } else {
            allocated = allocator.allocate(rounded);
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
std::uint32_t nt_free_virtual_memory_guest(
    GuestMemory& memory, GuestVirtualAllocator& allocator,
    std::uint32_t base_pointer, std::uint32_t size_pointer,
    std::uint32_t free_type, std::uint32_t debug_memory) {
    if (!valid_words(memory, base_pointer, size_pointer) || debug_memory)
        return kNtInvalidParameter;
    if (free_type == kMemDecommit) return kNtNotSupported;
    if (free_type != kMemRelease) return kNtInvalidParameter;
    const auto base = read_be32(memory, base_pointer);
    const auto size = read_be32(memory, size_pointer);
    // MEM_RELEASE requires a zero RegionSize and exact allocation base.
    if (size != 0) return kNtInvalidParameter;
    const auto existing = allocator.allocation_size(base);
    if (!existing) return kNtMemoryNotAllocated;
    try {
        if (!allocator.release(base)) return kNtMemoryNotAllocated;
    } catch (const std::exception&) {
        return kNtNoMemory;
    }
    write_be32(memory, size_pointer, static_cast<std::uint32_t>(*existing));
    return kNtSuccess;
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
std::uint32_t nt_bound_free_virtual_memory(
    std::uint32_t base_pointer, std::uint32_t size_pointer,
    std::uint32_t free_type, std::uint32_t debug_memory) {
    std::lock_guard lock(binding_mutex);
    if (!bound_memory || !bound_allocator) return kNtNotSupported;
    return nt_free_virtual_memory_guest(*bound_memory, *bound_allocator,
        base_pointer, size_pointer, free_type, debug_memory);
}
}
