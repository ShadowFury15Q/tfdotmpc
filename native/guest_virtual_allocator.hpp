#pragma once
#include "guest_memory.hpp"
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>

namespace dotm {
// Native low-address guest allocator. Backend for future Xbox kernel memory
// APIs; this class does NOT claim to implement the Xbox kernel ABI.
class GuestVirtualAllocator {
public:
    static constexpr std::uint64_t kPageSize = 4096;
    GuestVirtualAllocator(GuestMemory& memory,
                          std::uint32_t first = 0x10000000U,
                          std::uint32_t limit = 0x70000000U);
    // Page-rounded, zero-initialized, first-fit allocation. On exhaustion
    // returns nullopt without changing allocations.
    std::optional<std::uint32_t> allocate(
        std::uint64_t bytes, std::uint64_t alignment = kPageSize);
    // Reserve a specific page-aligned guest address. False if unavailable.
    bool allocate_at(std::uint32_t address, std::uint64_t bytes);
    // Exact-base release revokes host access; failure preserves tracking.
    bool release(std::uint32_t address);
    // Exact-base query used by the NT MEM_RELEASE ABI; interior pointers fail.
    std::optional<std::uint64_t> allocation_size(std::uint32_t address) const noexcept {
        const auto it = blocks_.find(address);
        if (it == blocks_.end()) return std::nullopt;
        return it->second;
    }
    std::size_t allocation_count() const noexcept { return blocks_.size(); }
    std::uint64_t allocated_bytes() const noexcept { return allocated_bytes_; }

private:
    static std::uint64_t rounded_size(std::uint64_t bytes);
    bool free_range(std::uint64_t address, std::uint64_t bytes) const;
    void commit_and_track(std::uint32_t address, std::uint64_t bytes);
    GuestMemory& memory_;
    std::uint32_t first_, limit_;
    std::map<std::uint32_t, std::uint64_t> blocks_;
    std::uint64_t allocated_bytes_ = 0;
};
}
