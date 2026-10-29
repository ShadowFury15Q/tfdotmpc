#include "guest_virtual_allocator.hpp"
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool pass, const char* message) {
    if (!pass) throw std::runtime_error(message);
}
template <typename F> void reject(F&& fn, const char* message) {
    bool caught = false;
    try { fn(); } catch (const std::invalid_argument&) { caught = true; }
    require(caught, message);
}
}
int main() {
    try {
        dotm::GuestMemory memory;
        using Alloc = dotm::GuestVirtualAllocator;
        constexpr std::uint32_t first = 0x20000000U;
        constexpr std::uint32_t limit = first + 8 * 4096;
        Alloc heap(memory, first, limit);
        const auto a = heap.allocate(1);
        const auto b = heap.allocate(4097);
        require(a == first && b == first + 4096,
                "First-fit and 4 KiB rounding");
        require(heap.allocated_bytes() == 3 * 4096 &&
                heap.allocation_count() == 2, "Guest allocation accounting");
        memory.address(*a)[0] = 0xCD;
        require(!heap.allocate_at(first, 4096), "Reject fixed overlap");
        require(!heap.allocate_at(first + 1, 4096), "Reject unaligned address");
        require(!heap.allocate_at(limit, 4096), "Reject out-of-window address");
        require(heap.allocate_at(first + 5 * 4096, 4096),
                "Reserve fixed guest virtual address");
        const auto aligned = heap.allocate(4096, 4 * 4096);
        require(aligned == first + 4 * 4096, "Honor alignment across holes");
        require(!heap.allocate(3 * 4096), "Exhaustion returns nullopt");
        require(heap.allocation_count() == 4, "Failed allocation is atomic");
        require(heap.release(*a), "Release allocation base");
        require(!memory.committed_range(*a, 1),
                "Released allocation must revoke committed guest pages");
        require(memory.committed_range(*b, 1),
                "Releasing one allocation preserves its neighbor");
        require(!heap.release(*a), "Double free rejected");
        require(!heap.release(*b + 4096), "Interior-pointer free rejected");
        const auto reused = heap.allocate(4096);
        require(reused == first && memory.address(*reused)[0] == 0,
                "Freed guest pages reused with zero initialization");
        require(heap.release(*b), "Release two-page allocation");
        require(!memory.committed_range(*b, 8192),
                "All pages of a multi-page allocation are decommitted");
        const auto merged = heap.allocate(3 * 4096);
        require(merged == first + 4096,
                "Adjacent free pages coalesce for first-fit search");
        const auto before = heap.allocated_bytes();
        reject([&]{ (void)heap.allocate(0); }, "Reject zero-size allocation");
        reject([&]{ (void)heap.allocate(std::numeric_limits<std::uint64_t>::max()); },
               "Reject overflowing guest allocation");
        reject([&]{ (void)heap.allocate(4096, 3000); },
               "Reject non-power-of-two alignment");
        reject([&]{ (void)heap.allocate_at(first, 0); },
               "Reject zero-size fixed allocation");
        require(heap.allocated_bytes() == before, "Rejected sizes must not mutate heap");
        reject([&]{ Alloc bad(memory, 0, limit); }, "Reject null-guard overlap");
        reject([&]{ Alloc bad(memory, first, dotm::GuestMemory::kImageBase + 4096); },
               "Reject guest PE image overlap");
        std::cout << "DOTM guest virtual allocator synthetic tests PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
