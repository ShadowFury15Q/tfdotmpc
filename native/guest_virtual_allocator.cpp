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
    return !memory_.any_committed_range(address, static_cast<std::size_t>(bytes));
}
std::optional<std::uint32_t> GuestVirtualAllocator::find_available_in_gap(
    std::uint64_t begin, std::uint64_t end,
    std::uint64_t bytes, std::uint64_t alignment) const {
    if (begin >= end || bytes > end - begin) return std::nullopt;
    // Walk guest pages once, rather than rescanning an entire candidate
    // allocation for every aligned start. A committed page invalidates all
    // candidate windows that would include it.
    std::uint64_t candidate = align_up(begin, alignment);
    std::uint64_t scan = candidate;
    while (candidate < end && bytes <= end - candidate) {
        if (memory_.any_committed_range(scan, static_cast<std::size_t>(kPageSize))) {
            candidate = align_up(scan + kPageSize, alignment);
            scan = candidate;
        } else {
            scan += kPageSize;
            if (scan - candidate >= bytes)
                return static_cast<std::uint32_t>(candidate);
        }
    }
    return std::nullopt;
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
    if (length > std::uint64_t(limit_) - first_ || alignment > limit_)
        return std::nullopt;
    std::uint64_t cursor = first_;
    for (const auto& [base, occupied] : blocks_) {
        if (const auto candidate = find_available_in_gap(
                cursor, base, length, alignment)) {
            commit_and_track(*candidate, length);
            return candidate;
        }
        cursor = std::uint64_t(base) + occupied;
    }
    if (const auto candidate = find_available_in_gap(
            cursor, limit_, length, alignment)) {
        commit_and_track(*candidate, length);
        return candidate;
    }
    return std::nullopt;
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
