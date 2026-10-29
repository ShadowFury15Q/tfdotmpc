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
        // An independently committed page is not owned by this allocator.
        // Neither automatic nor fixed allocations may zero its contents.
        dotm::GuestMemory shared;
        constexpr std::uint32_t shared_first = 0x30000000U;
        Alloc guarded(shared, shared_first, shared_first + 8 * 4096);
        shared.commit(shared_first, 4096);
        shared.commit(shared_first + 3 * 4096, 4096);
        shared.commit(shared_first + 6 * 4096, 4096);
        shared.address(shared_first)[0] = 0xA1;
        shared.address(shared_first + 3 * 4096)[0] = 0xB2;
        shared.address(shared_first + 6 * 4096)[0] = 0xC3;
        require(!guarded.allocate_at(shared_first, 4096),
                "Fixed allocation must not overwrite an external page");
        require(!guarded.allocate_at(shared_first + 2 * 4096, 8192),
                "Fixed allocation must detect partial committed overlap");
        const auto guarded_a = guarded.allocate(8192);
        const auto guarded_b = guarded.allocate(8192);
        require(guarded_a == shared_first + 4096 &&
                guarded_b == shared_first + 4 * 4096,
                "First-fit must skip externally committed pages");
        require(shared.address(shared_first)[0] == 0xA1 &&
                shared.address(shared_first + 3 * 4096)[0] == 0xB2 &&
                shared.address(shared_first + 6 * 4096)[0] == 0xC3,
                "External guest bytes must survive allocator operations");
        require(!guarded.allocate(2 * 4096),
                "External pages count as unavailable on exhaustion");
        require(guarded.release(*guarded_a), "Guarded allocation release");
        require(shared.committed_range(shared_first, 1) &&
                shared.committed_range(shared_first + 3 * 4096, 1),
                "Allocator release must not revoke external pages");
        shared.decommit(shared_first + 3 * 4096, 4096);
        require(guarded.allocate_at(shared_first + 3 * 4096, 4096),
                "Explicitly decommitted external page becomes available");
        // Fragmented external commits must not cause repeated full-range
        // scans or let first-fit allocations overwrite foreign guest pages.
        dotm::GuestMemory fragmented;
        constexpr std::uint32_t fragmented_first = 0x40000000U;
        Alloc fragmented_heap(fragmented, fragmented_first,
                              fragmented_first + 64 * 4096);
        for (std::uint32_t page = 0; page < 64; page += 5) {
            const auto address = fragmented_first + page * 4096;
            fragmented.commit(address, 4096);
            fragmented.address(address)[0] = static_cast<std::uint8_t>(page + 1);
        }
        require(!fragmented_heap.allocate(5 * 4096),
                "Fragmented guest space cannot satisfy a five-page run");
        const auto fragmented_a = fragmented_heap.allocate(4 * 4096);
        const auto fragmented_b = fragmented_heap.allocate(3 * 4096, 8192);
        require(fragmented_a == fragmented_first + 4096 &&
                fragmented_b == fragmented_first + 6 * 4096,
                "Fragmented first-fit respects alignment and committed holes");
        require(fragmented_heap.allocation_count() == 2 &&
                fragmented_heap.allocated_bytes() == 7 * 4096,
                "Fragmented allocation accounting stays consistent");
        for (std::uint32_t page = 0; page < 64; page += 5) {
            const auto address = fragmented_first + page * 4096;
            require(fragmented.address(address)[0] == page + 1,
                    "Fragmented allocator preserves externally owned pages");
        }
        std::cout << "DOTM guest virtual allocator synthetic tests PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
