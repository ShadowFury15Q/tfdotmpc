#pragma once
#include "guest_memory.hpp"
#include <cstdint>

namespace dotm {
// State of the Xbox 360 video-engine kernel interface. This is the real
// command-buffer setup path, not a renderer or a substitute for game output.
struct GpuEngineSnapshot {
    bool initialized = false;
    // Native Xenos EDRAM backing and GPU clock-gating state (not a renderer).
    bool edram_trained = false;
    bool clock_gating_enabled = false;
    std::uint32_t edram_backing_size = 0;
    std::uint32_t edram_last_training_mode = 0;
    std::uint64_t edram_worker_calls = 0;
    std::uint64_t edram_retrain_calls = 0;
    std::uint64_t clock_gating_changes = 0;
    bool ring_initialized = false;
    bool read_pointer_writeback_enabled = false;
    std::uint32_t flags = 0;
    std::uint32_t startup_callback = 0;
    std::uint32_t startup_context = 0;
    std::uint32_t pfp_microcode_address = 0;
    std::uint32_t me_microcode_address = 0;
    std::uint64_t pfp_microcode_fingerprint = 0;
    std::uint64_t me_microcode_fingerprint = 0;
    std::uint32_t ring_physical_address = 0;
    std::uint32_t ring_guest_address = 0;
    std::uint32_t ring_size_bytes = 0;
    std::uint32_t interrupt_callback = 0;
    std::uint32_t interrupt_context = 0;
    std::uint32_t read_pointer_writeback_physical_address = 0;
    std::uint32_t read_pointer_writeback_guest_address = 0;
    std::uint32_t read_pointer_writeback_block_log2 = 0;
    std::uint32_t ring_read_pointer_dwords = 0;
    std::uint64_t packets_executed = 0;
    std::uint32_t gpu_frame_counter = 0;
    // VdSwap has constructed a guest PM4 fetch command, not presented a frame.
    std::uint64_t swap_requests = 0;
    std::uint32_t last_swap_physical_address = 0;
    std::uint32_t last_swap_width = 0;
    std::uint32_t last_swap_height = 0;
    std::uint32_t last_swap_texture_format = 0;
    std::uint64_t register_writes = 0;
    // Xenos tiled-rendering bin predication state, written by PM4 packets.
    std::uint64_t bin_mask = 0;
    std::uint64_t bin_select = 0;
    // ME_INIT command (PM4 opcode 0x48) verified from original guest memory.
    bool me_init_command_verified = false;
    bool me_init_packet_submitted = false;
    std::uint32_t me_init_command_word_count = 0;
    std::uint64_t me_init_command_fingerprint = 0;
    std::uint32_t system_command_buffer_gpu_identifier_address = 0;
    std::uint32_t system_command_buffer_next_identifier = 0;
    std::uint64_t system_command_buffer_queries = 0;
    // Last original-title display scaling notification (four big-endian u16s).
    std::uint64_t graphics_notifications = 0;
    std::uint32_t last_graphics_notification = 0;
    std::uint16_t frontbuffer_width = 0;
    std::uint16_t frontbuffer_height = 0;
    std::uint16_t backbuffer_width = 0;
    std::uint16_t backbuffer_height = 0;
};

// The binding must be removed before the guest address space is destroyed.
void bind_gpu_engines(GuestMemory* memory) noexcept;
// Xbox 360 Xenos MMIO aperture at guest 0x7FC80000. Original PPC writes
// these registers directly, outside kernel import calls.
void vd_bound_map_gpu_mmio();
std::uint32_t vd_bound_read_gpu_mmio_register(std::uint32_t index);
// Xbox kernel VdInitializeEngines: flags, callback, callback argument,
// PFP and ME microcode addresses, returning BOOL in PPC r3.
std::uint32_t vd_bound_initialize_engines(
    std::uint32_t flags, std::uint32_t callback, std::uint32_t callback_context,
    std::uint32_t pfp_microcode, std::uint32_t me_microcode);
void vd_bound_shutdown_engines() noexcept;
// Original PPC 0x82525808 calls worker(0), then retrain(mode, &offset,
// 4096, &status, scratch, 2048). Validate the actual guest memory ABI.
std::uint32_t vd_bound_retrain_edram_worker(std::uint32_t mode);
std::uint32_t vd_bound_retrain_edram(
    std::uint32_t mode, std::uint32_t offset_pointer,
    std::uint32_t region_bytes, std::uint32_t status_pointer,
    std::uint32_t scratch_pointer, std::uint32_t scratch_bytes);
// Report the result of the last native Xenos EDRAM training pass.
// The original DOTM queries this after VdRetrainEDRAM at PPC 0x8251B978.
std::uint32_t vd_bound_is_hsio_training_succeeded();
std::uint32_t vd_bound_enable_disable_clock_gating(std::uint32_t enabled);
std::uint32_t vd_bound_call_graphics_notification_routines(
    std::uint32_t notification, std::uint32_t scaling_pointer);
void vd_bound_set_graphics_interrupt_callback(
    std::uint32_t callback, std::uint32_t context);
void vd_bound_set_system_command_buffer_gpu_identifier_address(std::uint32_t address);
// XAM owns the optional 0x94-byte system command buffer descriptor. Install
// a verified guest copy or detach it with descriptor_address == 0. No
// command buffer is fabricated when XAM has not supplied one.
void vd_bound_install_system_command_buffer(std::uint32_t descriptor_address,
                                           std::uint32_t initial_identifier);
// The original DOTM calls xboxkrnl VdPersistDisplay(r3=12-byte
// display descriptor, r4=guest address for returned physical allocation)
// at PPC 0x82512C70 and immediately releases the returned allocation via
// MmFreePhysicalMemory(1, address). Allocate real tracked physical memory.
// This does not substitute for native frontbuffer presentation.
std::uint32_t vd_bound_persist_display(std::uint32_t descriptor_address,
                                       std::uint32_t allocation_output);
// xboxkrnl VdGetSystemCommandBuffer(r3=descriptor, r4=identifier): return the
// XAM-owned descriptor and advance its two-step GPU fence sequence.
void vd_bound_get_system_command_buffer(std::uint32_t descriptor_address,
                                        std::uint32_t identifier_address);
// Original DOTM 0x8251286C VdSwap: eight PPC register arguments and
// two guest stack-spilled pointers at SP+84 and SP+92. Generate the
// real fetch-constant PM4 register packet in the guest's 64-DWORD slot.
// This does NOT mark a frame presented; a native renderer is still needed.
void vd_bound_swap(std::uint32_t command_buffer, std::uint32_t texture_fetch,
                   std::uint32_t system_writeback, std::uint32_t system_commands,
                   std::uint32_t gpu_identifier, std::uint32_t frontbuffer_output,
                   std::uint32_t format_output, std::uint32_t color_space_output,
                   std::uint32_t guest_stack);
// xboxkrnl ordinal 0x1DF: VdVerifyMEInitCommand, mislabeled as
// KiApcNormalRoutineNop in some Xbox 360 export name tables.
// Validate the original big-endian PM4_ME_INIT command in guest memory.
void vd_bound_verify_me_init_command(std::uint32_t guest_address,
                                     std::uint32_t word_count);
void vd_bound_initialize_ring_buffer(
    std::uint32_t physical_address, std::uint32_t size_log2);
void vd_bound_enable_ring_buffer_read_pointer_writeback(
    std::uint32_t physical_address, std::uint32_t block_size_log2);
// Consume original big-endian Xenos PM4 words from the allocated physical ring.
// The write pointer is a ring index in dwords, not a byte address.
// Unsupported GPU commands fail closed rather than pretending to render.
std::uint32_t vd_bound_execute_ring(std::uint32_t write_pointer_dwords,
                                    std::uint32_t max_packets = 4096);
std::uint32_t vd_bound_read_gpu_register(std::uint32_t index);
GpuEngineSnapshot vd_bound_gpu_snapshot();
}