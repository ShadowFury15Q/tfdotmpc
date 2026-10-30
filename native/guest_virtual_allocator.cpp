#include "guest_virtual_allocator.hpp"
#include <algorithm>
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
        if (std::uint64_t(previous->first) + previous->second.size > address)
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
    // Reserve metadata first: allocation failure must not leave orphaned
    // host-committed pages outside the guest allocator's accounting.
    auto [it, inserted] = blocks_.emplace(
        address, Block{bytes, std::vector<std::uint8_t>(bytes / kPageSize, 1)});
    if (!inserted) throw std::logic_error("Duplicate guest allocation");
    try {
        memory_.commit(address, static_cast<std::size_t>(bytes));
        std::memset(memory_.address(address), 0, static_cast<std::size_t>(bytes));
    } catch (...) {
        blocks_.erase(it);
        throw;
    }
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
        cursor = std::uint64_t(base) + occupied.size;
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
void GuestVirtualAllocator::reserve_and_track(
    std::uint32_t address, std::uint64_t bytes) {
    blocks_.emplace(
        address, Block{bytes, std::vector<std::uint8_t>(bytes / kPageSize, 0)});
}
std::optional<std::uint32_t> GuestVirtualAllocator::reserve(
    std::uint64_t bytes, std::uint64_t alignment) {
    const auto length = rounded_size(bytes);
    if (alignment < kPageSize || !power_of_two(alignment))
        throw std::invalid_argument("Guest alignment must be a page-sized power of two");
    if (length > std::uint64_t(limit_) - first_ || alignment > limit_)
        return std::nullopt;
    std::uint64_t cursor = first_;
    for (const auto& [base, occupied] : blocks_) {
        if (const auto candidate = find_available_in_gap(cursor, base, length, alignment)) {
            reserve_and_track(*candidate, length);
            return candidate;
        }
        cursor = std::uint64_t(base) + occupied.size;
    }
    if (const auto candidate = find_available_in_gap(cursor, limit_, length, alignment)) {
        reserve_and_track(*candidate, length);
        return candidate;
    }
    return std::nullopt;
}
bool GuestVirtualAllocator::reserve_at(std::uint32_t address, std::uint64_t bytes) {
    const auto length = rounded_size(bytes);
    if (!page_aligned(address) || !free_range(address, length)) return false;
    reserve_and_track(address, length);
    return true;
}
bool GuestVirtualAllocator::reserved_range(
    std::uint32_t address, std::uint64_t bytes) const noexcept {
    if (!bytes || !page_aligned(address) || !page_aligned(bytes))
        return false;
    auto it = blocks_.upper_bound(address);
    if (it == blocks_.begin()) return false;
    --it;
    return address >= it->first &&
        std::uint64_t(address) + bytes <= std::uint64_t(it->first) + it->second.size;
}
bool GuestVirtualAllocator::commit_reserved(std::uint32_t address, std::uint64_t bytes) {
    const auto length = rounded_size(bytes);
    if (!reserved_range(address, length) ||
        memory_.any_committed_range(address, static_cast<std::size_t>(length)))
        return false;
    auto it = std::prev(blocks_.upper_bound(address));
    const auto first = (std::uint64_t(address) - it->first) / kPageSize;
    const auto count = length / kPageSize;
    for (std::uint64_t page = first; page < first + count; ++page)
        if (it->second.pages[page]) return false;
    memory_.commit(address, static_cast<std::size_t>(length));
    std::memset(memory_.address(address), 0, static_cast<std::size_t>(length));
    std::fill(it->second.pages.begin() + first,
              it->second.pages.begin() + first + count, 1);
    allocated_bytes_ += length;
    return true;
}
bool GuestVirtualAllocator::decommit_reserved(std::uint32_t address, std::uint64_t bytes) {
    const auto length = rounded_size(bytes);
    if (!reserved_range(address, length)) return false;
    auto it = std::prev(blocks_.upper_bound(address));
    const auto first = (std::uint64_t(address) - it->first) / kPageSize;
    const auto count = length / kPageSize;
    for (std::uint64_t page = first; page < first + count; ++page)
        if (!it->second.pages[page]) return false;
    // Update accounting only after host pages have actually been revoked.
    memory_.decommit(address, static_cast<std::size_t>(length));
    std::fill(it->second.pages.begin() + first,
              it->second.pages.begin() + first + count, 0);
    allocated_bytes_ -= length;
    return true;
}
bool GuestVirtualAllocator::release(std::uint32_t address) {
    const auto it = blocks_.find(address);
    if (it == blocks_.end()) return false;
    // A partially committed reservation may contain several separate runs.
    // If any host decommit fails, keep the reservation and the remaining
    // page accounting intact so the caller can safely retry.
    auto& pages = it->second.pages;
    for (std::size_t first = 0; first < pages.size();) {
        if (!pages[first]) {
            ++first;
            continue;
        }
        auto end = first + 1;
        while (end < pages.size() && pages[end]) ++end;
        const auto length = (end - first) * kPageSize;
        memory_.decommit(std::uint64_t(address) + first * kPageSize,
                         static_cast<std::size_t>(length));
        std::fill(pages.begin() + first, pages.begin() + end, 0);
        allocated_bytes_ -= length;
        first = end;
    }
    blocks_.erase(it);
    return true;
}
}
