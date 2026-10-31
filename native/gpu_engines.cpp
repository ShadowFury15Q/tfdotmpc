#include "gpu_engines.hpp"
#include "kernel_guest_memory.hpp"
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <stdexcept>

namespace dotm {
namespace {
std::mutex gpu_mutex;
GuestMemory* guest_memory = nullptr;
GpuEngineSnapshot engine;

bool readable(std::uint32_t address, std::size_t size) {
    return guest_memory && address >= 0x10000U &&
           std::uint64_t(address) + size <= GuestMemory::kAddressSpaceSize &&
           guest_memory->committed_range(address, size);
}
// Microcode is read from the original PE's guest pages, not a hardcoded
// synthetic command stream. Fingerprints allow later GPU backend validation.
std::uint64_t microcode_fingerprint(std::uint32_t address) {
    constexpr std::size_t kProbeBytes = 64;
    constexpr std::uint64_t kOffset = 14695981039346656037ULL;
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    if (!readable(address, kProbeBytes) || (address & 3U)) return 0;
    std::uint64_t hash = kOffset;
    bool nonzero = false;
    for (std::size_t i = 0; i < kProbeBytes; ++i) {
        const auto value = guest_memory->address(address)[i];
        nonzero |= value != 0;
        hash = (hash ^ value) * kPrime;
    }
    return nonzero ? hash : 0;
}
void write_be32(std::uint32_t address, std::uint32_t value) {
    auto* p = guest_memory->address(address);
    p[0] = static_cast<std::uint8_t>(value >> 24);
    p[1] = static_cast<std::uint8_t>(value >> 16);
    p[2] = static_cast<std::uint8_t>(value >> 8);
    p[3] = static_cast<std::uint8_t>(value);
}
}
void bind_gpu_engines(GuestMemory* memory) noexcept {
    std::lock_guard lock(gpu_mutex);
    guest_memory = memory;
    engine = {};
}
std::uint32_t vd_bound_initialize_engines(
    std::uint32_t flags, std::uint32_t callback, std::uint32_t callback_context,
    std::uint32_t pfp_microcode, std::uint32_t me_microcode) {
    std::lock_guard lock(gpu_mutex);
    // The original DOTM calls with 0x4F810001. Other hardware configurations
    // require separate validation rather than silently claiming GPU support.
    if (flags != 0x4F810001U || !callback || !readable(callback, 4))
        return 0;
    const auto pfp_hash = microcode_fingerprint(pfp_microcode);
    const auto me_hash = microcode_fingerprint(me_microcode);
    if (!pfp_hash || !me_hash) return 0;
    engine = {};
    engine.initialized = true;
    engine.flags = flags;
    engine.startup_callback = callback;
    engine.startup_context = callback_context;
    engine.pfp_microcode_address = pfp_microcode;
    engine.me_microcode_address = me_microcode;
    engine.pfp_microcode_fingerprint = pfp_hash;
    engine.me_microcode_fingerprint = me_hash;
    return 1;
}
void vd_bound_shutdown_engines() noexcept {
    std::lock_guard lock(gpu_mutex);
    engine = {};
}
void vd_bound_set_graphics_interrupt_callback(
    std::uint32_t callback, std::uint32_t context) {
    std::lock_guard lock(gpu_mutex);
    if (!engine.initialized)
        throw std::runtime_error("GPU interrupt registration before engine initialization");
    if (callback && !readable(callback, 4))
        throw std::runtime_error("Invalid guest GPU interrupt callback");
    engine.interrupt_callback = callback;
    engine.interrupt_context = callback ? context : 0;
}
void vd_bound_initialize_ring_buffer(
    std::uint32_t physical_address, std::uint32_t size_log2) {
    std::lock_guard lock(gpu_mutex);
    if (!engine.initialized || size_log2 < 9 || size_log2 > 21)
        throw std::runtime_error("Invalid Xbox GPU ring size or engine state");
    // Xbox CP ring length in bytes = 1 << (size_log2 + 3).
    const std::uint32_t bytes = 1U << (size_log2 + 3);
    const auto guest_address = mm_bound_resolve_physical_range(
        physical_address, bytes);
    if (!guest_address || !readable(*guest_address, bytes))
        throw std::runtime_error("GPU ring must use allocated physical pages");
    engine.ring_initialized = true;
    engine.ring_physical_address = physical_address;
    engine.ring_guest_address = *guest_address;
    engine.ring_size_bytes = bytes;
    engine.read_pointer_writeback_enabled = false;
    engine.read_pointer_writeback_physical_address = 0;
    engine.read_pointer_writeback_guest_address = 0;
}
void vd_bound_enable_ring_buffer_read_pointer_writeback(
    std::uint32_t physical_address, std::uint32_t block_size_log2) {
    std::lock_guard lock(gpu_mutex);
    if (!engine.ring_initialized || (physical_address & 3U) ||
        block_size_log2 > 19)
        throw std::runtime_error("Invalid Xbox GPU read-pointer writeback setup");
    const auto guest_address = mm_bound_resolve_physical_range(
        physical_address, 4);
    if (!guest_address || !readable(*guest_address, 4))
        throw std::runtime_error("GPU read-pointer writeback requires physical memory");
    // CP_RB_RPTR starts at zero when the ring is initialized.
    write_be32(*guest_address, 0);
    engine.read_pointer_writeback_enabled = true;
    engine.read_pointer_writeback_physical_address = physical_address;
    engine.read_pointer_writeback_guest_address = *guest_address;
    engine.read_pointer_writeback_block_log2 = block_size_log2;
}
GpuEngineSnapshot vd_bound_gpu_snapshot() {
    std::lock_guard lock(gpu_mutex);
    return engine;
}
}