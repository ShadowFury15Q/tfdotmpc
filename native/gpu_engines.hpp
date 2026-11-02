#pragma once
#include "guest_memory.hpp"
#include "xenos_vertex_fetch.hpp"
#include <cstdint>
#include <vector>

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
    // Actual draw submissions accepted by a native graphics backend.
    std::uint64_t draws_submitted = 0;
    std::uint32_t last_draw_initiator = 0;
    std::uint32_t gpu_frame_counter = 0;
    // VdSwap has constructed a guest PM4 fetch command, not presented a frame.
    std::uint64_t swap_requests = 0;
    std::uint32_t last_swap_physical_address = 0;
    std::uint32_t last_swap_width = 0;
    std::uint32_t last_swap_height = 0;
    std::uint32_t last_swap_texture_format = 0;
    std::uint32_t display_mode_flags = 0;
    std::uint32_t display_mode_width = 0;
    std::uint32_t display_mode_height = 0;
    std::uint64_t display_mode_changes = 0;
    std::uint64_t register_writes = 0;
    // Original PM4_INVALIDATE_STATE mask, pending native GPU cache handling.
    std::uint64_t state_invalidations = 0;
    std::uint32_t last_state_invalidation_mask = 0;
    // Original Xenos sequencer microcode, captured from PM4 uploads.
    // Capturing is not executing or rendering a shader.
    std::uint64_t shader_uploads = 0;
    std::uint32_t vertex_shader_dwords = 0;
    std::uint32_t pixel_shader_dwords = 0;
    std::uint64_t vertex_shader_fingerprint = 0;
    std::uint64_t pixel_shader_fingerprint = 0;
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
    // Direct scanout needs no scaling command stream when source, output
    // and frontbuffer are the same size. Actual GPU presentation is separate.
    bool identity_scanout_configured = false;
    std::uint64_t identity_scanout_requests = 0;
    std::uint32_t identity_scanout_width = 0;
    std::uint32_t identity_scanout_height = 0;
    std::uint64_t graphics_notifications = 0;
    std::uint32_t last_graphics_notification = 0;
    std::uint16_t frontbuffer_width = 0;
    std::uint16_t frontbuffer_height = 0;
    std::uint16_t backbuffer_width = 0;
    std::uint16_t backbuffer_height = 0;
};

// A native graphics backend receives the complete original Xenos draw state.
// No draw is acknowledged without a backend that actually accepts the work.
// The callback runs synchronously under the GPU state lock; it must not call
// vd_bound_* functions or retain references to the command.
// Read-only, bounded copy of a real Xbox physical allocation. Only valid
// synchronously inside GpuDrawSubmitter; never retain or call from workers.
// This lets a native renderer fetch vertex/texture bytes without exposing
// guest virtual pointers or assuming an unallocated GPU address exists.
using GpuPhysicalMemoryReader = std::vector<std::uint8_t> (*)(
    std::uint32_t physical_address, std::uint32_t byte_count);
struct GpuDrawCommand {
    std::uint32_t initiator = 0;
    // PM4_DRAW_INDX carries the original visibility-query condition before
    // VGT_DRAW_INITIATOR. A native backend must evaluate nonzero tokens
    // against its real occlusion-query results, never assume visibility.
    bool has_visibility_query_token = false;
    std::uint32_t visibility_query_token = 0;
    std::uint32_t primitive_type = 0;
    std::uint32_t source_select = 0;
    std::uint32_t major_mode = 0;
    std::uint32_t index_count = 0;
    std::uint32_t index_size_bytes = 0;
    std::uint32_t dma_physical_address = 0;
    std::uint32_t dma_swap_mode = 0;
    // Exact guest bytes, including the Xenos DMA endian representation.
    std::vector<std::uint8_t> dma_index_bytes;
    // Host-native indices after the real Xenos DMA byte-lane permutation.
    // Auto-index draws use source_select=2 and do not supply this vector.
    std::vector<std::uint32_t> decoded_indices;
    // Copy source buffers referenced by the original Xenos fetch registers.
    // Valid only while this draw's submitter callback is running.
    GpuPhysicalMemoryReader copy_physical_memory = nullptr;
    // CPU MMIO writes are included, not only the PM4 register shadow.
    std::vector<std::uint32_t> register_words;
    // Original Xenos vertex fetch constants decoded from the 0x4800 register bank.
    // Buffer bytes are only accessible via the draw-scoped physical reader.
    std::vector<XenosVertexFetchConstant> vertex_fetch_constants;
    std::vector<std::uint32_t> vertex_shader_words;
    std::vector<std::uint32_t> pixel_shader_words;
    // Fetch only the vertex bytes actually referenced by a shader. Xenos
    // endian permutations operate on complete 32-bit DMA lanes, so expand
    // partial requests to physical DWORD boundaries before decoding.
    // Never read outside the bound fetch constant or wrap physical addresses.
    // The reader additionally checks live allocation and draw lifetime.
    std::vector<std::uint8_t> copy_vertex_fetch_range(
        std::uint32_t slot, std::uint32_t byte_offset,
        std::uint32_t byte_count) const {
        for (const auto& binding : vertex_fetch_constants) {
            if (binding.slot != slot) continue;
            if (!copy_physical_memory)
                throw std::runtime_error("No active Xenos draw physical reader");
            const auto total = std::uint64_t(binding.size_dwords) * 4U;
            const auto end = std::uint64_t(byte_offset) + byte_count;
            if (end > total)
                throw std::runtime_error("Xenos vertex fetch exceeds bound buffer");
            if (!byte_count) return {};
            const auto aligned_start = byte_offset & ~std::uint32_t(3U);
            const auto aligned_end = (end + 3U) & ~std::uint64_t(3U);
            const auto fetch_bytes = aligned_end - aligned_start;
            const auto physical = std::uint64_t(binding.physical_address) +
                                  aligned_start;
            if (physical + fetch_bytes > 0x100000000ULL)
                throw std::runtime_error("Xenos vertex fetch physical address wraps");
            const auto raw = copy_physical_memory(
                static_cast<std::uint32_t>(physical),
                static_cast<std::uint32_t>(fetch_bytes));
            const XenosVertexFetchConstant segment{
                slot, static_cast<std::uint32_t>(physical),
                static_cast<std::uint32_t>(fetch_bytes / 4U),
                binding.endian_mode};
            const auto decoded = decode_xenos_vertex_fetch_bytes(segment, raw);
            const auto skip = byte_offset - aligned_start;
            return {decoded.begin() + skip,
                    decoded.begin() + skip + byte_count};
        }
        throw std::runtime_error("Xenos vertex fetch slot is not bound");
    }
    // Full-buffer compatibility for backends that need the entire binding.
    std::vector<std::uint8_t> copy_vertex_fetch_buffer(std::uint32_t slot) const {
        for (const auto& binding : vertex_fetch_constants) {
            if (binding.slot == slot)
                return copy_vertex_fetch_range(
                    slot, 0U, binding.size_dwords * 4U);
        }
        throw std::runtime_error("Xenos vertex fetch slot is not bound");
    }
};
using GpuDrawSubmitter = bool (*)(const GpuDrawCommand&, void*);
void vd_bound_set_draw_submitter(GpuDrawSubmitter submitter, void* context);

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
// Original DOTM PPC 0x825201F0 passes 8 register arguments and 4 stack
// arguments at SP+84,+92,+100,+108. For a 1:1 720p scanout there is no
// scaling work: return zero PM4 words and register the native scanout
// dimensions. Non-identity scaling remains unsupported and fails closed.
std::uint32_t vd_bound_initialize_scaler_command_buffer(
    std::uint32_t source_xy, std::uint32_t source_wh,
    std::uint32_t output_xy, std::uint32_t output_wh,
    std::uint32_t frontbuffer_wh, std::uint32_t vertical_filter,
    std::uint32_t vertical_parameters, std::uint32_t horizontal_filter,
    std::uint32_t guest_stack);
// Retail DOTM display setup at 0x82512984 and the immediately following
// X_DISPLAY_INFO query (88-byte Xbox big-endian ABI).
std::uint32_t vd_bound_set_display_mode(std::uint32_t flags);
void vd_bound_get_current_display_information(std::uint32_t output);
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
// Poll the original PPC-written CP_RB_WPTR MMIO register (byte offset 0x714)
// and process the real ring. Returns zero before ring initialization.
std::uint32_t vd_bound_drain_mmio_ring(std::uint32_t max_packets = 4096);
// Copy currently bound original Xenos microcode (0=vertex, 1=pixel)
// for a future native shader translator. Does not render it.
std::vector<std::uint32_t> vd_bound_copy_shader_microcode(std::uint32_t stage);
std::uint32_t vd_bound_read_gpu_register(std::uint32_t index);
GpuEngineSnapshot vd_bound_gpu_snapshot();
}