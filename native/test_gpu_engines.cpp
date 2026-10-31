#include "ppc_recomp_shared.h"
#include "gpu_engines.hpp"
#include "kernel_guest_memory.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>

PPC_EXTERN_FUNC(__imp__VdInitializeEngines);
PPC_EXTERN_FUNC(__imp__VdShutdownEngines);
PPC_EXTERN_FUNC(__imp__VdSetGraphicsInterruptCallback);
PPC_EXTERN_FUNC(__imp__VdInitializeRingBuffer);
PPC_EXTERN_FUNC(__imp__VdEnableRingBufferRPtrWriteBack);

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void expect_throw(auto&& operation, const char* message) {
    bool threw = false;
    try { operation(); } catch (const std::runtime_error&) { threw = true; }
    check(threw, message);
}
}
int main() {
    try {
        dotm::GuestMemory memory;
        dotm::GuestVirtualAllocator allocator(memory, 0x40000000U, 0x40010000U);
        constexpr std::uint32_t image = 0x82010000U;
        memory.commit(image, 4096);
        for (std::uint32_t i = 0; i < 128; ++i)
            memory.address(image)[i] = static_cast<std::uint8_t>(i * 3 + 1);
        dotm::bind_kernel_guest_memory(&memory, &allocator);
        dotm::bind_gpu_engines(&memory);
        PPCContext ctx{};
        ctx.r3.u64 = 0x4F810001U;
        ctx.r4.u64 = image + 128;
        ctx.r5.u64 = 0xCAFEU;
        ctx.r6.u64 = image;
        ctx.r7.u64 = image + 64;
        __imp__VdInitializeEngines(ctx, memory.base());
        check(ctx.r3.u32 == 1, "PPC r3-r7 GPU initialization ABI");
        auto state = dotm::vd_bound_gpu_snapshot();
        check(state.initialized && state.startup_callback == image + 128 &&
              state.startup_context == 0xCAFE &&
              state.pfp_microcode_fingerprint && state.me_microcode_fingerprint &&
              state.pfp_microcode_fingerprint != state.me_microcode_fingerprint,
              "Original guest microcode fingerprint and callback captured");
        ctx.r3.u64 = image + 132;
        ctx.r4.u64 = 0xF00DU;
        __imp__VdSetGraphicsInterruptCallback(ctx, memory.base());
        state = dotm::vd_bound_gpu_snapshot();
        check(state.interrupt_callback == image + 132 &&
              state.interrupt_context == 0xF00DU,
              "PPC interrupt callback and context preserved");
        const auto ring = dotm::mm_bound_allocate_physical_memory_ex(
            2, 4096, 0x404, 0, 0xFFFFFFFFU, 4096);
        check(ring != 0, "Actual Xbox physical allocation for command ring");
        const auto physical = dotm::mm_bound_get_physical_address(ring);
        check(dotm::mm_bound_resolve_physical_range(physical, 4096) == ring &&
              !dotm::mm_bound_resolve_physical_range(physical, 8192),
              "GPU DMA resolves only allocated physical range");
        ctx.r3.u64 = physical;
        ctx.r4.u64 = 9;
        __imp__VdInitializeRingBuffer(ctx, memory.base());
        state = dotm::vd_bound_gpu_snapshot();
        check(state.ring_initialized && state.ring_size_bytes == 4096 &&
              state.ring_guest_address == ring,
              "GPU ring length is 1 << (size_log2 + 3)");
        const auto writeback = dotm::mm_bound_allocate_physical_memory_ex(
            2, 4096, 0x404, 0, 0xFFFFFFFFU, 4096);
        check(writeback != 0 && writeback != ring,
              "GPU read pointer uses separate allocated physical page");
        const auto writeback_physical = dotm::mm_bound_get_physical_address(writeback);
        memory.address(writeback)[0] = 0xFF;
        ctx.r3.u64 = writeback_physical;
        ctx.r4.u64 = 6;
        __imp__VdEnableRingBufferRPtrWriteBack(ctx, memory.base());
        state = dotm::vd_bound_gpu_snapshot();
        check(state.read_pointer_writeback_enabled &&
              state.read_pointer_writeback_guest_address == writeback &&
              state.read_pointer_writeback_block_log2 == 6 &&
              memory.address(writeback)[0] == 0,
              "GPU read pointer writeback uses real guest physical memory");
        expect_throw([&] { dotm::vd_bound_initialize_ring_buffer(0x100, 9); },
                     "Unallocated physical command ring rejected");
        expect_throw([&] { dotm::vd_bound_initialize_ring_buffer(physical, 22); },
                     "Out-of-range command ring exponent rejected");
        expect_throw([&] { dotm::vd_bound_enable_ring_buffer_read_pointer_writeback(
                            writeback_physical + 1, 6); },
                     "Unaligned read-pointer writeback rejected");
        check(dotm::vd_bound_gpu_snapshot().ring_initialized,
              "Rejected GPU setup does not corrupt initialized ring");
        ctx.r3.u64 = 0;
        ctx.r4.u64 = 0;
        __imp__VdSetGraphicsInterruptCallback(ctx, memory.base());
        check(dotm::vd_bound_gpu_snapshot().interrupt_callback == 0,
              "Guest GPU interrupt unregister supported");
        __imp__VdShutdownEngines(ctx, memory.base());
        check(!dotm::vd_bound_gpu_snapshot().initialized,
              "VdShutdownEngines clears real command processor state");
        ctx.r3.u64 = 0x4F810001U;
        ctx.r4.u64 = image + 128;
        ctx.r5.u64 = 0;
        ctx.r6.u64 = image;
        ctx.r7.u64 = image + 0x1000;
        __imp__VdInitializeEngines(ctx, memory.base());
        check(ctx.r3.u32 == 0 && !dotm::vd_bound_gpu_snapshot().initialized,
              "Uncommitted ME microcode fails rather than fake GPU success");
        dotm::bind_gpu_engines(nullptr);
        dotm::bind_kernel_guest_memory(nullptr, nullptr);
        std::cout << "Xbox 360 GPU engine and physical ring PPC ABI PASS\n";
        return 0;
    } catch (const std::exception& e) {
        dotm::bind_gpu_engines(nullptr);
        dotm::bind_kernel_guest_memory(nullptr, nullptr);
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}