#include "ppc_recomp_shared.h"
#include "gpu_engines.hpp"
#include "kernel_guest_memory.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>

PPC_EXTERN_FUNC(__imp__VdInitializeEngines);
PPC_EXTERN_FUNC(__imp__VdCallGraphicsNotificationRoutines);
PPC_EXTERN_FUNC(__imp__VdShutdownEngines);
PPC_EXTERN_FUNC(__imp__VdRetrainEDRAMWorker);
PPC_EXTERN_FUNC(__imp__VdRetrainEDRAM);
PPC_EXTERN_FUNC(__imp__VdIsHSIOTrainingSucceeded);
PPC_EXTERN_FUNC(__imp__VdEnableDisableClockGating);
PPC_EXTERN_FUNC(__imp__VdSetGraphicsInterruptCallback);
PPC_EXTERN_FUNC(__imp__VdInitializeRingBuffer);
PPC_EXTERN_FUNC(__imp__VdEnableRingBufferRPtrWriteBack);
PPC_EXTERN_FUNC(__imp__VdSetSystemCommandBufferGpuIdentifierAddress);
PPC_EXTERN_FUNC(__imp__VdGetSystemCommandBuffer);
PPC_EXTERN_FUNC(__imp__VdSwap);
PPC_EXTERN_FUNC(__imp__VdSetDisplayMode);
PPC_EXTERN_FUNC(__imp__VdInitializeScalerCommandBuffer);
PPC_EXTERN_FUNC(__imp__VdGetCurrentDisplayInformation);
PPC_EXTERN_FUNC(__imp__VdPersistDisplay);
PPC_EXTERN_FUNC(__imp__KiApcNormalRoutineNop);

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
        // The original video driver writes MMIO directly, bypassing imports.
        // Map its 512 KiB Xenos register aperture into committed guest pages.
        dotm::vd_bound_map_gpu_mmio();
        check(memory.committed_range(0x7FC80000U, 0x80000U),
              "Original Xenos MMIO aperture committed before PPC startup");
        constexpr std::uint32_t mmio = 0x7FC80714U;
        auto* mmio_bytes = memory.address(mmio);
        mmio_bytes[0] = 0x12; mmio_bytes[1] = 0x34;
        mmio_bytes[2] = 0x56; mmio_bytes[3] = 0x78;
        check(dotm::vd_bound_read_gpu_mmio_register(0x714U / 4U) == 0x12345678U &&
              dotm::vd_bound_read_gpu_register(0x714U / 4U) == 0x12345678U,
              "Original PPC MMIO writes are visible to the native PM4 register file");
        dotm::vd_bound_map_gpu_mmio(); // idempotent
        expect_throw([&] { (void)dotm::vd_bound_read_gpu_mmio_register(0x5000U); },
                     "GPU MMIO register reads reject out-of-range indexes");
        PPCContext ctx{};
        // Original DOTM 0x82525808 EDRAM retrain ABI: two BE32 guest
        // slots, 4096-byte calibration region, 2048-byte stack scratch.
        constexpr std::uint32_t edram_offset = image + 0x180U;
        constexpr std::uint32_t edram_status = image + 0x184U;
        constexpr std::uint32_t edram_scratch = image + 0x800U;
        __imp__VdIsHSIOTrainingSucceeded(ctx, memory.base());
        check(ctx.r3.u32 == 0U, "HSIO training is not reported before calibration");
        ctx.r3.u32 = 0;
        __imp__VdRetrainEDRAMWorker(ctx, memory.base());
        check(ctx.r3.u32 == 0 && dotm::vd_bound_gpu_snapshot().edram_worker_calls == 1,
              "Original PPC EDRAM worker ABI");
        ctx.r3.u32 = 0;
        ctx.r4.u32 = edram_offset;
        ctx.r5.u32 = 4096;
        ctx.r6.u32 = edram_status;
        ctx.r7.u32 = edram_scratch;
        ctx.r8.u32 = 2048;
        __imp__VdRetrainEDRAM(ctx, memory.base());
        auto edram_state = dotm::vd_bound_gpu_snapshot();
        check(ctx.r3.u32 == 0 && edram_state.edram_trained &&
              edram_state.edram_backing_size == 10U * 1024U * 1024U &&
              edram_state.edram_retrain_calls == 1 &&
              memory.address(edram_offset)[0] == 0 &&
              memory.address(edram_status)[0] == 0,
              "EDRAM backing calibrated without corrupting original guest outputs");
        __imp__VdIsHSIOTrainingSucceeded(ctx, memory.base());
        check(ctx.r3.u32 == 1U, "HSIO training status reflects verified EDRAM backing");
        ctx.r4.u32 = 0xDEAD0000U;
        expect_throw([&] { __imp__VdRetrainEDRAM(ctx, memory.base()); },
                     "Uncommitted EDRAM guest output rejected");
        check(dotm::vd_bound_gpu_snapshot().edram_retrain_calls == 1,
              "Invalid EDRAM retrain leaves prior state intact");
        ctx.r3.u32 = 0;
        __imp__VdEnableDisableClockGating(ctx, memory.base());
        check(ctx.r3.u32 == 0 && !dotm::vd_bound_gpu_snapshot().clock_gating_enabled,
              "DOTM GPU clock-gating disable");
        ctx.r3.u32 = 1;
        __imp__VdEnableDisableClockGating(ctx, memory.base());
        check(dotm::vd_bound_gpu_snapshot().clock_gating_enabled &&
              dotm::vd_bound_gpu_snapshot().clock_gating_changes == 2,
              "DOTM GPU clock-gating enable");
        ctx.r3.u32 = 2;
        expect_throw([&] { __imp__VdEnableDisableClockGating(ctx, memory.base()); },
                     "Invalid GPU clock-gating mode rejected");
        ctx.r3.u64 = 0x4F810001U;
        ctx.r4.u64 = image + 128;
        ctx.r5.u64 = 0xCAFEU;
        ctx.r6.u64 = image;
        ctx.r7.u64 = image + 64;
        __imp__VdInitializeEngines(ctx, memory.base());
        check(ctx.r3.u32 == 1, "PPC r3-r7 GPU initialization ABI");
        // Actual DOTM PPC 0x8251286C uses r3-r10 plus width/height
        // pointers spilled at SP+84 and SP+92. The fetch describes a
        // virtual D3D frontbuffer, which the GPU needs as physical memory.
        constexpr std::uint32_t swap_fetch = image + 0xA00U;
        constexpr std::uint32_t swap_command = image + 0xB00U;
        constexpr std::uint32_t swap_args = image + 0xC00U;
        constexpr std::uint32_t swap_stack = image + 0xD00U;
        constexpr std::uint32_t swap_system = image + 0xE00U;
        auto swap_store = [&](std::uint32_t address, std::uint32_t value) {
            auto* b = memory.address(address);
            b[0] = static_cast<std::uint8_t>(value >> 24);
            b[1] = static_cast<std::uint8_t>(value >> 16);
            b[2] = static_cast<std::uint8_t>(value >> 8);
            b[3] = static_cast<std::uint8_t>(value);
        };
        auto swap_load = [&](std::uint32_t address) -> std::uint32_t {
            const auto* b = memory.address(address);
            return (std::uint32_t(b[0]) << 24) | (std::uint32_t(b[1]) << 16) |
                   (std::uint32_t(b[2]) << 8) | std::uint32_t(b[3]);
        };
        const auto swap_virtual = dotm::mm_bound_allocate_physical_memory_ex(
            1U, 4096U, 4U, 0U, 0x1FFFFFFFU, 4096U);
        check(swap_virtual != 0U, "VdSwap test allocates actual Xbox physical pages");
        const auto swap_physical = dotm::mm_bound_get_physical_address(swap_virtual);
        swap_store(swap_fetch, 2U);
        swap_store(swap_fetch + 4U, (swap_virtual & 0xFFFFF000U) | 6U);
        swap_store(swap_fetch + 8U, (1U << 13U) | 1U); // 2x2 frontbuffer
        for (std::uint32_t i = 3U; i < 6U; ++i)
            swap_store(swap_fetch + i * 4U, 0x100U + i);
        swap_store(swap_args, swap_virtual);
        swap_store(swap_args + 4U, 6U);
        swap_store(swap_args + 8U, 0U);
        swap_store(swap_args + 12U, 2U);
        swap_store(swap_args + 16U, 2U);
        swap_store(swap_stack + 84U, swap_args + 12U);
        swap_store(swap_stack + 92U, swap_args + 16U);
        swap_store(swap_command, 0xDEADBEEFU);
        ctx.r1.u64 = swap_stack;
        ctx.r3.u64 = swap_command;
        ctx.r4.u64 = swap_fetch;
        ctx.r5.u64 = swap_args + 32U;
        ctx.r6.u64 = swap_system;
        ctx.r7.u64 = 1U;
        ctx.r8.u64 = swap_args;
        ctx.r9.u64 = swap_args + 4U;
        ctx.r10.u64 = swap_args + 8U;
        swap_store(swap_args + 4U, 7U);
        expect_throw([&] { __imp__VdSwap(ctx, memory.base()); },
                     "VdSwap rejects mismatch between PPC texture and fetch format");
        check(swap_load(swap_command) == 0xDEADBEEFU &&
              dotm::vd_bound_gpu_snapshot().swap_requests == 0U,
              "Rejected VdSwap leaves original guest command slot untouched");
        swap_store(swap_args + 4U, 6U);
        __imp__VdSwap(ctx, memory.base());
        const auto swap_state = dotm::vd_bound_gpu_snapshot();
        check(swap_state.swap_requests == 1U &&
              swap_state.last_swap_physical_address == swap_physical &&
              swap_state.last_swap_width == 2U &&
              swap_state.last_swap_height == 2U &&
              swap_state.last_swap_texture_format == 6U &&
              swap_state.gpu_frame_counter == 0U &&
              swap_load(swap_command) == 0x00054800U &&
              swap_load(swap_command + 4U) == 2U &&
              swap_load(swap_command + 8U) ==
                  ((swap_physical & 0xFFFFF000U) | 6U) &&
              swap_load(swap_command + 28U) == 0x80000000U &&
              swap_load(swap_command + 252U) == 0x80000000U,
              "VdSwap publishes real physical PM4 fetch without fake GPU present");
        // Actual retail DOTM calls VdSetDisplayMode(0x40000000) and
        // reads X_DISPLAY_INFO fields at offsets 0x48, 0x4A, 0x56.
        ctx.r3.u64 = 0x40000000U;
        __imp__VdSetDisplayMode(ctx, memory.base());
        check(ctx.r3.u32 == 0U &&
              dotm::vd_bound_gpu_snapshot().display_mode_changes == 1U,
              "Retail 720p RGB display mode registered by Xbox kernel ABI");
        constexpr std::uint32_t display_info = image + 0xF00U;
        ctx.r3.u64 = display_info;
        __imp__VdGetCurrentDisplayInformation(ctx, memory.base());
        check(swap_load(display_info) == 0x050002D0U &&
              swap_load(display_info + 0x10U) == 1280U &&
              swap_load(display_info + 0x14U) == 720U &&
              swap_load(display_info + 0x4CU) == 0x42700000U &&
              memory.address(display_info)[0x48U] == 0x05U &&
              memory.address(display_info)[0x49U] == 0x00U &&
              memory.address(display_info)[0x4AU] == 0x02U &&
              memory.address(display_info)[0x4BU] == 0xD0U &&
              memory.address(display_info)[0x56U] == 0x05U,
              "X_DISPLAY_INFO matches actual DOTM PPC big-endian field reads");
        // The original scaler import has 12 arguments: eight registers and
        // four spill slots. Identity 720p scanout needs no scaler PM4 words.
        // This is not a substitute for the GPU's actual frontbuffer present.
        constexpr std::uint32_t scaler_commands = image + 0x200U;
        constexpr std::uint32_t scaler_vfilter = image + 0x700U;
        constexpr std::uint32_t scaler_hfilter = image + 0x720U;
        swap_store(scaler_commands, 0xDEADBEEFU);
        swap_store(swap_stack + 84U, scaler_hfilter);
        swap_store(swap_stack + 92U, 0U);
        swap_store(swap_stack + 100U, scaler_commands);
        swap_store(swap_stack + 108U, 200U);
        ctx.r1.u64 = swap_stack;
        ctx.r3.u64 = 0U;
        // Original _xstart call passes 0x050002D0 (width:height),
        // with both filter modes set to 7.
        ctx.r4.u64 = (1280U << 16U) | 720U;
        ctx.r5.u64 = 0U;
        ctx.r6.u64 = (1280U << 16U) | 720U;
        ctx.r7.u64 = (1280U << 16U) | 720U;
        ctx.r8.u64 = 7U;
        ctx.r9.u64 = scaler_vfilter;
        ctx.r10.u64 = 7U;
        __imp__VdInitializeScalerCommandBuffer(ctx, memory.base());
        const auto scaler_state = dotm::vd_bound_gpu_snapshot();
        check(ctx.r3.u32 == 0U && scaler_state.identity_scanout_configured &&
              scaler_state.identity_scanout_requests == 1U &&
              scaler_state.identity_scanout_width == 1280U &&
              scaler_state.identity_scanout_height == 720U &&
              swap_load(scaler_commands) == 0xDEADBEEFU,
              "Identity 720p scanout records native dimensions without fake GPU commands");
        ctx.r6.u64 = (1920U << 16U) | 1080U;
        expect_throw([&] { __imp__VdInitializeScalerCommandBuffer(ctx, memory.base()); },
                     "Non-identity scaler cannot silently return a fake command stream");
        check(dotm::vd_bound_gpu_snapshot().identity_scanout_requests == 1U &&
              swap_load(scaler_commands) == 0xDEADBEEFU,
              "Rejected scaling preserves original guest buffer and GPU state");
        ctx.r6.u64 = (1280U << 16U) | 720U;
        swap_store(swap_stack + 100U, 0xDEAD0000U);
        expect_throw([&] { __imp__VdInitializeScalerCommandBuffer(ctx, memory.base()); },
                     "Uncommitted scaler output is rejected before any guest write");
        ctx.r3.u64 = 0x10000000U;
        expect_throw([&] { __imp__VdSetDisplayMode(ctx, memory.base()); },
                     "Unsupported display mode is not silently accepted");
        check(dotm::vd_bound_gpu_snapshot().display_mode_changes == 1U,
              "Rejected display mode leaves active Xbox display state intact");
        dotm::mm_bound_free_physical_memory(1U, swap_virtual);
        check(dotm::mm_bound_query_physical_allocation_size(swap_virtual) == 0U,
              "VdSwap frontbuffer test releases tracked physical pages");
        // Retail DOTM 0x82512C70 passes a guest display descriptor at r3
        // and receives a physical allocation through r4. The caller frees
        // it with MmFreePhysicalMemory(type=1). Do not fabricate a pointer.
        constexpr std::uint32_t display_descriptor = image + 0x200U;
        constexpr std::uint32_t display_allocation = image + 0x220U;
        ctx.r3.u64 = display_descriptor;
        ctx.r4.u64 = display_allocation;
        __imp__VdPersistDisplay(ctx, memory.base());
        const auto physical_display = (std::uint32_t(memory.address(display_allocation)[0]) << 24) |
                                      (std::uint32_t(memory.address(display_allocation)[1]) << 16) |
                                      (std::uint32_t(memory.address(display_allocation)[2]) << 8) |
                                       std::uint32_t(memory.address(display_allocation)[3]);
        check(ctx.r3.u32 == 1U && physical_display != 0U &&
              dotm::mm_bound_query_physical_allocation_size(physical_display) >= 64U,
              "Original VdPersistDisplay returns a real tracked Xbox physical allocation");
        dotm::mm_bound_free_physical_memory(1U, physical_display);
        check(dotm::mm_bound_query_physical_allocation_size(physical_display) == 0U,
              "Original PPC MmFreePhysicalMemory releases persisted-display storage");
        ctx.r3.u64 = display_descriptor;
        ctx.r4.u64 = display_descriptor + 8U;
        expect_throw([&] { __imp__VdPersistDisplay(ctx, memory.base()); },
                     "PersistDisplay rejects output overlapping original descriptor");
        ctx.r4.u64 = 0xDEAD0000U;
        expect_throw([&] { __imp__VdPersistDisplay(ctx, memory.base()); },
                     "PersistDisplay rejects uncommitted PPC output");
        check(dotm::vd_bound_read_gpu_register(0x714U / 4U) == 0U &&
              dotm::vd_bound_read_gpu_mmio_register(0x714U / 4U) == 0U,
              "GPU initialization clears prior direct PPC MMIO register writes");
        auto state = dotm::vd_bound_gpu_snapshot();
        check(state.initialized && state.startup_callback == image + 128 &&
              state.startup_context == 0xCAFE &&
              state.pfp_microcode_fingerprint && state.me_microcode_fingerprint &&
              state.pfp_microcode_fingerprint != state.me_microcode_fingerprint,
              "Original guest microcode fingerprint and callback captured");
        // Original PPC video scaling notification has a packed 8-byte BE
        // payload. Preserve the real dimensions for native presentation.
        constexpr std::uint32_t scaling = image + 0x180U;
        const std::uint8_t payload[8] = {0x05, 0x00, 0x02, 0xD0,
                                         0x05, 0x00, 0x02, 0xD0};
        for (unsigned i = 0; i < 8; ++i)
            memory.address(scaling)[i] = payload[i];
        ctx.r3.u32 = 1U; ctx.r4.u32 = scaling;
        __imp__VdCallGraphicsNotificationRoutines(ctx, memory.base());
        state = dotm::vd_bound_gpu_snapshot();
        check(ctx.r3.u32 == 0U && state.graphics_notifications == 1 &&
              state.last_graphics_notification == 1U &&
              state.frontbuffer_width == 1280 && state.frontbuffer_height == 720 &&
              state.backbuffer_width == 1280 && state.backbuffer_height == 720,
              "Xbox graphics notification preserves real BE scaling dimensions");
        ctx.r3.u32 = 2U; ctx.r4.u32 = scaling;
        expect_throw([&] { __imp__VdCallGraphicsNotificationRoutines(ctx, memory.base()); },
                     "Unknown graphics notification rejected");
        check(dotm::vd_bound_gpu_snapshot().graphics_notifications == 1,
              "Invalid graphics notification leaves GPU state unchanged");
        // Original DOTM's xboxkrnl ordinal 0x1DF call passes 19 words
        // starting with PM4_ME_INIT (0xC0114800).
        constexpr std::uint32_t me_command = image + 256;
        auto me_word = [](std::uint32_t i) -> std::uint32_t {
            return i == 0 ? 0xC0114800U : 0xA5A5A5A5U + i * 0x10203U;
        };
        for (std::uint32_t i = 0; i < 19; ++i) {
            const auto word = me_word(i);
            auto* b = memory.address(me_command + i * 4);
            b[0] = static_cast<std::uint8_t>(word >> 24);
            b[1] = static_cast<std::uint8_t>(word >> 16);
            b[2] = static_cast<std::uint8_t>(word >> 8);
            b[3] = static_cast<std::uint8_t>(word);
        }
        ctx.r3.u64 = me_command;
        ctx.r4.u64 = 19;
        __imp__KiApcNormalRoutineNop(ctx, memory.base());
        state = dotm::vd_bound_gpu_snapshot();
        check(state.me_init_command_verified &&
              state.me_init_command_word_count == 19 &&
              state.me_init_command_fingerprint != 0 &&
              !state.me_init_packet_submitted &&
              ctx.r3.u32 == me_command,
              "Ordinal 0x1DF verifies actual guest ME_INIT without clobbering PPC r3");
        const auto verified_hash = state.me_init_command_fingerprint;
        memory.address(me_command)[2] = 0x38; // corrupt PM4 opcode 0x48
        expect_throw([&] { __imp__KiApcNormalRoutineNop(ctx, memory.base()); },
                     "Wrong PM4 opcode rejected by Xbox ME_INIT verifier");
        memory.address(me_command)[2] = 0x48;
        check(dotm::vd_bound_gpu_snapshot().me_init_command_fingerprint == verified_hash,
              "Rejected ME_INIT packet preserves previously verified state");
        ctx.r4.u64 = 18;
        expect_throw([&] { __imp__KiApcNormalRoutineNop(ctx, memory.base()); },
                     "Wrong ME_INIT word count rejected");
        ctx.r3.u64 = image + 4092;
        ctx.r4.u64 = 19;
        expect_throw([&] { __imp__KiApcNormalRoutineNop(ctx, memory.base()); },
                     "ME_INIT command cannot read past committed guest memory");
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
        // The original _xstart graph calls VdGetSystemCommandBuffer at
        // 0x825127D8 with r3 pointing to 0x94 bytes and r4 to a BE32 fence.
        constexpr std::uint32_t system_descriptor = image + 0x300U;
        constexpr std::uint32_t system_fence = image + 0x400U;
        for (std::uint32_t i = 0; i < 0x94U; ++i)
            memory.address(system_descriptor)[i] = 0xA5U;
        ctx.r3.u32 = system_descriptor;
        ctx.r4.u32 = system_fence;
        __imp__VdGetSystemCommandBuffer(ctx, memory.base());
        check(dotm::vd_bound_gpu_snapshot().system_command_buffer_queries == 1 &&
              memory.address(system_descriptor)[0] == 0 &&
              memory.address(system_descriptor)[0x93] == 0 &&
              memory.address(system_fence)[3] == 1,
              "No XAM descriptor means an empty record but a real GPU fence sequence");
        memory.address(system_descriptor)[0] = 0x12;
        memory.address(system_descriptor)[0x93] = 0x34;
        dotm::vd_bound_install_system_command_buffer(system_descriptor, 7U);
        __imp__VdGetSystemCommandBuffer(ctx, memory.base());
        check(memory.address(system_descriptor)[0] == 0x12 &&
              memory.address(system_descriptor)[0x93] == 0x34 &&
              memory.address(system_fence)[3] == 7 &&
              dotm::vd_bound_gpu_snapshot().system_command_buffer_next_identifier == 9U,
              "Original XAM system descriptor and first GPU fence copied");
        __imp__VdGetSystemCommandBuffer(ctx, memory.base());
        check(memory.address(system_fence)[3] == 9 &&
              dotm::vd_bound_gpu_snapshot().system_command_buffer_next_identifier == 11U,
              "Successive XAM system fences advance by two");
        ctx.r4.u32 = 0xDEAD0000U;
        expect_throw([&] { __imp__VdGetSystemCommandBuffer(ctx, memory.base()); },
                     "Invalid fence output fails before descriptor is overwritten");
        check(memory.address(system_descriptor)[0] == 0x12 &&
              dotm::vd_bound_gpu_snapshot().system_command_buffer_queries == 3,
              "Invalid system buffer query preserves previous guest state");
        ctx.r4.u32 = system_fence;
        dotm::vd_bound_install_system_command_buffer(0, 0);
        __imp__VdGetSystemCommandBuffer(ctx, memory.base());
        check(memory.address(system_descriptor)[0] == 0 &&
              memory.address(system_descriptor)[0x93] == 0 &&
              memory.address(system_fence)[3] == 11 &&
              dotm::vd_bound_gpu_snapshot().system_command_buffer_next_identifier == 13U,
              "XAM descriptor detach retains the kernel GPU fence sequence");
        // Exercise actual big-endian PM4 packets from the allocated DMA ring.
        auto put = [&](std::uint32_t index, std::uint32_t value) {
            auto* b = memory.address(ring) + index * 4;
            b[0] = static_cast<std::uint8_t>(value >> 24);
            b[1] = static_cast<std::uint8_t>(value >> 16);
            b[2] = static_cast<std::uint8_t>(value >> 8);
            b[3] = static_cast<std::uint8_t>(value);
        };
        // The physical ring has 1024 DWORDs. A packet requesting all
        // 1024 as payload can never fit together with its own header.
        // Reject it rather than leaving the original title in an infinite
        // command-processor stall on every subsequent write-pointer kick.
        put(0, (1023U << 16) | 0x123U);
        expect_throw([&] { (void)dotm::vd_bound_execute_ring(1U); },
                     "Oversized Xenos PM4 packet rejected instead of stalling");
        check(dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 0U &&
              memory.address(writeback)[0] == 0U &&
              memory.address(writeback)[3] == 0U,
              "Rejected oversized PM4 packet preserves physical RPTR writeback");
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
              dotm::vd_bound_read_gpu_mmio_register(0x123) == 0x11223344U &&
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
        check(dotm::vd_bound_execute_ring(23) == 0 &&
              dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 21 &&
              memory.address(writeback)[3] == 21,
              "Incomplete PM4 packet waits without throwing or consuming the header");
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
        // A zero header is TYPE0, count=1, register=0; it is not padding.
        put(2, 0);
        put(3, 0x13572468U);
        check(dotm::vd_bound_execute_ring(4) == 1 &&
              dotm::vd_bound_read_gpu_register(0) == 0x13572468U &&
              dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 4 &&
              memory.address(writeback)[3] == 4,
              "Zero-header PM4 type-0 writes register zero and advances by two dwords");
        // Original PPC can write Xenos MMIO directly between PM4 packets.
        // REG_RMW must read that CPU-written value, not a stale shadow copy.
        auto* cpu_register = memory.address(0x7FC80000U + 0x123U * 4U);
        cpu_register[0] = 0xFE; cpu_register[1] = 0xDC;
        cpu_register[2] = 0xBA; cpu_register[3] = 0x98;
        check(dotm::vd_bound_read_gpu_register(0x123) == 0xFEDCBA98U,
              "Direct PPC MMIO writes update the command processor register state");
        // PM4_REG_RMW updates a real GPU register without discarding other bits.
        put(4, 0xC0000000U | (2U << 16) | (0x21U << 8));
        put(5, 0x123U);
        put(6, 0x00FF00FFU);
        put(7, 0xA000B000U);
        check(dotm::vd_bound_execute_ring(8) == 1 &&
              dotm::vd_bound_read_gpu_register(0x123) ==
                  ((0xFEDCBA98U & 0x00FF00FFU) | 0xA000B000U) &&
              dotm::vd_bound_read_gpu_mmio_register(0x123) ==
                  ((0xFEDCBA98U & 0x00FF00FFU) | 0xA000B000U) &&
              dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 8 &&
              memory.address(writeback)[3] == 8,
              "PM4 REG_RMW applies AND/OR masks and advances GPU ring");
        put(8, 0xC0000000U | (1U << 16) | (0x21U << 8));
        put(9, 0x123U);
        put(10, 0U);
        expect_throw([&] { dotm::vd_bound_execute_ring(11); },
                     "Malformed REG_RMW must not be accepted");
        check(dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 8,
              "Malformed REG_RMW leaves GPU read pointer unchanged");
        // The physical GPU ring must receive the same 19-word ME_INIT packet.
        for (std::uint32_t i = 0; i < 19; ++i)
            put(8 + i, me_word(i));
        check(dotm::vd_bound_execute_ring(27) == 1 &&
              dotm::vd_bound_gpu_snapshot().me_init_packet_submitted &&
              dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 27 &&
              memory.address(writeback)[3] == 27,
              "Verified original ME_INIT packet submitted through physical GPU ring");
        for (std::uint32_t i = 0; i < 19; ++i)
            put(27 + i, me_word(i));
        put(28, 0xDEADBEEFU);
        expect_throw([&] { dotm::vd_bound_execute_ring(46); },
                     "Altered ME_INIT packet must fail before GPU read pointer advances");
        check(dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 27,
              "Unverified ME_INIT DMA packet remains pending");
        // Real Xenos tiled rendering uses 64-bit bin masks and selections.
        // The high and low DWORDs of each PM4 packet must not be swapped.
        const auto register_before_bins = dotm::vd_bound_read_gpu_register(0x123U);
        put(27, 0xC0015000U); put(28, 1U); put(29, 2U); // SET_BIN_MASK
        put(30, 0xC0015100U); put(31, 1U); put(32, 0U); // SET_BIN_SELECT
        put(33, 0xC0022101U); // Predicated REG_RMW: selected high bin matches.
        put(34, 0x123U); put(35, 0xFFFFFFFFU); put(36, 0x10U);
        check(dotm::vd_bound_execute_ring(37) == 3 &&
              dotm::vd_bound_gpu_snapshot().bin_mask == 0x100000002ULL &&
              dotm::vd_bound_gpu_snapshot().bin_select == 0x100000000ULL &&
              dotm::vd_bound_read_gpu_register(0x123U) ==
                  (register_before_bins | 0x10U),
              "Xenos 64-bit bin selection executes matching predicated PM4 writes");
        const auto register_after_match = dotm::vd_bound_read_gpu_register(0x123U);
        put(37, 0xC0006000U); put(38, 0U); // SET_BIN_MASK_LO
        put(39, 0xC0006100U); put(40, 0U); // SET_BIN_MASK_HI
        put(41, 0xC0022101U); // Predicated REG_RMW must not write.
        put(42, 0x123U); put(43, 0U); put(44, 0xFFFFFFFFU);
        put(45, 0xC0002201U); put(46, 0x1234U); // Masked-out draw.
        check(dotm::vd_bound_execute_ring(47) == 4 &&
              dotm::vd_bound_gpu_snapshot().bin_mask == 0 &&
              dotm::vd_bound_read_gpu_register(0x123U) == register_after_match &&
              dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 47 &&
              memory.address(writeback)[3] == 47,
              "Masked-out PM4 writes and draws consume packets without side effects");
        put(47, 0xC0006100U); put(48, 1U); // SET_BIN_MASK_HI
        put(49, 0xC0006200U); put(50, 0U); // SET_BIN_SELECT_LO
        put(51, 0xC0006300U); put(52, 1U); // SET_BIN_SELECT_HI
        put(53, 0xC0022101U);
        put(54, 0x123U); put(55, 0xFFFFFFFFU); put(56, 0x20U);
        check(dotm::vd_bound_execute_ring(57) == 4 &&
              dotm::vd_bound_gpu_snapshot().bin_mask == 0x100000000ULL &&
              dotm::vd_bound_gpu_snapshot().bin_select == 0x100000000ULL &&
              dotm::vd_bound_read_gpu_register(0x123U) ==
                  (register_after_match | 0x20U),
              "Per-half PM4 bin updates restore high-bin predicated execution");
        // REG_RMW can read its AND and OR operands from GPU registers.
        // Set them through the guest MMIO aperture, as the original PPC
        // driver does, and verify all three register-source combinations.
        auto* and_source = memory.address(0x7FC80000U + 0x200U * 4U);
        auto* or_source = memory.address(0x7FC80000U + 0x201U * 4U);
        for (unsigned i = 0; i < 4; ++i) {
            and_source[i] = 0x0FU;
            or_source[i] = 0x10U;
        }
        const auto rmw_before = dotm::vd_bound_read_gpu_register(0x123U);
        put(57, 0xC0022100U); put(58, 0xC0000123U);
        put(59, 0xFFFF0200U); put(60, 0xFFFF0201U);
        const auto both_expected = (rmw_before & 0x0F0F0F0FU) | 0x10101010U;
        check(dotm::vd_bound_execute_ring(61) == 1 &&
              dotm::vd_bound_read_gpu_register(0x123U) == both_expected &&
              dotm::vd_bound_read_gpu_mmio_register(0x123U) == both_expected &&
              dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 61,
              "PM4 REG_RMW resolves both register operands and masks their indices");
        put(61, 0xC0022100U); put(62, 0x80000123U);
        put(63, 0x200U); put(64, 0U);
        const auto and_expected = both_expected & 0x0F0F0F0FU;
        check(dotm::vd_bound_execute_ring(65) == 1 &&
              dotm::vd_bound_read_gpu_register(0x123U) == and_expected,
              "PM4 REG_RMW supports register AND with immediate OR");
        put(65, 0xC0022100U); put(66, 0x40000123U);
        put(67, 0U); put(68, 0x201U);
        check(dotm::vd_bound_execute_ring(69) == 1 &&
              dotm::vd_bound_read_gpu_register(0x123U) == 0x10101010U &&
              dotm::vd_bound_read_gpu_register(0x200U) == 0x0F0F0F0FU &&
              dotm::vd_bound_read_gpu_register(0x201U) == 0x10101010U,
              "PM4 REG_RMW supports immediate AND with register OR");
        put(69, 0xC0005000U); put(70, 0U);
        expect_throw([&] { dotm::vd_bound_execute_ring(71); },
                     "Malformed Xenos SET_BIN_MASK packet rejected");
        check(dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 69,
              "Malformed bin packet cannot advance GPU read pointer");
        put(69, 0xC0002200U); put(70, 0x1234U);
        expect_throw([&] { dotm::vd_bound_execute_ring(71); },
                     "Unmasked original draw still requires a real rendering backend");
        check(dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 69,
              "Unimplemented unmasked draw remains pending");
        // WAIT_REG_MEM reads live CPU MMIO registers and stalls the ring
        // without losing the packet or writing a speculative read pointer.
        auto* poll_register = memory.address(0x7FC80000U + 0x202U * 4U);
        poll_register[0] = 0x12; poll_register[1] = 0x34;
        poll_register[2] = 0x56; poll_register[3] = 0x78;
        put(69, 0xC0043C00U); // WAIT_REG_MEM, five payload DWORDs
        put(70, 3U); put(71, 0x202U);
        put(72, 0x12340000U); put(73, 0xFFFF0000U); put(74, 0U);
        check(dotm::vd_bound_execute_ring(75) == 1 &&
              dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 75 &&
              memory.address(writeback)[3] == 75,
              "WAIT_REG_MEM matches masked guest MMIO register value");
        put(75, 0xC0043C00U);
        put(76, 3U); put(77, 0x202U);
        put(78, 0xDEADBEEFU); put(79, 0xFFFFFFFFU); put(80, 0x100U);
        check(dotm::vd_bound_execute_ring(81) == 0 &&
              dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 75 &&
              memory.address(writeback)[3] == 75,
              "Unmet WAIT_REG_MEM preserves packet and DMA read-pointer writeback");
        poll_register[0] = 0xDE; poll_register[1] = 0xAD;
        poll_register[2] = 0xBE; poll_register[3] = 0xEF;
        check(dotm::vd_bound_execute_ring(81) == 1 &&
              dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 81,
              "WAIT_REG_MEM resumes when original PPC updates the polled register");
        // The memory form resolves a genuine allocated GPU physical page,
        // with the low address bits selecting Xenos 8-in-32 endian swapping.
        auto* physical_poll = memory.address(writeback + 4);
        physical_poll[0] = 0xA1; physical_poll[1] = 0xB2;
        physical_poll[2] = 0xC3; physical_poll[3] = 0xD4;
        put(81, 0xC0043C00U);
        put(82, 0x13U); put(83, (writeback_physical + 4U) | 2U);
        put(84, 0xA1B2C3D4U); put(85, 0xFFFFFFFFU); put(86, 0U);
        check(dotm::vd_bound_execute_ring(87) == 1 &&
              dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 87 &&
              memory.address(writeback)[3] == 87,
              "WAIT_REG_MEM physical-memory mode uses GPU endian conversion");
        put(87, 0xC0033C00U);
        put(88, 3U); put(89, 0x202U); put(90, 0U); put(91, 0U);
        expect_throw([&] { dotm::vd_bound_execute_ring(92); },
                     "Malformed WAIT_REG_MEM packet rejected before consumption");
        check(dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 87,
              "Malformed WAIT_REG_MEM preserves the ring read pointer");
        put(87, 0xC0043C00U);
        put(88, 0x13U); put(89, 0xFFFFFFF2U);
        put(90, 0U); put(91, 0xFFFFFFFFU); put(92, 0U);
        expect_throw([&] { dotm::vd_bound_execute_ring(93); },
                     "WAIT_REG_MEM rejects unallocated physical poll addresses");
        check(dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 87,
              "Invalid physical WAIT_REG_MEM leaves DMA state intact");
        // GPU-to-CPU transfers must use allocated physical pages and the
        // live MMIO register file, with the Xenos destination endian mode.
        put(87, 0xC0013E00U); put(88, 0x202U);
        put(89, (writeback_physical + 32U) | 2U);
        check(dotm::vd_bound_execute_ring(90) == 1 &&
              dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 90 &&
              memory.address(writeback + 32U)[0] == 0xDE &&
              memory.address(writeback + 32U)[1] == 0xAD &&
              memory.address(writeback + 32U)[2] == 0xBE &&
              memory.address(writeback + 32U)[3] == 0xEF,
              "REG_TO_MEM writes live Xenos register with 8-in-32 endian mode");
        put(90, 0xC0013E00U); put(91, 0x202U);
        put(92, (writeback_physical + 36U) | 0U);
        check(dotm::vd_bound_execute_ring(93) == 1 &&
              memory.address(writeback + 36U)[0] == 0xEF &&
              memory.address(writeback + 36U)[1] == 0xBE &&
              memory.address(writeback + 36U)[2] == 0xAD &&
              memory.address(writeback + 36U)[3] == 0xDE,
              "REG_TO_MEM supports unswapped little-endian GPU writes");
        put(93, 0xC0023D00U);
        put(94, (writeback_physical + 40U) | 1U);
        put(95, 0x11223344U); put(96, 0x55667788U);
        check(dotm::vd_bound_execute_ring(97) == 1 &&
              memory.address(writeback + 40U)[0] == 0x33 &&
              memory.address(writeback + 40U)[1] == 0x44 &&
              memory.address(writeback + 40U)[2] == 0x11 &&
              memory.address(writeback + 40U)[3] == 0x22 &&
              memory.address(writeback + 44U)[0] == 0x77 &&
              memory.address(writeback + 44U)[1] == 0x88 &&
              memory.address(writeback + 44U)[2] == 0x55 &&
              memory.address(writeback + 44U)[3] == 0x66,
              "MEM_WRITE writes consecutive physical DWORDs in 8-in-16 mode");
        put(97, 0xC0013D00U);
        put(98, (writeback_physical + 48U) | 3U);
        put(99, 0xA1B2C3D4U);
        check(dotm::vd_bound_execute_ring(100) == 1 &&
              memory.address(writeback + 48U)[0] == 0xB2 &&
              memory.address(writeback + 48U)[1] == 0xA1 &&
              memory.address(writeback + 48U)[2] == 0xD4 &&
              memory.address(writeback + 48U)[3] == 0xC3,
              "MEM_WRITE supports 16-in-32 endian mode");
        // The ring is the highest physical allocation, so its last DWORD
        // is followed by the 512 MiB physical-space boundary. The
        // writeback page sits directly below the ring and must not be
        // treated as an unallocated neighbor.
        memory.address(ring + 4092U)[0] = 0x5A;
        put(100, 0xC0023D00U);
        put(101, (physical + 4092U) | 2U);
        put(102, 0xFFFFFFFFU); put(103, 0x01020304U);
        expect_throw([&] { dotm::vd_bound_execute_ring(104); },
                     "MEM_WRITE rejects DMA crossing into unallocated memory");
        check(dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 100 &&
              memory.address(ring + 4092U)[0] == 0x5A,
              "Invalid MEM_WRITE cannot partially modify allocated memory");
        put(100, 0xC0013E00U);
        put(101, 0x202U); put(102, 0xFFFFFFF2U);
        expect_throw([&] { dotm::vd_bound_execute_ring(103); },
                     "REG_TO_MEM rejects an unallocated physical destination");
        check(dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 100,
              "Invalid REG_TO_MEM preserves DMA ring read pointer");
        put(100, 0xC0013D00U);
        put(101, (writeback_physical + 52U) | 2U);
        put(102, 0x12345678U);
        check(dotm::vd_bound_execute_ring(103) == 1 &&
              memory.address(writeback + 52U)[0] == 0x12 &&
              memory.address(writeback + 52U)[1] == 0x34 &&
              memory.address(writeback + 52U)[2] == 0x56 &&
              memory.address(writeback + 52U)[3] == 0x78 &&
              dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 103,
              "MEM_WRITE 8-in-32 endian mode commits a validated GPU write");
        // A legal physical DMA can overlap the packet being executed.
        // Both payload values must come from the original packet, even if
        // the first destination overwrites the second source DWORD.
        put(103, 0xC0023D00U);
        put(104, (physical + 106U * 4U) | 2U);
        put(105, 0xAABBCCDDU);
        put(106, 0x11223344U);
        check(dotm::vd_bound_execute_ring(107) == 1 &&
              dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 107 &&
              memory.address(ring + 106U * 4U)[0] == 0xAA &&
              memory.address(ring + 106U * 4U)[1] == 0xBB &&
              memory.address(ring + 106U * 4U)[2] == 0xCC &&
              memory.address(ring + 106U * 4U)[3] == 0xDD &&
              memory.address(ring + 107U * 4U)[0] == 0x11 &&
              memory.address(ring + 107U * 4U)[1] == 0x22 &&
              memory.address(ring + 107U * 4U)[2] == 0x33 &&
              memory.address(ring + 107U * 4U)[3] == 0x44,
              "MEM_WRITE snapshots payload before DMA overwrites its own ring");
        // LOAD_ALU_CONSTANT reads actual allocated physical shader constants.
        // The GPU sees their big-endian values and updates the MMIO-backed
        // ALU constant bank without inventing a shader or rendering output.
        auto* constants = memory.address(writeback + 64U);
        constants[0] = 0x12; constants[1] = 0x34;
        constants[2] = 0x56; constants[3] = 0x78;
        constants[4] = 0xA1; constants[5] = 0xB2;
        constants[6] = 0xC3; constants[7] = 0xD4;
        put(107, 0xC0022F00U);
        put(108, writeback_physical + 64U);
        put(109, 0x10U); put(110, 2U);
        check(dotm::vd_bound_execute_ring(111) == 1 &&
              dotm::vd_bound_read_gpu_register(0x4010U) == 0x12345678U &&
              dotm::vd_bound_read_gpu_register(0x4011U) == 0xA1B2C3D4U &&
              dotm::vd_bound_read_gpu_mmio_register(0x4010U) == 0x12345678U &&
              dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 111 &&
              memory.address(writeback)[3] == 111,
              "LOAD_ALU_CONSTANT copies allocated physical BE shader constants");
        put(111, 0xC0022F00U);
        put(112, writeback_physical + 64U);
        put(113, (4U << 16) | 0x30U); put(114, 2U);
        check(dotm::vd_bound_execute_ring(115) == 1 &&
              dotm::vd_bound_read_gpu_register(0x2030U) == 0x12345678U &&
              dotm::vd_bound_read_gpu_register(0x2031U) == 0xA1B2C3D4U,
              "LOAD_ALU_CONSTANT supports original Xenos general-register bank");
        put(115, 0xC0022F00U);
        put(116, 0x3FFFFFFCU);
        put(117, 0x10U); put(118, 2U);
        expect_throw([&] { dotm::vd_bound_execute_ring(119); },
                     "LOAD_ALU_CONSTANT rejects an unallocated physical source");
        check(dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 115 &&
              memory.address(writeback)[3] == 115,
              "Failed LOAD_ALU_CONSTANT preserves DMA ring and writeback");
        put(115, 0xC0022F00U);
        put(116, writeback_physical + 64U);
        put(117, (3U << 16) | 0x7FFU); put(118, 0xFFFU);
        expect_throw([&] { dotm::vd_bound_execute_ring(119); },
                     "LOAD_ALU_CONSTANT rejects register bank overflow");
        check(dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 115,
              "Register overflow cannot partially write shader constants");
        put(115, 0xC0022F00U);
        put(116, 0x3FFFFFFCU); // No source memory is read for zero words.
        put(117, 0x10U); put(118, 0U);
        check(dotm::vd_bound_execute_ring(119) == 1 &&
              dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 119 &&
              dotm::vd_bound_read_gpu_register(0x4010U) == 0x12345678U,
              "Zero-length LOAD_ALU_CONSTANT advances without fake physical memory");
        put(119, 0xC0022F00U);
        put(120, writeback_physical + 64U);
        put(121, (5U << 16) | 0x10U); put(122, 1U);
        expect_throw([&] { dotm::vd_bound_execute_ring(123); },
                     "Unknown Xenos shader constant bank is rejected");
        check(dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 119,
              "Unknown shader bank cannot advance GPU ring");
        // COND_WRITE compares live Xenos MMIO or allocated physical data
        // and commits only the matching register/physical-memory transfer.
        put(119, 0xC0054500U); put(120, 3U);
        put(121, 0x202U); put(122, 0xDEAD0000U);
        put(123, 0xFFFF0000U); put(124, 0x220U);
        put(125, 0xA1B2C3D4U);
        check(dotm::vd_bound_execute_ring(126) == 1 &&
              dotm::vd_bound_read_gpu_register(0x220U) == 0xA1B2C3D4U &&
              dotm::vd_bound_read_gpu_mmio_register(0x220U) == 0xA1B2C3D4U &&
              dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 126,
              "COND_WRITE matches masked live MMIO and writes a GPU register");
        put(126, 0xC0054500U); put(127, 3U);
        put(128, 0x202U); put(129, 0xBAD00000U);
        put(130, 0xFFFF0000U); put(131, 0x220U);
        put(132, 0U);
        check(dotm::vd_bound_execute_ring(133) == 1 &&
              dotm::vd_bound_read_gpu_register(0x220U) == 0xA1B2C3D4U &&
              dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 133,
              "False COND_WRITE consumes packet without modifying MMIO");
        put(133, 0xC0054500U); put(134, 0x113U);
        put(135, (writeback_physical + 64U) | 2U);
        put(136, 0x12345678U); put(137, 0xFFFFFFFFU);
        put(138, (writeback_physical + 72U) | 3U);
        put(139, 0xA1B2C3D4U);
        check(dotm::vd_bound_execute_ring(140) == 1 &&
              memory.address(writeback + 72U)[0] == 0xB2 &&
              memory.address(writeback + 72U)[1] == 0xA1 &&
              memory.address(writeback + 72U)[2] == 0xD4 &&
              memory.address(writeback + 72U)[3] == 0xC3 &&
              dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 140,
              "COND_WRITE compares physical memory and writes 16-in-32 DMA");
        put(140, 0xC0054500U); put(141, 0x107U);
        put(142, 0x202U); put(143, 0U); put(144, 0U);
        put(145, 0xFFFFFFF2U); put(146, 0x12345678U);
        expect_throw([&] { dotm::vd_bound_execute_ring(147); },
                     "Matching COND_WRITE rejects unallocated physical destination");
        check(dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 140 &&
              memory.address(writeback)[3] == 140,
              "Failed COND_WRITE preserves ring and read-pointer writeback");
        put(141, 0x100U); // Never: invalid destination must not be accessed.
        check(dotm::vd_bound_execute_ring(147) == 1 &&
              dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 147 &&
              dotm::vd_bound_read_gpu_register(0x220U) == 0xA1B2C3D4U,
              "False COND_WRITE skips an inaccessible physical destination");
        put(147, 0xC0054500U); put(148, 0x13U);
        put(149, 0xFFFFFFF2U); put(150, 0U);
        put(151, 0xFFFFFFFFU); put(152, 0x220U);
        put(153, 0xFFFFFFFFU);
        expect_throw([&] { dotm::vd_bound_execute_ring(154); },
                     "COND_WRITE rejects an unallocated physical poll address");
        check(dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 147,
              "Invalid COND_WRITE source cannot advance GPU ring");
        // A complete packet before a partial one must still commit its
        // progress; the unfinished command and its writeback stay pending.
        put(147, 0x80000000U); // PM4 type-2, no payload.
        put(148, (1U << 16) | 0x180U); // Two register values required.
        put(149, 0x11223344U); // Producer has only written the first.
        check(dotm::vd_bound_execute_ring(150) == 1 &&
              dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 148 &&
              memory.address(writeback)[3] == 148 &&
              dotm::vd_bound_read_gpu_register(0x180U) == 0U,
              "GPU executes complete prefix and defers the incomplete next packet");
        put(150, 0x55667788U);
        check(dotm::vd_bound_execute_ring(151) == 1 &&
              dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 151 &&
              memory.address(writeback)[3] == 151 &&
              dotm::vd_bound_read_gpu_register(0x180U) == 0x11223344U &&
              dotm::vd_bound_read_gpu_register(0x181U) == 0x55667788U,
              "GPU resumes partial PM4 packet when the missing payload arrives");
        // Xenos SCRATCH_REG0..7 write to real GPU physical memory when
        // their SCRATCH_UMSK bits are enabled. The CPU reads these fences.
        put(151, (1U << 16) | 0x01DCU); // UMSK and ADDR.
        put(152, 0x3U);
        put(153, writeback_physical + 0x40U);
        put(154, (1U << 16) | 0x0578U); // SCRATCH_REG0 and REG1.
        put(155, 0x11223344U);
        put(156, 0x55667788U);
        check(dotm::vd_bound_execute_ring(157) == 2 &&
              memory.address(writeback)[0x40] == 0x11 &&
              memory.address(writeback)[0x41] == 0x22 &&
              memory.address(writeback)[0x42] == 0x33 &&
              memory.address(writeback)[0x43] == 0x44 &&
              memory.address(writeback)[0x44] == 0x55 &&
              memory.address(writeback)[0x47] == 0x88,
              "Enabled Xenos scratch registers write big-endian physical GPU fences");
        put(157, 0x01DCU); // Disable scratch DMA.
        put(158, 0U);
        put(159, 0x0578U);
        put(160, 0xAABBCCDDU);
        check(dotm::vd_bound_execute_ring(161) == 2 &&
              dotm::vd_bound_read_gpu_register(0x0578U) == 0xAABBCCDDU &&
              memory.address(writeback)[0x40] == 0x11,
              "Disabled scratch writeback updates the GPU register only");
        put(161, (1U << 16) | 0x01DCU);
        put(162, 1U);
        put(163, 0x20000000U); // Outside the Xbox 360 512 MiB physical aperture.
        put(164, 0x0578U);
        put(165, 0xDEADBEEFU);
        expect_throw([&] { dotm::vd_bound_execute_ring(166); },
                     "Scratch DMA to unallocated physical memory is rejected");
        check(dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 164 &&
              dotm::vd_bound_read_gpu_register(0x0578U) == 0xAABBCCDDU &&
              memory.address(writeback)[0x40] == 0x11,
              "Failed scratch DMA preserves the pending packet and prior fence");
        put(164, 0x01DDU);
        put(165, writeback_physical + 0x40U);
        put(166, 0x0578U);
        put(167, 0xCAFEBABEU);
        check(dotm::vd_bound_execute_ring(168) == 2 &&
              dotm::vd_bound_read_gpu_register(0x0578U) == 0xCAFEBABEU &&
              memory.address(writeback)[0x40] == 0xCA &&
              memory.address(writeback)[0x43] == 0xBE,
              "Scratch DMA resumes after restoring a valid physical address");
        // The ring allocation is the highest physical page; its final DWORD
        // is valid but the following DWORD crosses the 512 MiB aperture.
        // The lower writeback page is adjacent to the ring and would make
        // a writeback+4092 boundary test incorrectly succeed. Both TYPE0
        // and TYPE1 must validate all destinations before committing.
        for (unsigned i = 0; i < 4; ++i)
            memory.address(ring)[4092 + i] =
                static_cast<std::uint8_t>(0xFAU + i);
        put(168, (1U << 16) | 0x01DCU);
        put(169, 3U);
        put(170, physical + 4092U);
        put(171, (1U << 16) | 0x0578U);
        put(172, 0x12345678U);
        put(173, 0x87654321U);
        expect_throw([&] { dotm::vd_bound_execute_ring(174); },
                     "TYPE0 scratch DMA validates every destination before committing");
        check(dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 171 &&
              dotm::vd_bound_read_gpu_register(0x0578U) == 0xCAFEBABEU &&
              dotm::vd_bound_read_gpu_register(0x0579U) == 0x55667788U &&
              memory.address(ring)[4092] == 0xFA &&
              memory.address(ring)[4095] == 0xFD &&
              memory.address(writeback)[3] == 171,
              "TYPE0 invalid second scratch DMA leaves first register and fence intact");
        put(171, 0x01DDU);
        put(172, writeback_physical + 0x40U);
        put(173, (1U << 16) | 0x0578U);
        put(174, 0x0A0B0C0DU);
        put(175, 0x01020304U);
        check(dotm::vd_bound_execute_ring(176) == 2 &&
              dotm::vd_bound_read_gpu_register(0x0578U) == 0x0A0B0C0DU &&
              dotm::vd_bound_read_gpu_register(0x0579U) == 0x01020304U,
              "TYPE0 scratch DMA resumes after fixing the physical destination");
        put(176, 0x01DDU);
        put(177, physical + 4092U);
        put(178, 0x40000000U | (0x0579U << 11) | 0x0578U);
        put(179, 0xFEEDBEEFU);
        put(180, 0x11224488U);
        expect_throw([&] { dotm::vd_bound_execute_ring(181); },
                     "TYPE1 scratch DMA validates both destinations before committing");
        check(dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 178 &&
              dotm::vd_bound_read_gpu_register(0x0578U) == 0x0A0B0C0DU &&
              dotm::vd_bound_read_gpu_register(0x0579U) == 0x01020304U &&
              memory.address(ring)[4092] == 0xFA &&
              memory.address(ring)[4095] == 0xFD &&
              memory.address(writeback)[3] == 178,
              "TYPE1 invalid second scratch DMA leaves first register and fence intact");
        put(178, 0x01DDU);
        put(179, writeback_physical + 0x40U);
        put(180, 0x40000000U | (0x0579U << 11) | 0x0578U);
        put(181, 0xF1E2D3C4U);
        put(182, 0xB1A2C3D4U);
        check(dotm::vd_bound_execute_ring(183) == 2 &&
              dotm::vd_bound_read_gpu_register(0x0578U) == 0xF1E2D3C4U &&
              dotm::vd_bound_read_gpu_register(0x0579U) == 0xB1A2C3D4U &&
              memory.address(writeback)[0x40] == 0xF1 &&
              memory.address(writeback)[0x44] == 0xB1,
              "TYPE1 scratch DMA resumes and writes both physical fences");
        // Xenos scratch DMA may alias a later word in the same command
        // packet. Fetching into a FIFO snapshot must preserve that word's
        // original value for the second register write.
        put(183, (1U << 16) | 0x01DCU);
        put(184, 1U);
        put(185, physical + 188U * 4U);
        put(186, (1U << 16) | 0x0578U);
        put(187, 0xAABBCCDDU);
        put(188, 0x11223344U);
        check(dotm::vd_bound_execute_ring(189) == 2 &&
              dotm::vd_bound_read_gpu_register(0x0578U) == 0xAABBCCDDU &&
              dotm::vd_bound_read_gpu_register(0x0579U) == 0x11223344U &&
              memory.address(ring)[188U * 4U] == 0xAA &&
              memory.address(ring)[188U * 4U + 3U] == 0xDD,
              "TYPE0 scratch DMA ring alias cannot mutate the fetched packet payload");
        // SET_CONSTANT (PM4 0x55) can write the same scratch registers as
        // type-0/type-1 packets. The second DMA is outside the allocated
        // ring: the first register and physical fence must remain unchanged.
        put(189, (1U << 16) | 0x01DCU);
        put(190, 3U);
        put(191, physical + 4092U);
        check(dotm::vd_bound_execute_ring(192) == 1,
              "Prepare both Xenos scratch writeback lanes");
        put(192, 0xC0025500U);
        put(193, 0x0578U);
        put(194, 0xCAFED00DU);
        put(195, 0xBADC0FFEU);
        expect_throw([&] { dotm::vd_bound_execute_ring(196); },
                     "SET_CONSTANT preflights all scratch DMA destinations");
        check(dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 192 &&
              dotm::vd_bound_read_gpu_register(0x0578U) == 0xAABBCCDDU &&
              dotm::vd_bound_read_gpu_register(0x0579U) == 0x11223344U &&
              memory.address(ring)[4092] == 0xFA &&
              memory.address(ring)[4095] == 0xFD &&
              memory.address(writeback)[3] == 192,
              "Rejected SET_CONSTANT preserves registers, fence and ring pointer");
        put(192, 0x01DDU);
        put(193, writeback_physical + 0x40U);
        put(194, 0xC0025500U);
        put(195, 0x0578U);
        put(196, 0xCAFED00DU);
        put(197, 0xBADC0FFEU);
        check(dotm::vd_bound_execute_ring(198) == 2 &&
              dotm::vd_bound_read_gpu_register(0x0578U) == 0xCAFED00DU &&
              dotm::vd_bound_read_gpu_register(0x0579U) == 0xBADC0FFEU &&
              memory.address(writeback)[0x40] == 0xCA &&
              memory.address(writeback)[0x44] == 0xBA,
              "SET_CONSTANT commits both valid physical scratch fences");
        // The first scratch DMA overwrites the next packet payload dword.
        // Both register values must still be those fetched before execution.
        put(198, (1U << 16) | 0x01DCU);
        put(199, 1U);
        put(200, physical + 204U * 4U);
        put(201, 0xC0025500U);
        put(202, 0x0578U);
        put(203, 0x11223344U);
        put(204, 0x55667788U);
        check(dotm::vd_bound_execute_ring(205) == 2 &&
              dotm::vd_bound_read_gpu_register(0x0578U) == 0x11223344U &&
              dotm::vd_bound_read_gpu_register(0x0579U) == 0x55667788U &&
              memory.address(ring)[204U * 4U] == 0x11 &&
              memory.address(ring)[204U * 4U + 3U] == 0x44,
              "SET_CONSTANT snapshots packet payload before scratch DMA alias");
        // Real Xenos PM4 EVENT_WRITE_SHD (opcode 0x58) writes a shader
        // completion fence to GPU physical memory. The low address bits
        // select the 32-bit endian mode, not additional address bits.
        put(205, 0xC0025800U);
        put(206, 3U); // PS_DONE_TS
        put(207, (writeback_physical + 0x100U) | 2U);
        put(208, 0xA1B2C3D4U);
        check(dotm::vd_bound_execute_ring(209) == 1 &&
              memory.address(writeback)[0x100] == 0xA1 &&
              memory.address(writeback)[0x103] == 0xD4 &&
              dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 209 &&
              memory.address(writeback)[3] == 209 &&
              dotm::vd_bound_read_gpu_register(0x21F9U) == 3U &&
              dotm::vd_bound_read_gpu_mmio_register(0x21F9U) == 3U,
              "EVENT_WRITE_SHD commits both physical fence and VGT event register");
        put(209, 0xC0025800U);
        put(210, 4U); // CACHE_FLUSH_TS requires different GPU semantics
        put(211, (writeback_physical + 0x104U) | 2U);
        put(212, 0x11223344U);
        expect_throw([&] { dotm::vd_bound_execute_ring(213); },
                     "Unsupported shader-done event is not silently acknowledged");
        check(dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 209 &&
              memory.address(writeback)[0x104] == 0 &&
              memory.address(writeback)[3] == 209 &&
              dotm::vd_bound_read_gpu_register(0x21F9U) == 3U,
              "Rejected shader event leaves the physical fence, VGT event and RPTR unchanged");
        put(210, 2U); // VS_DONE_TS
        put(211, 0xFFFFFFFEU);
        expect_throw([&] { dotm::vd_bound_execute_ring(213); },
                     "EVENT_WRITE_SHD refuses unallocated physical fence pages");
        check(dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 209 &&
              memory.address(writeback)[3] == 209 &&
              dotm::vd_bound_read_gpu_register(0x21F9U) == 3U,
              "Invalid physical event write preserves pending packet and event register");
        put(211, (writeback_physical + 0x104U) | 2U);
        check(dotm::vd_bound_execute_ring(213) == 1 &&
              memory.address(writeback)[0x104] == 0x11 &&
              memory.address(writeback)[0x107] == 0x44 &&
              memory.address(writeback)[3] == 213 &&
              dotm::vd_bound_read_gpu_register(0x21F9U) == 2U,
              "EVENT_WRITE_SHD VS_DONE_TS retries and publishes the event");
        // Initiator bit 31 selects the GPU frame counter, not the packet's
        // immediate value. No frames have been presented by this test.
        put(213, 0xC0025800U);
        put(214, 0x80000003U);
        put(215, (writeback_physical + 0x108U) | 2U);
        put(216, 0xFFFFFFFFU);
        check(dotm::vd_bound_execute_ring(217) == 1 &&
              memory.address(writeback)[0x108] == 0 &&
              memory.address(writeback)[0x10B] == 0 &&
              dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 217,
              "EVENT_WRITE_SHD counter mode never invents presented GPU frames");
        // PM4_EVENT_WRITE with a single initiator has no DMA destination.
        // Its low six bits are visible in VGT_EVENT_INITIATOR (0x21F9).
        put(217, 0xC0004600U);
        put(218, 0x80000006U); // CACHE_FLUSH event, no fake GPU frame.
        check(dotm::vd_bound_execute_ring(219) == 1 &&
              dotm::vd_bound_read_gpu_register(0x21F9U) == 6U &&
              dotm::vd_bound_read_gpu_mmio_register(0x21F9U) == 6U &&
              dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 219 &&
              memory.address(writeback)[3] == 219,
              "EVENT_WRITE publishes the real VGT initiator through guest MMIO");
        put(219, 0xC0014600U); // Unsupported two-DWORD memory variant.
        put(220, 2U);
        put(221, (writeback_physical + 0x10CU) | 2U);
        expect_throw([&] { dotm::vd_bound_execute_ring(222); },
                     "EVENT_WRITE memory variant must not fake a GPU completion");
        check(dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 219 &&
              dotm::vd_bound_read_gpu_register(0x21F9U) == 6U &&
              memory.address(writeback)[3] == 219 &&
              memory.address(writeback)[0x10C] == 0,
              "Unsupported EVENT_WRITE preserves event, fence and ring state");
        // Xenos shader uploads preserve actual BE instruction words.
        auto* shader_bytes = memory.address(writeback + 0x120U);
        constexpr std::uint32_t vertex_words[] = {
            0x01234567U, 0x89ABCDEFU, 0xFEDCBA98U
        };
        for (std::uint32_t i = 0; i < 3U; ++i) {
            shader_bytes[i * 4U] = static_cast<std::uint8_t>(vertex_words[i] >> 24);
            shader_bytes[i * 4U + 1U] = static_cast<std::uint8_t>(vertex_words[i] >> 16);
            shader_bytes[i * 4U + 2U] = static_cast<std::uint8_t>(vertex_words[i] >> 8);
            shader_bytes[i * 4U + 3U] = static_cast<std::uint8_t>(vertex_words[i]);
        }
        put(219, 0xC0012700U); // PM4_IM_LOAD, two payload words.
        put(220, writeback_physical + 0x120U); // Vertex shader.
        put(221, 3U);
        check(dotm::vd_bound_execute_ring(222) == 1 &&
              dotm::vd_bound_copy_shader_microcode(0U) ==
                  std::vector<std::uint32_t>(vertex_words, vertex_words + 3) &&
              dotm::vd_bound_gpu_snapshot().vertex_shader_dwords == 3U &&
              dotm::vd_bound_gpu_snapshot().vertex_shader_fingerprint != 0U,
              "IM_LOAD preserves allocated physical vertex shader microcode");
        put(222, 0xC0042B00U); // IM_LOAD_IMMEDIATE, five payload words.
        put(223, 1U); // Pixel shader.
        put(224, 3U);
        put(225, 0xAABBCCDDU);
        put(226, 0x12345678U);
        put(227, 0x11223344U);
        check(dotm::vd_bound_execute_ring(228) == 1 &&
              dotm::vd_bound_copy_shader_microcode(1U) ==
                  std::vector<std::uint32_t>({0xAABBCCDDU, 0x12345678U, 0x11223344U}) &&
              dotm::vd_bound_gpu_snapshot().shader_uploads == 2U &&
              dotm::vd_bound_gpu_snapshot().pixel_shader_dwords == 3U,
              "IM_LOAD_IMMEDIATE captures original pixel shader words");
        const auto previous_shader = dotm::vd_bound_copy_shader_microcode(0U);
        put(228, 0xC0012700U);
        put(229, 0x3FFFF000U); // Outside the 512 MiB physical aperture.
        put(230, 3U);
        expect_throw([&] { dotm::vd_bound_execute_ring(231); },
                     "IM_LOAD rejects unallocated shader DMA source");
        check(dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 228U &&
              dotm::vd_bound_copy_shader_microcode(0U) == previous_shader &&
              dotm::vd_bound_gpu_snapshot().shader_uploads == 2U,
              "Rejected shader DMA preserves shader and ring state");
        put(228, 0xC0022B00U);
        put(229, 1U);
        put(230, 3U);
        put(231, 0x11111111U); // Declared 3 words, packet contains 1.
        expect_throw([&] { dotm::vd_bound_execute_ring(232); },
                     "IM_LOAD_IMMEDIATE rejects inconsistent shader length");
        check(dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 228U &&
              dotm::vd_bound_gpu_snapshot().shader_uploads == 2U,
              "Malformed shader upload does not consume command ring");
        // IM_LOAD start_size high bits address instructions within the
        // existing shader. Preserve the prefix and replace only the
        // requested instruction range, including the final instruction.
        auto* shader_patch = memory.address(writeback + 0x130U);
        const std::uint8_t vertex_patch_bytes[] = {
            0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE
        };
        for (std::uint32_t i = 0; i < 8U; ++i)
            shader_patch[i] = vertex_patch_bytes[i];
        put(228, 0xC0012700U); // Physical IM_LOAD.
        put(229, writeback_physical + 0x130U);
        put(230, (1U << 16) | 2U); // Replace vertex instructions 1..2.
        check(dotm::vd_bound_execute_ring(231) == 1 &&
              dotm::vd_bound_copy_shader_microcode(0U) ==
                  std::vector<std::uint32_t>({0x01234567U, 0xDEADBEEFU, 0xCAFEBABEU}) &&
              dotm::vd_bound_gpu_snapshot().vertex_shader_dwords == 3U &&
              dotm::vd_bound_gpu_snapshot().shader_uploads == 3U,
              "IM_LOAD patches existing vertex microcode without losing prefix");
        const auto vertex_fingerprint = dotm::vd_bound_gpu_snapshot().vertex_shader_fingerprint;
        put(231, 0xC0032B00U); // Immediate upload: 4 payload words.
        put(232, 1U);
        put(233, (2U << 16) | 2U); // Replace pixel word 2, append word 3.
        put(234, 0x13579BDFU);
        put(235, 0x2468ACE0U);
        check(dotm::vd_bound_execute_ring(236) == 1 &&
              dotm::vd_bound_copy_shader_microcode(1U) ==
                  std::vector<std::uint32_t>({0xAABBCCDDU, 0x12345678U,
                                              0x13579BDFU, 0x2468ACE0U}) &&
              dotm::vd_bound_gpu_snapshot().pixel_shader_dwords == 4U &&
              dotm::vd_bound_gpu_snapshot().shader_uploads == 4U,
              "IM_LOAD_IMMEDIATE patches and extends pixel shader instruction image");
        put(236, 0xC0012700U);
        put(237, writeback_physical + 0x130U);
        put(238, (5U << 16) | 1U); // Vertex shader has only 3 words.
        expect_throw([&] { dotm::vd_bound_execute_ring(239); },
                     "IM_LOAD rejects a gap of uninitialized shader instructions");
        check(dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 236U &&
              dotm::vd_bound_gpu_snapshot().shader_uploads == 4U &&
              dotm::vd_bound_gpu_snapshot().vertex_shader_fingerprint == vertex_fingerprint &&
              dotm::vd_bound_copy_shader_microcode(0U) ==
                  std::vector<std::uint32_t>({0x01234567U, 0xDEADBEEFU, 0xCAFEBABEU}),
              "Rejected partial shader upload preserves ring, hash and microcode");
        expect_throw([&] { (void)dotm::vd_bound_copy_shader_microcode(2U); },
                     "Unknown shader stage rejected");
        // Original Xenos driver writes COHER_STATUS_HOST to invalidate
        // vertex/texture caches and then polls the same register for zero.
        // The native physical backing is shared, so this is a real host
        // memory ordering barrier, not a fabricated GPU frame or draw.
        put(236, 0x00000A31U); // Type-0, one COHER_STATUS_HOST write.
        put(237, 0x01000000U); // VC action, status bit set by hardware.
        check(dotm::vd_bound_execute_ring(238) == 1 &&
              dotm::vd_bound_read_gpu_register(0x0A31U) == 0x81000000U &&
              dotm::vd_bound_read_gpu_mmio_register(0x0A31U) == 0x81000000U,
              "Xenos coherency request sets the pending hardware status bit");
        put(238, 0xC0043C00U); // WAIT_REG_MEM, five payload dwords.
        put(239, 3U); put(240, 0x0A31U);
        put(241, 0U); put(242, 0xFFFFFFFFU); put(243, 0U);
        check(dotm::vd_bound_execute_ring(244) == 1 &&
              dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 244U &&
              dotm::vd_bound_read_gpu_register(0x0A31U) == 0U &&
              dotm::vd_bound_read_gpu_mmio_register(0x0A31U) == 0U,
              "WAIT_REG_MEM completes a shared-memory Xenos coherency barrier");
        // Original PPC also writes GPU MMIO directly, bypassing the native
        // PM4 register-write hook. A raw CPU coherency request does not have
        // the synthetic pending bit, but still must complete when polled.
        constexpr std::uint32_t coher_mmio = 0x7FC80000U + 0x0A31U * 4U;
        auto* coher_bytes = memory.address(coher_mmio);
        coher_bytes[0] = 0x02U;
        coher_bytes[1] = 0U;
        coher_bytes[2] = 0U;
        coher_bytes[3] = 0U;
        check(dotm::vd_bound_read_gpu_register(0x0A31U) == 0x02000000U,
              "Original PPC direct MMIO coherency request reaches Xenos register");
        put(244, 0xC0043C00U); // WAIT_REG_MEM on CPU-originated request.
        put(245, 3U); put(246, 0x0A31U);
        put(247, 0U); put(248, 0xFFFFFFFFU); put(249, 0U);
        check(dotm::vd_bound_execute_ring(250) == 1 &&
              dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 250U &&
              dotm::vd_bound_read_gpu_register(0x0A31U) == 0U &&
              dotm::vd_bound_read_gpu_mmio_register(0x0A31U) == 0U &&
              memory.address(writeback)[3] == 250U,
              "WAIT_REG_MEM completes direct PPC MMIO coherency without stalling ring");
        // Original Xenos indirect command lists live in allocated physical memory.
        const auto ib = dotm::mm_bound_allocate_physical_memory_ex(
            2, 4096, 0x404, 0, 0xFFFFFFFFU, 4096);
        check(ib != 0, "Physical PM4 indirect buffer allocation");
        const auto ib_physical = dotm::mm_bound_get_physical_address(ib);
        auto put_ib = [&](std::uint32_t i, std::uint32_t value) {
            auto* p = memory.address(ib + 4U * i);
            p[0] = static_cast<std::uint8_t>(value >> 24U);
            p[1] = static_cast<std::uint8_t>(value >> 16U);
            p[2] = static_cast<std::uint8_t>(value >> 8U);
            p[3] = static_cast<std::uint8_t>(value);
        };
        put_ib(0, 0x00000204U); put_ib(1, 0x13579BDFU);
        put_ib(2, 0xC0043C00U); // WAIT_REG_MEM on register 0x205.
        put_ib(3, 3U); put_ib(4, 0x205U); put_ib(5, 0x1234U);
        put_ib(6, 0xFFFFFFFFU); put_ib(7, 0U);
        put_ib(8, 0x00000206U); put_ib(9, 0xCAFEBABEU);
        put_ib(10, 0x80000000U); // Type-2.
        auto* poll = memory.address(0x7FC80000U + 0x205U * 4U);
        poll[0] = 0; poll[1] = 0; poll[2] = 0; poll[3] = 0;
        put(250, 0xC0013F00U); put(251, ib_physical); put(252, 11U);
        check(dotm::vd_bound_execute_ring(253) == 1 &&
              dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 250U &&
              dotm::vd_bound_read_gpu_register(0x204U) == 0x13579BDFU &&
              memory.address(writeback)[3] == 250U,
              "Indirect PM4 prefix executes without acknowledging pending WAIT");
        poll[2] = 0x12U; poll[3] = 0x34U;
        check(dotm::vd_bound_execute_ring(253, 1U) == 1 &&
              dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 250U,
              "Nested WAIT_REG_MEM resumes from its saved offset");
        check(dotm::vd_bound_execute_ring(253) == 3 &&
              dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 253U &&
              dotm::vd_bound_read_gpu_register(0x206U) == 0xCAFEBABEU &&
              memory.address(writeback)[3] == 253U,
              "Nested completion commits parent ring and RPTR writeback");
        put(253, 0xC0013F00U); put(254, 0x100U); put(255, 1U);
        expect_throw([&] { (void)dotm::vd_bound_execute_ring(256); },
                     "Indirect DMA rejects unallocated physical memory");
        check(dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 253U,
              "Rejected indirect DMA preserves parent read pointer");
        put(254, ib_physical); put(255, 0x100001U);
        expect_throw([&] { (void)dotm::vd_bound_execute_ring(256); },
                     "Indirect DMA rejects reserved length bits");
        put(255, 1U); put_ib(0, 0x80000000U);
        check(dotm::vd_bound_execute_ring(256) == 2 &&
              dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 256U &&
              memory.address(writeback)[2] == 1U &&
              memory.address(writeback)[3] == 0U,
              "Indirect retry consumes nested TYPE2 and parent packet");
        // Two-level nested dispatch and PFD (0x37) share physical memory rules.
        put_ib(0, 0xC0013F00U); put_ib(1, ib_physical + 64U);
        put_ib(2, 2U); put_ib(16, 0x00000207U);
        put_ib(17, 0xFEEDC0DEU);
        put(256, 0xC0013F00U); put(257, ib_physical); put(258, 3U);
        check(dotm::vd_bound_execute_ring(259) == 3 &&
              dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 259U &&
              dotm::vd_bound_read_gpu_register(0x207U) == 0xFEEDC0DEU,
              "Two-level original PM4 indirect dispatch");
        put(259, 0xC0013700U); put(260, ib_physical + 64U); put(261, 2U);
        check(dotm::vd_bound_execute_ring(262) == 2 &&
              dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 262U,
              "PFD indirect-buffer opcode executes physical command list");
        dotm::mm_bound_free_physical_memory(1U, ib);
        // CP_RB_WPTR is written directly by the original PPC at 0x7FC80714.
        put(262, 0x80000000U); // PM4 type-2 no-op, real ring dword.
        auto* wptr_mmio = memory.address(0x7FC80714U);
        wptr_mmio[0] = 0U; wptr_mmio[1] = 0U;
        wptr_mmio[2] = 1U; wptr_mmio[3] = 7U; // 263 dwords, big-endian.
        check(dotm::vd_bound_drain_mmio_ring() == 1 &&
              dotm::vd_bound_gpu_snapshot().ring_read_pointer_dwords == 263U &&
              memory.address(writeback)[2] == 1U &&
              memory.address(writeback)[3] == 7U &&
              dotm::vd_bound_drain_mmio_ring() == 0,
              "Original PPC MMIO CP_RB_WPTR drives native ring and writeback");
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
        check(!dotm::vd_bound_gpu_snapshot().initialized &&
              dotm::vd_bound_copy_shader_microcode(0U).empty() &&
              dotm::vd_bound_copy_shader_microcode(1U).empty(),
              "VdShutdownEngines clears original shader microcode and engine state");
        check(dotm::vd_bound_read_gpu_register(0x123U) == 0U &&
              dotm::vd_bound_read_gpu_mmio_register(0x123U) == 0U &&
              memory.committed_range(0x7FC80000U, 0x80000U),
              "GPU shutdown clears both register views without unmapping PPC MMIO");
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