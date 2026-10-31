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
PPC_EXTERN_FUNC(__imp__VdSetSystemCommandBufferGpuIdentifierAddress);

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
        // Retail driver registers the guest address of its GPU command ID.
        ctx.r3.u64 = 0;
        __imp__VdSetSystemCommandBufferGpuIdentifierAddress(ctx, memory.base());
        check(dotm::vd_bound_gpu_snapshot().system_command_buffer_gpu_identifier_address == 0,
              "Zero detaches system command-buffer GPU identifier");
        ctx.r3.u64 = writeback;
        __imp__VdSetSystemCommandBufferGpuIdentifierAddress(ctx, memory.base());
        check(dotm::vd_bound_gpu_snapshot().system_command_buffer_gpu_identifier_address == writeback,
              "Original PPC r3 registers writable guest GPU identifier address");
        ctx.r3.u64 = 0xFFFFFFFFU;
        expect_throw([&] { __imp__VdSetSystemCommandBufferGpuIdentifierAddress(ctx, memory.base()); },
                     "Unmapped GPU identifier address rejected");
        check(dotm::vd_bound_gpu_snapshot().system_command_buffer_gpu_identifier_address == writeback,
              "Invalid GPU identifier registration leaves previous address intact");
        // Exercise actual big-endian PM4 packets from the allocated DMA ring.
        auto put = [&](std::uint32_t index, std::uint32_t value) {
            auto* b = memory.address(ring) + index * 4;
            b[0] = static_cast<std::uint8_t>(value >> 24);
            b[1] = static_cast<std::uint8_t>(value >> 16);
            b[2] = static_cast<std::uint8_t>(value >> 8);
            b[3] = static_cast<std::uint8_t>(value);
        };
        put(0, (1U << 16) | 0x123U);
        put(1, 0x11223344U);
        put(2, 0x55667788U);
        put(3, 0x40000000U | (0x23U << 11) | 0x22U);
        put(4, 0xAABBCCDDU);
        put(5, 0xEEFF0099U);
        put(6, 0x80000000U);
        put(7, 0xC0000000U | (1U << 16) | (0x10U << 8));
        put(8, 0x11111111U);
        put(9, 0x22222222U);
        put(10, 0xC0000000U | (2U << 16) | (0x55U << 8));
        put(11, 0x4100U);
        put(12, 0xDEADBEEFU);
        put(13, 0x10203040U);
        put(14, 0xC0000000U | (1U << 16) | (0x2DU << 8));
        put(15, (4U << 16) | 0x40U);
        put(16, 0x0BADCAFEU);
        put(17, (2U << 16) | 0x8000U | 0x20U);
        put(18, 0x11111111U);
        put(19, 0x22222222U);
        put(20, 0x33333333U);
        check(dotm::vd_bound_execute_ring(21) == 7,
              "Xenos PM4 type-0/1/2/3 packets executed from real ring");
        state = dotm::vd_bound_gpu_snapshot();
        check(state.ring_read_pointer_dwords == 21 &&
              state.packets_executed == 7 && state.register_writes == 10 &&
              dotm::vd_bound_read_gpu_register(0x123) == 0x11223344U &&
              dotm::vd_bound_read_gpu_register(0x124) == 0x55667788U &&
              dotm::vd_bound_read_gpu_register(0x22) == 0xAABBCCDDU &&
              dotm::vd_bound_read_gpu_register(0x23) == 0xEEFF0099U &&
              dotm::vd_bound_read_gpu_register(0x4100) == 0xDEADBEEFU &&
              dotm::vd_bound_read_gpu_register(0x4101) == 0x10203040U &&
              dotm::vd_bound_read_gpu_register(0x2040) == 0x0BADCAFEU &&
              dotm::vd_bound_read_gpu_register(0x20) == 0x33333333U,
              "PM4 register state and repeated writes are preserved");
        check(memory.address(writeback)[3] == 21,
              "GPU read pointer writeback advances in guest big-endian memory");
        put(21, (1U << 16) | 0x100U);
        put(22, 0xABCDEF12U);
        expect_throw([&] { dotm::vd_bound_execute_ring(23); },
                     "Incomplete PM4 packet rejected without consuming it");
        check(dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 21,
              "Incomplete PM4 does not advance read pointer");
        put(23, 0xFEEDCAFEU);
        check(dotm::vd_bound_execute_ring(24) == 1 &&
              dotm::vd_bound_read_gpu_register(0x101) == 0xFEEDCAFEU,
              "Incomplete packet resumes once producer finishes payload");
        put(24, 0xC0000000U | (0x22U << 8));
        put(25, 0x1234U);
        expect_throw([&] { dotm::vd_bound_execute_ring(26); },
                     "Unimplemented Xenos draw packet must not be silently skipped");
        check(dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 24,
              "Unsupported draw command remains pending");
        // Large NOP advances the read pointer to the final ring dword.
        put(24, 0xC0000000U | (997U << 16) | (0x10U << 8));
        check(dotm::vd_bound_execute_ring(1023) == 1 &&
              dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 1023,
              "Large PM4 NOP advances to ring boundary");
        put(1023, (1U << 16) | 0x150U);
        put(0, 0xCAFE0123U);
        put(1, 0xCAFE4567U);
        check(dotm::vd_bound_execute_ring(2) == 1 &&
              dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 2 &&
              dotm::vd_bound_read_gpu_register(0x150) == 0xCAFE0123U &&
              dotm::vd_bound_read_gpu_register(0x151) == 0xCAFE4567U &&
              memory.address(writeback)[3] == 2,
              "PM4 payload wraps across physical ring boundary");
        expect_throw([&] { dotm::vd_bound_execute_ring(1024); },
                     "Out-of-range GPU write pointer rejected");
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