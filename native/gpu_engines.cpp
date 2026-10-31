#include "gpu_engines.hpp"
#include "kernel_guest_memory.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <stdexcept>

namespace dotm {
namespace {
std::mutex gpu_mutex;
GuestMemory* guest_memory = nullptr;
GpuEngineSnapshot engine;
std::array<std::uint32_t, 0x5000> gpu_registers{};

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
std::uint32_t read_be32(std::uint32_t address) {
    const auto* p = guest_memory->address(address);
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
           (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
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
    gpu_registers.fill(0);
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
    gpu_registers.fill(0);
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
    gpu_registers.fill(0);
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
void vd_bound_set_system_command_buffer_gpu_identifier_address(std::uint32_t address) {
    std::lock_guard lock(gpu_mutex);
    if (!engine.initialized || !guest_memory)
        throw std::runtime_error("Xbox GPU engines are not initialized");
    // The retail driver registers a writable guest dword, or zero to detach.
    // Keep the address for the command processor; do not fabricate GPU IDs.
    if (address && ((address & 3U) || !guest_memory->committed_range(address, 4)))
        throw std::runtime_error("Invalid Xbox GPU command-buffer identifier address");
    engine.system_command_buffer_gpu_identifier_address = address;
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
    engine.ring_read_pointer_dwords = 0;
    engine.packets_executed = 0;
    engine.register_writes = 0;
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
std::uint32_t vd_bound_execute_ring(std::uint32_t write_pointer_dwords,
                                    std::uint32_t max_packets) {
    std::lock_guard lock(gpu_mutex);
    if (!engine.ring_initialized || !guest_memory || !max_packets)
        throw std::runtime_error("GPU command processor is not ready");
    const auto capacity = engine.ring_size_bytes / 4;
    if (write_pointer_dwords >= capacity ||
        mm_bound_resolve_physical_range(engine.ring_physical_address,
                                        engine.ring_size_bytes) !=
            std::optional<std::uint32_t>(engine.ring_guest_address) ||
        !readable(engine.ring_guest_address, engine.ring_size_bytes))
        throw std::runtime_error("GPU ring pointer or physical allocation invalid");
    if (engine.read_pointer_writeback_enabled &&
        mm_bound_resolve_physical_range(
            engine.read_pointer_writeback_physical_address, 4) !=
            std::optional<std::uint32_t>(engine.read_pointer_writeback_guest_address))
        throw std::runtime_error("GPU read-pointer writeback page freed");
    auto at = [&](std::uint32_t index) {
        return read_be32(engine.ring_guest_address + (index % capacity) * 4);
    };
    std::uint32_t completed = 0;
    while (engine.ring_read_pointer_dwords != write_pointer_dwords &&
           completed < max_packets) {
        const auto start = engine.ring_read_pointer_dwords;
        const auto available = (write_pointer_dwords + capacity - start) % capacity;
        const auto header = at(start);
        const auto type = header >> 30;
        const auto count = ((header >> 16) & 0x3FFFU) + 1;
        // A zero dword is a valid type-0 packet writing register zero.
        // Only type-2 packets have no payload; treating header zero as
        // padding silently desynchronizes the command processor.
        const auto payload = type == 2 ? 0U : type == 1 ? 2U : count;
        if (payload >= available)
            throw std::runtime_error("Incomplete Xenos PM4 packet in physical ring");
        const auto first = (start + 1) % capacity;
        if (type == 0) {
            const auto base = header & 0x7FFFU;
            const bool repeat = (header & 0x8000U) != 0;
            if (base >= gpu_registers.size() ||
                (!repeat && std::uint64_t(base) + payload > gpu_registers.size()))
                throw std::runtime_error("Xenos PM4 type-0 register out of range");
            for (std::uint32_t i = 0; i < payload; ++i) {
                gpu_registers[base + (repeat ? 0 : i)] = at(first + i);
            }
            engine.register_writes += payload;
        } else if (type == 1) {
            const auto r0 = header & 0x7FFU;
            const auto r1 = (header >> 11) & 0x7FFU;
            gpu_registers[r0] = at(first);
            gpu_registers[r1] = at(first + 1);
            engine.register_writes += 2;
        } else if (type == 3) {
            const auto opcode = (header >> 8) & 0x7FU;
            if (header & 1U)
                throw std::runtime_error("Predicated Xenos PM4 packet requires bin state");
            if (opcode == 0x10U) {
                // PM4_NOP has payload but does not change GPU state.
            } else if (opcode == 0x21U) {
                // PM4_REG_RMW: register index, AND mask, OR mask.
                // Apply the update only after validating the complete packet.
                if (payload != 3U)
                    throw std::runtime_error("Invalid Xenos PM4 REG_RMW payload size");
                const auto index = at(first);
                if (index >= gpu_registers.size())
                    throw std::runtime_error("Xenos PM4 REG_RMW register out of range");
                const auto and_mask = at(first + 1);
                const auto or_mask = at(first + 2);
                gpu_registers[index] = (gpu_registers[index] & and_mask) | or_mask;
                ++engine.register_writes;
            } else if (opcode == 0x55U || opcode == 0x56U || opcode == 0x2DU) {
                const auto offset_type = at(first);
                std::uint32_t index = offset_type & 0xFFFFU;
                if (opcode == 0x2DU) {
                    index = offset_type & 0x7FFU;
                    switch ((offset_type >> 16) & 0xFFU) {
                    case 0: index += 0x4000U; break;
                    case 1: index += 0x4800U; break;
                    case 2: index += 0x4900U; break;
                    case 3: index += 0x4908U; break;
                    case 4: index += 0x2000U; break;
                    default: throw std::runtime_error("Unsupported Xenos PM4 constant bank");
                    }
                }
                if (index >= gpu_registers.size() ||
                    std::uint64_t(index) + payload - 1 > gpu_registers.size())
                    throw std::runtime_error("Xenos PM4 constant register overflow");
                for (std::uint32_t i = 1; i < payload; ++i)
                    gpu_registers[index + i - 1] = at(first + i);
                engine.register_writes += payload - 1;
            } else {
                throw std::runtime_error("Unsupported Xenos PM4 type-3 operation");
            }
        }
        engine.ring_read_pointer_dwords = (start + payload + 1) % capacity;
        ++engine.packets_executed;
        ++completed;
        if (engine.read_pointer_writeback_enabled) {
            write_be32(engine.read_pointer_writeback_guest_address,
                       engine.ring_read_pointer_dwords);
        }
    }
    return completed;
}
std::uint32_t vd_bound_read_gpu_register(std::uint32_t index) {
    std::lock_guard lock(gpu_mutex);
    if (index >= gpu_registers.size())
        throw std::runtime_error("Xenos register index out of range");
    return gpu_registers[index];
}
GpuEngineSnapshot vd_bound_gpu_snapshot() {
    std::lock_guard lock(gpu_mutex);
    return engine;
}
}