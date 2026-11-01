#include "gpu_engines.hpp"
#include "kernel_guest_memory.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace dotm {
namespace {
std::mutex gpu_mutex;
GuestMemory* guest_memory = nullptr;
GpuEngineSnapshot engine;
std::array<std::uint32_t, 0x5000> gpu_registers{};
constexpr std::uint32_t kXenosMmioBase = 0x7FC80000U;
constexpr std::uint32_t kXenosMmioBytes = 0x00080000U;

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
// Xenos DMA writes target allocated GPU physical pages, not arbitrary guest
// virtual addresses. The low address bits specify the 32-bit endian mode.
std::uint32_t gpu_physical_dword_address(std::uint32_t encoded_address) {
    const auto guest = mm_bound_resolve_physical_range(encoded_address & ~3U, 4U);
    if (!guest || !readable(*guest, 4U))
        throw std::runtime_error("Xenos PM4 DMA write requires allocated physical memory");
    return *guest;
}
void write_gpu_physical_dword(std::uint32_t encoded_address, std::uint32_t value) {
    const auto guest = gpu_physical_dword_address(encoded_address);
    // Store in guest byte order. These are the four Xenos 32-bit GPU
    // endian modes (none, 8-in-16, 8-in-32, 16-in-32).
    switch (encoded_address & 3U) {
    case 0: value = ((value & 0x000000FFU) << 24) |
                    ((value & 0x0000FF00U) << 8) |
                    ((value & 0x00FF0000U) >> 8) |
                    ((value & 0xFF000000U) >> 24); break;
    case 1: value = (value << 16) | (value >> 16); break;
    case 2: break;
    case 3: value = ((value & 0x00FF00FFU) << 8) |
                    ((value & 0xFF00FF00U) >> 8); break;
    }
    write_be32(guest, value);
}
// Read the same physical DWORD format used by GPU writes. Xenos embeds
// the endian selector in the low two bits of the physical address.
std::uint32_t read_gpu_physical_dword(std::uint32_t encoded_address) {
    const auto guest = gpu_physical_dword_address(encoded_address);
    const auto value = read_be32(guest);
    switch (encoded_address & 3U) {
    case 0: return ((value & 0x000000FFU) << 24) |
                   ((value & 0x0000FF00U) << 8) |
                   ((value & 0x00FF0000U) >> 8) |
                   ((value & 0xFF000000U) >> 24);
    case 1: return (value << 16) | (value >> 16);
    case 2: return value;
    default: return ((value & 0x00FF00FFU) << 8) |
                    ((value & 0xFF00FF00U) >> 8);
    }
}
// CPU-side Xenos MMIO and PM4 packet writes address the same register file.
// Guest PPC accesses the MMIO pages directly, so the guest bytes are the
// authoritative register state whenever the aperture has been mapped.
// A successful GPU reset must invalidate both register representations.
// Otherwise direct PPC MMIO reads retain stale values after initialization
// even though the native PM4 register shadow has been cleared.
void reset_gpu_registers() {
    gpu_registers.fill(0);
    if (guest_memory &&
        guest_memory->committed_range(kXenosMmioBase, kXenosMmioBytes))
        std::memset(guest_memory->address(kXenosMmioBase), 0, kXenosMmioBytes);
}
std::uint32_t read_gpu_register(std::uint32_t index) {
    const auto address = kXenosMmioBase + index * 4U;
    return readable(address, 4U) ? read_be32(address) : gpu_registers[index];
}
void write_gpu_register(std::uint32_t index, std::uint32_t value) {
    // Xenos scratch registers are a hardware CPU/GPU synchronization path.
    // An enabled SCRATCH_UMSK bit causes a write of SCRATCH_REGn to the
    // physical dword at SCRATCH_ADDR + 4*n. Validate the destination before
    // changing the register, so an invalid DMA leaves the packet pending.
    std::optional<std::uint32_t> scratch_writeback;
    if (index >= 0x0578U && index <= 0x057FU &&
        (read_gpu_register(0x01DCU) & (1U << (index - 0x0578U)))) {
        const auto physical = std::uint64_t(read_gpu_register(0x01DDU)) +
                              std::uint64_t(index - 0x0578U) * 4U;
        if ((physical & 3U) || physical > UINT32_MAX)
            throw std::runtime_error("Xenos scratch writeback address invalid");
        scratch_writeback = gpu_physical_dword_address(
            static_cast<std::uint32_t>(physical));
    }
    gpu_registers[index] = value;
    const auto address = kXenosMmioBase + index * 4U;
    if (readable(address, 4U)) write_be32(address, value);
    if (scratch_writeback) write_be32(*scratch_writeback, value);
}
// Preflight an entire PM4 register packet before committing any MMIO or
// scratch DMA writes. A later invalid scratch destination must not leave
// earlier register writes visible while the ring read pointer stays pending.
// Track UMSK/ADDR writes inside the same packet in their original order.
template <typename RegisterAt, typename ValueAt>
void validate_gpu_register_packet(std::uint32_t count,
                                  RegisterAt register_at, ValueAt value_at) {
    auto scratch_mask = read_gpu_register(0x01DCU);
    auto scratch_address = read_gpu_register(0x01DDU);
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto index = register_at(i);
        const auto value = value_at(i);
        if (index == 0x01DCU) {
            scratch_mask = value;
        } else if (index == 0x01DDU) {
            scratch_address = value;
        } else if (index >= 0x0578U && index <= 0x057FU &&
                   (scratch_mask & (1U << (index - 0x0578U)))) {
            const auto physical = std::uint64_t(scratch_address) +
                                  std::uint64_t(index - 0x0578U) * 4U;
            if ((physical & 3U) || physical > UINT32_MAX)
                throw std::runtime_error("Xenos scratch writeback address invalid");
            (void)gpu_physical_dword_address(
                static_cast<std::uint32_t>(physical));
        }
    }
}
// Hash the actual big-endian packet words, including the type-3 header.
// This also works for a packet wrapped around the physical GPU ring.
constexpr std::uint64_t kPacketHashOffset = 14695981039346656037ULL;
constexpr std::uint64_t kPacketHashPrime = 1099511628211ULL;
std::uint64_t hash_packet_word(std::uint64_t hash, std::uint32_t word) {
    for (unsigned shift : {24U, 16U, 8U, 0U})
        hash = (hash ^ ((word >> shift) & 0xFFU)) * kPacketHashPrime;
    return hash;
}
std::uint64_t hash_guest_packet(std::uint32_t address, std::uint32_t words) {
    auto hash = kPacketHashOffset;
    for (std::uint32_t i = 0; i < words; ++i)
        hash = hash_packet_word(hash, read_be32(address + i * 4));
    return hash;
}
}
void vd_bound_map_gpu_mmio() {
    std::lock_guard lock(gpu_mutex);
    if (!guest_memory)
        throw std::runtime_error("Xbox GPU MMIO requires a guest address space");
    if (guest_memory->any_committed_range(kXenosMmioBase, kXenosMmioBytes)) {
        if (guest_memory->committed_range(kXenosMmioBase, kXenosMmioBytes))
            return;
        throw std::runtime_error("Xbox GPU MMIO overlaps guest allocations");
    }
    guest_memory->commit(kXenosMmioBase, kXenosMmioBytes);
}
std::uint32_t vd_bound_read_gpu_mmio_register(std::uint32_t index) {
    std::lock_guard lock(gpu_mutex);
    if (index >= 0x5000U ||
        !readable(kXenosMmioBase + index * 4U, 4U))
        throw std::runtime_error("Invalid Xbox GPU MMIO register read");
    return read_be32(kXenosMmioBase + index * 4U);
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
    reset_gpu_registers();
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
    reset_gpu_registers();
}
std::uint32_t vd_bound_call_graphics_notification_routines(
    std::uint32_t notification, std::uint32_t scaling_pointer) {
    std::lock_guard lock(gpu_mutex);
    // Original DOTM calls with r3=1 and r4 pointing to four big-endian
    // uint16_t dimensions (frontbuffer W/H, backbuffer W/H).
    if (notification != 1U || !readable(scaling_pointer, 8U))
        throw std::runtime_error("Invalid Xbox graphics scaling notification");
    const auto* data = guest_memory->address(scaling_pointer);
    const auto be16 = [&](unsigned offset) {
        return std::uint16_t((std::uint16_t(data[offset]) << 8) |
                             data[offset + 1]);
    };
    engine.last_graphics_notification = notification;
    engine.frontbuffer_width = be16(0);
    engine.frontbuffer_height = be16(2);
    engine.backbuffer_width = be16(4);
    engine.backbuffer_height = be16(6);
    ++engine.graphics_notifications;
    // No guest graphics notification registrations exist in the original
    // title's import graph. The native renderer can consume this state.
    return 0;
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
void vd_bound_verify_me_init_command(std::uint32_t address,
                                     std::uint32_t word_count) {
    std::lock_guard lock(gpu_mutex);
    // The original DOTM PPC call at 0x82518534 passes 19 DWORDs, with
    // 0xC0114800 (PM4_ME_INIT, 18 payload words) at its start.
    if (!engine.initialized || word_count != 19U || (address & 3U) ||
        !readable(address, std::size_t(word_count) * 4))
        throw std::runtime_error("Invalid Xbox VdVerifyMEInitCommand guest buffer");
    const auto header = read_be32(address);
    if ((header >> 30) != 3U || ((header >> 8) & 0x7FU) != 0x48U ||
        (header & 1U) || (((header >> 16) & 0x3FFFU) + 2U) != word_count)
        throw std::runtime_error("Invalid Xenos PM4_ME_INIT packet header");
    // Preserve the exact command fingerprint for matching the command later
    // submitted through the GPU ring. This is structural validation, not
    // a claim that the Xenos micro-engine has actually executed microcode.
    engine.me_init_command_word_count = word_count;
    engine.me_init_command_fingerprint = hash_guest_packet(address, word_count);
    engine.me_init_command_verified = true;
    engine.me_init_packet_submitted = false;
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
        // A CPU producer may publish its write pointer before the final
        // payload dword arrives. Preserve the header, read pointer and
        // writeback until a later kick supplies the entire packet. This is
        // normal producer/consumer flow, not a malformed GPU command.
        if (payload >= available)
            return completed;
        const auto first = (start + 1) % capacity;
        if (type == 0) {
            const auto base = header & 0x7FFFU;
            const bool repeat = (header & 0x8000U) != 0;
            if (base >= gpu_registers.size() ||
                (!repeat && std::uint64_t(base) + payload > gpu_registers.size()))
                throw std::runtime_error("Xenos PM4 type-0 register out of range");
            validate_gpu_register_packet(
                payload, [&](std::uint32_t i) { return base + (repeat ? 0U : i); },
                [&](std::uint32_t i) { return at(first + i); });
            for (std::uint32_t i = 0; i < payload; ++i) {
                write_gpu_register(base + (repeat ? 0 : i), at(first + i));
            }
            engine.register_writes += payload;
        } else if (type == 1) {
            const auto r0 = header & 0x7FFU;
            const auto r1 = (header >> 11) & 0x7FFU;
            validate_gpu_register_packet(
                2U, [&](std::uint32_t i) { return i ? r1 : r0; },
                [&](std::uint32_t i) { return at(first + i); });
            write_gpu_register(r0, at(first));
            write_gpu_register(r1, at(first + 1));
            engine.register_writes += 2;
        } else if (type == 3) {
            const auto opcode = (header >> 8) & 0x7FU;
            // Type-3 predicate bit gates execution by the Xenos tile-bin
            // selection, but the packet itself must still be consumed.
            // A masked-out draw must not be mistaken for an executed draw.
            if ((header & 1U) && !(engine.bin_mask & engine.bin_select)) {
                // Predicate false: leave all GPU state and guest memory intact.
            } else if (opcode == 0x48U) {
                // Submit the exact ME_INIT packet that the title previously
                // passed to xboxkrnl ordinal 0x1DF for verification.
                if (!engine.me_init_command_verified ||
                    payload + 1 != engine.me_init_command_word_count)
                    throw std::runtime_error("Unverified Xenos PM4_ME_INIT packet");
                auto hash = kPacketHashOffset;
                for (std::uint32_t i = 0; i <= payload; ++i)
                    hash = hash_packet_word(hash, at(start + i));
                if (hash != engine.me_init_command_fingerprint)
                    throw std::runtime_error("Xenos PM4_ME_INIT differs from verified command");
                engine.me_init_packet_submitted = true;
            } else if (opcode == 0x10U) {
                // PM4_NOP has payload but does not change GPU state.
            } else if (opcode == 0x3CU) {
                // PM4_WAIT_REG_MEM: wait_info, poll address, reference, mask,
                // poll interval. Do not consume the packet until its predicate
                // is satisfied; a subsequent GPU kick can retry the same wait.
                // Busy-spinning while holding gpu_mutex would prevent the
                // guest CPU from updating MMIO state and deadlock the title.
                if (payload != 5U)
                    throw std::runtime_error("Invalid Xenos PM4 WAIT_REG_MEM payload size");
                const auto wait_info = at(first);
                const auto poll_address = at(first + 1);
                const auto reference = at(first + 2);
                const auto mask = at(first + 3);
                // The fifth word controls hardware poll delay. Host-side
                // execution retries on the next ring kick instead of sleeping.
                const auto poll_interval = at(first + 4);
                (void)poll_interval;
                std::uint32_t value = 0;
                if (wait_info & 0x10U) {
                    value = read_gpu_physical_dword(poll_address);
                } else {
                    if (poll_address >= gpu_registers.size())
                        throw std::runtime_error("Xenos PM4 WAIT_REG_MEM register out of range");
                    value = read_gpu_register(poll_address);
                }
                const auto masked = value & mask;
                bool satisfied = false;
                switch (wait_info & 7U) {
                case 0: satisfied = false; break;
                case 1: satisfied = masked < reference; break;
                case 2: satisfied = masked <= reference; break;
                case 3: satisfied = masked == reference; break;
                case 4: satisfied = masked != reference; break;
                case 5: satisfied = masked >= reference; break;
                case 6: satisfied = masked > reference; break;
                case 7: satisfied = true; break;
                }
                if (!satisfied) return completed;
            } else if (opcode == 0x3EU) {
                // PM4_REG_TO_MEM: transfer a live Xenos register to an
                // allocated GPU physical dword with the requested endian mode.
                if (payload != 2U)
                    throw std::runtime_error("Invalid Xenos PM4 REG_TO_MEM payload size");
                const auto register_index = at(first);
                const auto destination = at(first + 1);
                if (register_index >= gpu_registers.size())
                    throw std::runtime_error("Xenos PM4 REG_TO_MEM register out of range");
                (void)gpu_physical_dword_address(destination);
                write_gpu_physical_dword(destination, read_gpu_register(register_index));
            } else if (opcode == 0x3DU) {
                // PM4_MEM_WRITE: one physical address followed by consecutive
                // DWORDs. Validate the entire destination before modifying
                // any guest memory, so a bad DMA cannot partially commit.
                if (payload < 2U)
                    throw std::runtime_error("Invalid Xenos PM4 MEM_WRITE payload size");
                const auto destination = at(first);
                const auto base_address = destination & ~3U;
                const auto word_count = payload - 1U;
                if (std::uint64_t(base_address) +
                        std::uint64_t(word_count) * 4U > (1ULL << 32))
                    throw std::runtime_error("Xenos PM4 MEM_WRITE address overflow");
                for (std::uint32_t i = 0; i < word_count; ++i)
                    (void)gpu_physical_dword_address(base_address + i * 4U);
                // The destination may overlap the active physical ring.
                // Snapshot all packet payload words before committing DMA:
                // otherwise the first write can overwrite a later source
                // word and silently change the rest of this same packet.
                std::vector<std::uint32_t> data;
                data.reserve(word_count);
                for (std::uint32_t i = 0; i < word_count; ++i)
                    data.push_back(at(first + 1U + i));
                for (std::uint32_t i = 0; i < word_count; ++i)
                    write_gpu_physical_dword(
                        (base_address + i * 4U) | (destination & 3U),
                        data[i]);
            } else if (opcode == 0x45U) {
                // PM4_COND_WRITE: condition, poll address, reference, mask,
                // destination, data. Both polling and writing can target
                // live MMIO registers or allocated GPU physical memory.
                if (payload != 6U)
                    throw std::runtime_error("Invalid Xenos PM4 COND_WRITE payload size");
                const auto info = at(first);
                const auto poll = at(first + 1);
                const auto reference = at(first + 2);
                const auto mask = at(first + 3);
                const auto destination = at(first + 4);
                const auto data = at(first + 5);
                std::uint32_t value;
                if (info & 0x10U) {
                    value = read_gpu_physical_dword(poll);
                } else {
                    if (poll >= gpu_registers.size())
                        throw std::runtime_error("Xenos PM4 COND_WRITE poll register out of range");
                    value = read_gpu_register(poll);
                }
                const auto masked = value & mask;
                bool matched = false;
                switch (info & 7U) {
                case 0: matched = false; break;
                case 1: matched = masked < reference; break;
                case 2: matched = masked <= reference; break;
                case 3: matched = masked == reference; break;
                case 4: matched = masked != reference; break;
                case 5: matched = masked >= reference; break;
                case 6: matched = masked > reference; break;
                case 7: matched = true; break;
                }
                if (matched) {
                    if (info & 0x100U) {
                        // Validate the destination before committing DMA.
                        write_gpu_physical_dword(destination, data);
                    } else {
                        if (destination >= gpu_registers.size())
                            throw std::runtime_error("Xenos PM4 COND_WRITE target register out of range");
                        write_gpu_register(destination, data);
                        ++engine.register_writes;
                    }
                }
            } else if (opcode == 0x2FU) {
                // PM4_LOAD_ALU_CONSTANT: DMA shader constants from allocated
                // Xenos physical memory into the live register file.
                // The source is big-endian Xbox data, not host-endian words.
                if (payload != 3U)
                    throw std::runtime_error("Invalid Xenos PM4 LOAD_ALU_CONSTANT payload size");
                const auto source = at(first) & 0x3FFFFFFFU;
                const auto offset_type = at(first + 1);
                const auto count_dwords = at(first + 2) & 0xFFFU;
                std::uint32_t index = offset_type & 0x7FFU;
                switch ((offset_type >> 16) & 0xFFU) {
                case 0: index += 0x4000U; break; // ALU
                case 1: index += 0x4800U; break; // fetch
                case 2: index += 0x4900U; break; // bool
                case 3: index += 0x4908U; break; // loop
                case 4: index += 0x2000U; break; // general registers
                default: throw std::runtime_error("Unsupported Xenos LOAD_ALU_CONSTANT bank");
                }
                if ((source & 3U) ||
                    std::uint64_t(index) + count_dwords > gpu_registers.size())
                    throw std::runtime_error("Xenos LOAD_ALU_CONSTANT alignment or register overflow");
                // Zero-length DMA is legal and reads no physical memory.
                // A nonempty transfer must resolve its entire source first.
                if (count_dwords) {
                    const auto guest_address = mm_bound_resolve_physical_range(
                        source, std::size_t(count_dwords) * 4U);
                    if (!guest_address ||
                        !readable(*guest_address, std::size_t(count_dwords) * 4U))
                        throw std::runtime_error("Xenos LOAD_ALU_CONSTANT requires allocated physical source");
                    // Validate the entire source and register span before writes.
                    // CPU-side MMIO and PM4 see the same committed values.
                    for (std::uint32_t i = 0; i < count_dwords; ++i)
                        write_gpu_register(index + i, read_be32(*guest_address + i * 4U));
                    engine.register_writes += count_dwords;
                }
            } else if (opcode == 0x21U) {
                // PM4_REG_RMW: register index, AND mask, OR mask.
                // Apply the update only after validating the complete packet.
                if (payload != 3U)
                    throw std::runtime_error("Invalid Xenos PM4 REG_RMW payload size");
                // Bits 31 and 30 select GPU register operands for the AND
                // and OR inputs. The destination and register operands use
                // 13-bit register indices; the flags are not part of the index.
                // These forms are used by the original Xenos driver during
                // shader upload and EDRAM setup.
                const auto rmw_info = at(first);
                const auto index = rmw_info & 0x1FFFU;
                const auto and_operand = at(first + 1);
                const auto or_operand = at(first + 2);
                if (index >= gpu_registers.size())
                    throw std::runtime_error("Xenos PM4 REG_RMW register out of range");
                const auto and_mask = (rmw_info & 0x80000000U)
                    ? read_gpu_register(and_operand & 0x1FFFU) : and_operand;
                const auto or_mask = (rmw_info & 0x40000000U)
                    ? read_gpu_register(or_operand & 0x1FFFU) : or_operand;
                write_gpu_register(index, (read_gpu_register(index) & and_mask) | or_mask);
                ++engine.register_writes;
            } else if (opcode == 0x50U || opcode == 0x51U) {
                // SET_BIN_MASK / SET_BIN_SELECT: high dword followed by low.
                if (payload != 2U)
                    throw std::runtime_error("Invalid Xenos PM4 bin-mask packet size");
                const auto value = (std::uint64_t(at(first)) << 32) | at(first + 1);
                if (opcode == 0x50U) engine.bin_mask = value;
                else engine.bin_select = value;
            } else if (opcode >= 0x60U && opcode <= 0x63U) {
                // Per-half updates preserve the other 32 bits of the bin mask
                // or selection, as required by tiled rendering.
                if (payload != 1U)
                    throw std::runtime_error("Invalid Xenos PM4 bin-half packet size");
                const auto value = std::uint64_t(at(first));
                switch (opcode) {
                case 0x60U: engine.bin_mask =
                    (engine.bin_mask & 0xFFFFFFFF00000000ULL) | value; break;
                case 0x61U: engine.bin_mask =
                    (engine.bin_mask & 0xFFFFFFFFULL) | (value << 32); break;
                case 0x62U: engine.bin_select =
                    (engine.bin_select & 0xFFFFFFFF00000000ULL) | value; break;
                case 0x63U: engine.bin_select =
                    (engine.bin_select & 0xFFFFFFFFULL) | (value << 32); break;
                }
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
                    write_gpu_register(index + i - 1, at(first + i));
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
    return read_gpu_register(index);
}
GpuEngineSnapshot vd_bound_gpu_snapshot() {
    std::lock_guard lock(gpu_mutex);
    return engine;
}
}