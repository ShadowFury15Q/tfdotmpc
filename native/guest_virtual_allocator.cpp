#include "guest_virtual_allocator.hpp"
#include <cstring>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace dotm {
namespace {
bool page_aligned(std::uint64_t address) {
    return (address & (GuestVirtualAllocator::kPageSize - 1)) == 0;
}
bool power_of_two(std::uint64_t value) {
    return value && !(value & (value - 1));
}
std::uint64_t align_up(std::uint64_t address, std::uint64_t alignment) {
    return (address + alignment - 1) & ~(alignment - 1);
}
}
GuestVirtualAllocator::GuestVirtualAllocator(
    GuestMemory& memory, std::uint32_t first, std::uint32_t limit)
    : memory_(memory), first_(first), limit_(limit) {
    // Never allocate into the high guest PE image or native dispatch table.
    if (first < 0x10000U || first >= limit || limit > GuestMemory::kImageBase ||
        !page_aligned(first) || !page_aligned(limit))
        throw std::invalid_argument("Invalid guest allocation window");
}
std::uint64_t GuestVirtualAllocator::rounded_size(std::uint64_t bytes) {
    if (!bytes || bytes > std::numeric_limits<std::uint64_t>::max() - (kPageSize - 1))
        throw std::invalid_argument("Invalid guest allocation size");
    return align_up(bytes, kPageSize);
}
bool GuestVirtualAllocator::free_range(
    std::uint64_t address, std::uint64_t bytes) const {
    if (address < first_ || address >= limit_ || bytes > std::uint64_t(limit_) - address)
        return false;
    const auto next = blocks_.lower_bound(static_cast<std::uint32_t>(address));
    if (next != blocks_.end() && std::uint64_t(next->first) < address + bytes)
        return false;
    if (next != blocks_.begin()) {
        const auto previous = std::prev(next);
        if (std::uint64_t(previous->first) + previous->second > address)
            return false;
    }
    return true;
}
void GuestVirtualAllocator::commit_and_track(
    std::uint32_t address, std::uint64_t bytes) {
    // A failed host commit must not consume a guest address range.
    memory_.commit(address, static_cast<std::size_t>(bytes));
    std::memset(memory_.address(address), 0, static_cast<std::size_t>(bytes));
    blocks_.emplace(address, bytes);
    allocated_bytes_ += bytes;
}
std::optional<std::uint32_t> GuestVirtualAllocator::allocate(
    std::uint64_t bytes, std::uint64_t alignment) {
    const auto length = rounded_size(bytes);
    if (alignment < kPageSize || !power_of_two(alignment))
        throw std::invalid_argument("Guest alignment must be a page-sized power of two");
    if (length > std::uint64_t(limit_) - first_) return std::nullopt;
    std::uint64_t cursor = first_;
    for (const auto& [base, occupied] : blocks_) {
        const auto candidate = align_up(cursor, alignment);
        if (candidate <= base && length <= std::uint64_t(base) - candidate) {
            commit_and_track(static_cast<std::uint32_t>(candidate), length);
            return static_cast<std::uint32_t>(candidate);
        }
        cursor = std::uint64_t(base) + occupied;
    }
    const auto candidate = align_up(cursor, alignment);
    if (candidate >= limit_ || length > std::uint64_t(limit_) - candidate)
        return std::nullopt;
    commit_and_track(static_cast<std::uint32_t>(candidate), length);
    return static_cast<std::uint32_t>(candidate);
}
bool GuestVirtualAllocator::allocate_at(
    std::uint32_t address, std::uint64_t bytes) {
    const auto length = rounded_size(bytes);
    if (!page_aligned(address) || !free_range(address, length)) return false;
    commit_and_track(address, length);
    return true;
}
bool GuestVirtualAllocator::release(std::uint32_t address) {
    const auto it = blocks_.find(address);
    if (it == blocks_.end()) return false;
    // Do not make the address reusable until host decommit succeeds.
    memory_.decommit(address, static_cast<std::size_t>(it->second));
    allocated_bytes_ -= it->second;
    blocks_.erase(it);
    return true;
}
}
