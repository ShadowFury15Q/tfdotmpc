#pragma once
#include "guest_memory.hpp"
#include <cstdint>

namespace dotm {
// State of the Xbox 360 video-engine kernel interface. This is the real
// command-buffer setup path, not a renderer or a substitute for game output.
struct GpuEngineSnapshot {
    bool initialized = false;
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
    std::uint64_t register_writes = 0;
};

// The binding must be removed before the guest address space is destroyed.
void bind_gpu_engines(GuestMemory* memory) noexcept;
// Xbox kernel VdInitializeEngines: flags, callback, callback argument,
// PFP and ME microcode addresses, returning BOOL in PPC r3.
std::uint32_t vd_bound_initialize_engines(
    std::uint32_t flags, std::uint32_t callback, std::uint32_t callback_context,
    std::uint32_t pfp_microcode, std::uint32_t me_microcode);
void vd_bound_shutdown_engines() noexcept;
void vd_bound_set_graphics_interrupt_callback(
    std::uint32_t callback, std::uint32_t context);
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