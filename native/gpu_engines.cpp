#include "gpu_engines.hpp"
#include "xenos_index_decode.hpp"
#include "kernel_guest_memory.hpp"
#include <algorithm>
#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace dotm {
namespace {
std::mutex gpu_mutex;
GuestMemory* guest_memory = nullptr;
GpuEngineSnapshot engine;
std::array<std::uint32_t, 0x5003> gpu_registers{};
constexpr std::uint32_t kEdramBytes = 10U * 1024U * 1024U;
constexpr std::size_t kSystemCommandBufferBytes = 0x94U;
std::array<std::uint8_t, kSystemCommandBufferBytes> system_command_buffer{};
bool system_command_buffer_installed = false;
std::vector<std::uint8_t> edram_backing;
std::array<std::vector<std::uint32_t>, 2> active_shader_microcode{};
// Each indirect buffer retains its execution cursor across WAIT_REG_MEM kicks.
// The parent PM4 packet is acknowledged only after all nested commands finish.
struct IndirectFrame {
    std::uint32_t physical_address;
    std::uint32_t guest_address;
    std::uint32_t word_count;
    std::uint32_t read_offset;
};
std::vector<IndirectFrame> indirect_frames;
GpuDrawSubmitter draw_submitter = nullptr;
void* draw_submitter_context = nullptr;
constexpr std::size_t kMaxIndirectDepth = 8;
constexpr std::uint32_t kXenosMmioBase = 0x7FC80000U;
constexpr std::uint32_t kXenosMmioBytes = 0x00080000U;
// Xenos COHER_STATUS_HOST register, 32-bit MMIO index.
constexpr std::uint32_t kCoherStatusHost = 0x0A31U;

bool readable(std::uint32_t address, std::size_t size) {
    return guest_memory && address >= 0x10000U &&
           std::uint64_t(address) + size <= GuestMemory::kAddressSpaceSize &&
           guest_memory->committed_range(address, size);
}
// GPU draw callbacks run under gpu_mutex. A backend may copy original
// vertex/texture bytes during submission, but must not retain a guest pointer
// or read freed/reused physical allocations after the callback returns.
thread_local bool draw_read_window_open = false;
struct ScopedDrawReadWindow {
    const bool previous = draw_read_window_open;
    ScopedDrawReadWindow() { draw_read_window_open = true; }
    ~ScopedDrawReadWindow() { draw_read_window_open = previous; }
};
std::vector<std::uint8_t> copy_draw_physical_bytes(
    std::uint32_t physical_address, std::uint32_t byte_count) {
    constexpr std::uint32_t kMaxCopyBytes = 64U * 1024U * 1024U;
    if (!draw_read_window_open)
        throw std::runtime_error("Xenos physical fetch outside active draw submission");
    if (byte_count > kMaxCopyBytes)
        throw std::runtime_error("Xenos physical fetch exceeds bounded copy size");
    if (!byte_count) return {};
    const auto guest = mm_bound_resolve_physical_range(physical_address, byte_count);
    if (!guest || !readable(*guest, byte_count))
        throw std::runtime_error("Xenos physical fetch requires allocated committed memory");
    const auto* data = guest_memory->address(*guest);
    return {data, data + byte_count};
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
    if (index >= gpu_registers.size())
        throw std::runtime_error("Xenos GPU register read out of range");
    const auto address = kXenosMmioBase + index * 4U;
    return readable(address, 4U) ? read_be32(address) : gpu_registers[index];
}
void write_gpu_register(std::uint32_t index, std::uint32_t value) {
    if (index >= gpu_registers.size())
        throw std::runtime_error("Xenos GPU register write out of range");
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
    // Xenos marks a cache-coherency request pending when the CPU or PM4
    // writes COHER_STATUS_HOST. WAIT_REG_MEM must observe completion, not
    // indefinitely poll the request bits as if this were an ordinary register.
    if (index == kCoherStatusHost && value)
        value |= 0x80000000U;
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
    if (index >= gpu_registers.size() ||
        !readable(kXenosMmioBase + index * 4U, 4U))
        throw std::runtime_error("Invalid Xbox GPU MMIO register read");
    return read_be32(kXenosMmioBase + index * 4U);
}
void vd_bound_set_draw_submitter(GpuDrawSubmitter submitter, void* context) {
    std::lock_guard lock(gpu_mutex);
    draw_submitter = submitter;
    draw_submitter_context = submitter ? context : nullptr;
}
void bind_gpu_engines(GuestMemory* memory) noexcept {
    std::lock_guard lock(gpu_mutex);
    guest_memory = memory;
    draw_submitter = nullptr;
    draw_submitter_context = nullptr;
    engine = {};
    indirect_frames.clear();
    gpu_registers.fill(0);
    edram_backing.clear();
    for (auto& words : active_shader_microcode) words.clear();
    system_command_buffer.fill(0);
    system_command_buffer_installed = false;
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
    const auto previous = engine;
    engine = {};
    indirect_frames.clear();
    for (auto& words : active_shader_microcode) words.clear();
    system_command_buffer.fill(0);
    system_command_buffer_installed = false;
    engine.system_command_buffer_next_identifier = 1U;
    engine.edram_trained = previous.edram_trained;
    engine.edram_backing_size = previous.edram_backing_size;
    engine.edram_last_training_mode = previous.edram_last_training_mode;
    engine.edram_worker_calls = previous.edram_worker_calls;
    engine.edram_retrain_calls = previous.edram_retrain_calls;
    engine.clock_gating_enabled = previous.clock_gating_enabled;
    engine.clock_gating_changes = previous.clock_gating_changes;
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
    draw_submitter = nullptr;
    draw_submitter_context = nullptr;
    engine = {};
    indirect_frames.clear();
    edram_backing.clear();
    for (auto& words : active_shader_microcode) words.clear();
    system_command_buffer.fill(0);
    system_command_buffer_installed = false;
    reset_gpu_registers();
}
std::uint32_t vd_bound_retrain_edram_worker(std::uint32_t mode) {
    std::lock_guard lock(gpu_mutex);
    if (!guest_memory || mode != 0U)
        throw std::runtime_error("Invalid Xenos EDRAM worker invocation");
    ++engine.edram_worker_calls;
    engine.edram_trained = false;
    if (!edram_backing.empty())
        std::fill(edram_backing.begin(), edram_backing.end(), 0);
    return 0;
}
std::uint32_t vd_bound_retrain_edram(
    std::uint32_t mode, std::uint32_t offset_pointer,
    std::uint32_t region_bytes, std::uint32_t status_pointer,
    std::uint32_t scratch_pointer, std::uint32_t scratch_bytes) {
    std::lock_guard lock(gpu_mutex);
    // The original r4/r6 are writable BE32 guest slots, r7 is a
    // 2048-byte scratch region. Reject uncommitted or malformed pointers.
    if ((offset_pointer & 3U) || (status_pointer & 3U) ||
        !region_bytes || region_bytes > kEdramBytes ||
        !scratch_bytes || scratch_bytes > 0x100000U ||
        !readable(offset_pointer, 4U) || !readable(status_pointer, 4U) ||
        !readable(scratch_pointer, scratch_bytes))
        throw std::runtime_error("Invalid Xenos EDRAM retraining guest buffers");
    // Native PC backing replaces the console's physical 10 MiB EDRAM.
    // Check memory integrity, then clear it for future tile/resolve use.
    // This does not implement a GPU renderer or actual Xenos PHY training.
    if (edram_backing.size() != kEdramBytes)
        edram_backing.assign(kEdramBytes, 0);
    for (std::uint32_t i = 0; i < region_bytes; ++i)
        edram_backing[i] = static_cast<std::uint8_t>((i * 131U + 0x5AU) & 0xFFU);
    for (std::uint32_t i = 0; i < region_bytes; ++i) {
        if (edram_backing[i] != static_cast<std::uint8_t>((i * 131U + 0x5AU) & 0xFFU)) {
            engine.edram_trained = false;
            return 1;
        }
    }
    std::fill(edram_backing.begin(), edram_backing.end(), 0);
    engine.edram_trained = true;
    engine.edram_backing_size = kEdramBytes;
    engine.edram_last_training_mode = mode;
    ++engine.edram_retrain_calls;
    // PPC branch at 0x82525868 treats zero as successful retraining.
    // Preserve the caller's output slots, which it initialized itself.
    return 0;
}
std::uint32_t vd_bound_is_hsio_training_succeeded() {
    std::lock_guard lock(gpu_mutex);
    return guest_memory && engine.edram_trained &&
           engine.edram_backing_size == kEdramBytes ? 1U : 0U;
}
std::uint32_t vd_bound_enable_disable_clock_gating(std::uint32_t enabled) {
    std::lock_guard lock(gpu_mutex);
    if (!guest_memory || enabled > 1U)
        throw std::runtime_error("Invalid Xenos GPU clock-gating mode");
    engine.clock_gating_enabled = enabled != 0U;
    ++engine.clock_gating_changes;
    return 0;
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
void vd_bound_install_system_command_buffer(std::uint32_t descriptor_address,
                                           std::uint32_t initial_identifier) {
    std::lock_guard lock(gpu_mutex);
    if (!engine.initialized || !guest_memory)
        throw std::runtime_error("Xbox GPU engines are not initialized");
    if (!descriptor_address) {
        system_command_buffer.fill(0);
        system_command_buffer_installed = false;
        return;
    }
    if ((descriptor_address & 3U) ||
        !readable(descriptor_address, kSystemCommandBufferBytes))
        throw std::runtime_error("Invalid XAM system command buffer descriptor");
    // The descriptor is an opaque XAM-owned 0x94-byte kernel record. Keep
    // the exact guest bytes rather than constructing a fake GPU command.
    std::memcpy(system_command_buffer.data(), guest_memory->address(descriptor_address),
                kSystemCommandBufferBytes);
    system_command_buffer_installed = true;
    engine.system_command_buffer_next_identifier = initial_identifier;
}
std::uint32_t vd_bound_persist_display(std::uint32_t descriptor_address,
                                       std::uint32_t allocation_output) {
    std::lock_guard lock(gpu_mutex);
    constexpr std::uint32_t kDescriptorBytes = 12U;
    constexpr std::uint32_t kPhysicalBytes = 64U;
    const auto output_begin = std::uint64_t(allocation_output);
    const auto output_end = output_begin + 4U;
    const auto descriptor_begin = std::uint64_t(descriptor_address);
    const auto descriptor_end = descriptor_begin + kDescriptorBytes;
    const auto table = std::uint64_t(GuestMemory::kFunctionTableOffset);
    const auto table_end = table + GuestMemory::kFunctionTableSize;
    if (!engine.initialized || (descriptor_address & 3U) ||
        (allocation_output & 3U) ||
        !readable(descriptor_address, kDescriptorBytes) ||
        !readable(allocation_output, 4U) ||
        (output_begin < table_end && output_end > table) ||
        (output_begin < descriptor_end && output_end > descriptor_begin))
        throw std::runtime_error("Invalid Xbox VdPersistDisplay guest arguments");
    // The retail DOTM path expects a *real* physical allocation, which
    // is passed to MmFreePhysicalMemory(type=1) after VdPersistDisplay
    // returns success. Do not invent a pointer or simulate a present.
    const auto physical = mm_bound_allocate_physical_memory_ex(
        1U, kPhysicalBytes, 4U, 0U, 0x1FFFFFFFU, 32U);
    if (!physical)
        return 0U; // physical pool exhausted: report failure without writing.
    write_be32(allocation_output, physical);
    return 1U;
}
void vd_bound_get_system_command_buffer(std::uint32_t descriptor_address,
                                        std::uint32_t identifier_address) {
    std::lock_guard lock(gpu_mutex);
    if (!engine.initialized || (descriptor_address & 3U) ||
        (identifier_address & 3U) ||
        !readable(descriptor_address, kSystemCommandBufferBytes) ||
        !readable(identifier_address, 4U))
        throw std::runtime_error("Invalid Xbox VdGetSystemCommandBuffer output");
    // Retail VdGet advances its fence by two on *every* query, including
    // when XAM has no system commands (see hardware VdSwap probes).
    if (system_command_buffer_installed) {
        std::memcpy(guest_memory->address(descriptor_address),
                    system_command_buffer.data(), kSystemCommandBufferBytes);
    } else {
        std::memset(guest_memory->address(descriptor_address), 0,
                    kSystemCommandBufferBytes);
    }
    write_be32(identifier_address, engine.system_command_buffer_next_identifier);
    engine.system_command_buffer_next_identifier += 2U;
    ++engine.system_command_buffer_queries;
}
void vd_bound_swap(std::uint32_t command_buffer, std::uint32_t texture_fetch,
                   std::uint32_t system_writeback, std::uint32_t system_commands,
                   std::uint32_t gpu_identifier, std::uint32_t frontbuffer_output,
                   std::uint32_t format_output, std::uint32_t color_space_output,
                   std::uint32_t guest_stack) {
    std::lock_guard lock(gpu_mutex);
    constexpr std::uint32_t kCommandWords = 64U;
    constexpr std::uint32_t kFetchWords = 6U;
    constexpr std::uint32_t kFetchRegister = 0x4800U;
    // The ninth and tenth arguments are *pointers* in the PPC caller's
    // stack frame, not immediate dimensions.
    if (!engine.initialized || !guest_memory ||
        (command_buffer & 3U) || (texture_fetch & 3U) ||
        (frontbuffer_output & 3U) || (format_output & 3U) ||
        (color_space_output & 3U) || (guest_stack & 0xFU) ||
        !readable(command_buffer, kCommandWords * 4U) ||
        !readable(texture_fetch, kFetchWords * 4U) ||
        !readable(system_writeback, 4U) ||
        !readable(system_commands, kSystemCommandBufferBytes) ||
        !readable(frontbuffer_output, 4U) ||
        !readable(format_output, 4U) ||
        !readable(color_space_output, 4U) ||
        !readable(guest_stack + 84U, 4U) ||
        !readable(guest_stack + 92U, 4U))
        throw std::runtime_error("Invalid original Xbox VdSwap guest arguments");
    const auto width_output = read_be32(guest_stack + 84U);
    const auto height_output = read_be32(guest_stack + 92U);
    if ((width_output & 3U) || (height_output & 3U) ||
        !readable(width_output, 4U) || !readable(height_output, 4U))
        throw std::runtime_error("Invalid Xbox VdSwap dimension outputs");
    const auto frontbuffer_virtual = read_be32(frontbuffer_output);
    const auto texture_format = read_be32(format_output);
    const auto color_space = read_be32(color_space_output);
    const auto width = read_be32(width_output);
    const auto height = read_be32(height_output);
    std::array<std::uint32_t, kFetchWords> fetch{};
    for (std::uint32_t i = 0; i < kFetchWords; ++i)
        fetch[i] = read_be32(texture_fetch + i * 4U);
    const auto fetch_virtual = (fetch[1] & 0xFFFFF000U);
    const auto fetch_format = fetch[1] & 0x3FU;
    const auto fetch_width = (fetch[2] & 0x1FFFU) + 1U;
    const auto fetch_height = ((fetch[2] >> 13U) & 0x1FFFU) + 1U;
    if ((fetch[0] & 3U) != 2U || !width || !height ||
        width != fetch_width || height != fetch_height ||
        frontbuffer_virtual != fetch_virtual ||
        texture_format != fetch_format || color_space != 0U ||
        (texture_format != 6U && texture_format != 54U))
        throw std::runtime_error("Xbox VdSwap frontbuffer fetch mismatch");
    const auto physical = mm_bound_get_physical_address(frontbuffer_virtual);
    if (!physical ||
        mm_bound_resolve_physical_range(physical, 4U) !=
            std::optional<std::uint32_t>(frontbuffer_virtual) ||
        (physical & 0xFFFU))
        throw std::runtime_error("Xbox VdSwap frontbuffer has no physical backing");
    // Convert the *real* D3D texture header's virtual page address into
    // a GPU physical fetch address. Keep the original format, pitch, tiling,
    // dimensions and sampling fields unchanged.
    fetch[1] = (fetch[1] & 0xFFFU) | (physical & 0xFFFFF000U);
    // The original caller reserves 64 DWORDs. Publish a real PM4 TYPE0
    // six-register fetch update followed by valid TYPE2 no-op packets.
    // There is no native Xenos "swap" PM4 opcode: do not inject Xenia's
    // synthetic XE_SWAP or pretend that a frame has been displayed.
    write_be32(command_buffer, ((kFetchWords - 1U) << 16U) | kFetchRegister);
    for (std::uint32_t i = 0; i < kFetchWords; ++i)
        write_be32(command_buffer + (i + 1U) * 4U, fetch[i]);
    for (std::uint32_t i = kFetchWords + 1U; i < kCommandWords; ++i)
        write_be32(command_buffer + i * 4U, 0x80000000U);
    ++engine.swap_requests;
    engine.last_swap_physical_address = physical;
    engine.last_swap_width = width;
    engine.last_swap_height = height;
    engine.last_swap_texture_format = texture_format;
    (void)gpu_identifier;
}
std::uint32_t vd_bound_initialize_scaler_command_buffer(
    std::uint32_t source_xy, std::uint32_t source_wh,
    std::uint32_t output_xy, std::uint32_t output_wh,
    std::uint32_t frontbuffer_wh, std::uint32_t vertical_filter,
    std::uint32_t vertical_parameters, std::uint32_t horizontal_filter,
    std::uint32_t guest_stack) {
    std::lock_guard lock(gpu_mutex);
    // XDK PowerPC ABI: r3-r10, followed by four stack-spilled arguments.
    // The original DOTM function at 0x82520070 initializes a 200-DWORD
    // buffer with NOPs, then copies the returned DWORD count. A direct
    // 1:1 scanout requires no scaler GPU commands, so return zero words.
    // Never claim non-identity scaling or hardware presentation works.
    if (!engine.initialized || !engine.display_mode_changes ||
        (guest_stack & 15U) ||
        !readable(std::uint64_t(guest_stack) + 84U, 28U))
        throw std::runtime_error("Invalid Xbox scaler PPC stack arguments");
    const auto horizontal_parameters = read_be32(guest_stack + 84U);
    const auto unknown9 = read_be32(guest_stack + 92U);
    const auto command_buffer = read_be32(guest_stack + 100U);
    const auto command_capacity = read_be32(guest_stack + 108U);
    if (!command_capacity || command_capacity > 4096U ||
        (command_buffer & 3U) ||
        !readable(command_buffer, std::size_t(command_capacity) * 4U) ||
        (vertical_parameters && !readable(vertical_parameters, 12U)) ||
        (horizontal_parameters && !readable(horizontal_parameters, 12U)) ||
        (unknown9 && !readable(unknown9, 4U)))
        throw std::runtime_error("Invalid Xbox scaler guest buffers");
    // Original DOTM at PPC 0x825201EC passes 0x050002D0 for its 1280x720
    // source/output/frontbuffer. The XDK packs width in the high 16 bits,
    // height in the low 16 bits (opposite the earlier synthetic fixture).
    const auto width = source_wh >> 16U;
    const auto height = source_wh & 0xFFFFU;
    if (source_xy != output_xy || source_wh != output_wh ||
        source_wh != frontbuffer_wh || source_xy != 0U ||
        !width || !height || width != engine.display_mode_width ||
        height != engine.display_mode_height ||
        (vertical_filter != 0U && vertical_filter != 1U &&
         vertical_filter != 7U) ||
        (horizontal_filter != 0U && horizontal_filter != 1U &&
         horizontal_filter != 7U))
        throw std::runtime_error(
            "Xbox VdInitializeScalerCommandBuffer: native non-identity scaler not implemented"
            " source_xy=" + std::to_string(source_xy) +
            " source_wh=" + std::to_string(source_wh) +
            " output_xy=" + std::to_string(output_xy) +
            " output_wh=" + std::to_string(output_wh) +
            " frontbuffer_wh=" + std::to_string(frontbuffer_wh) +
            " vertical_filter=" + std::to_string(vertical_filter) +
            " horizontal_filter=" + std::to_string(horizontal_filter));
    // Retail DOTM passes filter mode 7 even for its 1:1 720p scanout.
    // No resampling occurs when source and destination rectangles match;
    // accept that mode only in this verified identity path. This does not
    // implement a non-identity 7-tap scaler or GPU presentation.
    // Leave the caller's buffer untouched, return 0 DWORDs and preserve
    // the actual source dimensions for the future native GPU presenter.
    engine.identity_scanout_configured = true;
    ++engine.identity_scanout_requests;
    engine.identity_scanout_width = width;
    engine.identity_scanout_height = height;
    return 0U;
}
std::uint32_t vd_bound_set_display_mode(std::uint32_t flags) {
    std::lock_guard lock(gpu_mutex);
    // DOTM passes 0x40000000 for its 1280x720 RGB mode. Bit 27 selects
    // the 10:10:10:2 texture format; the SD and non-RGB paths require
    // hardware-specific scaler handling and must not silently succeed.
    if (!engine.initialized || (flags & 0x40000000U) == 0U ||
        (flags & ~0x78000002U) || (flags & 0x30000002U))
        throw std::runtime_error("Unsupported Xbox VdSetDisplayMode configuration");
    engine.display_mode_flags = flags;
    engine.display_mode_width = 1280U;
    engine.display_mode_height = 720U;
    ++engine.display_mode_changes;
    return 0U; // Xbox kernel STATUS_SUCCESS
}
void vd_bound_get_current_display_information(std::uint32_t output) {
    std::lock_guard lock(gpu_mutex);
    constexpr std::uint32_t kBytes = 0x58U;
    const auto begin = std::uint64_t(output);
    const auto end = begin + kBytes;
    const auto table = std::uint64_t(GuestMemory::kFunctionTableOffset);
    if (!engine.initialized || !engine.display_mode_changes ||
        (output & 3U) || !readable(output, kBytes) ||
        (begin < table + GuestMemory::kFunctionTableSize && end > table))
        throw std::runtime_error("Invalid Xbox VdGetCurrentDisplayInformation output");
    std::memset(guest_memory->address(output), 0, kBytes);
    const auto write16 = [&](std::uint32_t offset, std::uint16_t value) {
        auto* p = guest_memory->address(output + offset);
        p[0] = static_cast<std::uint8_t>(value >> 8U);
        p[1] = static_cast<std::uint8_t>(value);
    };
    const auto width = static_cast<std::uint16_t>(engine.display_mode_width);
    const auto height = static_cast<std::uint16_t>(engine.display_mode_height);
    write16(0x00U, width);             // front buffer width
    write16(0x02U, height);            // front buffer height
    guest_memory->address(output)[4] = static_cast<std::uint8_t>(
        (engine.display_mode_flags & 0x08000000U) ? 54U : 6U);
    write_be32(output + 0x10U, width);  // scaler source rectangle x2
    write_be32(output + 0x14U, height); // scaler source rectangle y2
    write_be32(output + 0x18U, width);  // scaler output width
    write_be32(output + 0x1CU, height); // scaler output height
    write_be32(output + 0x20U, 1U);    // vertical filter type
    write_be32(output + 0x30U, 1U);    // horizontal filter type
    write16(0x48U, width);             // display width (DOTM reads at SP+440)
    write16(0x4AU, height);            // display height (SP+442)
    write_be32(output + 0x4CU, 0x42700000U); // 60 Hz IEEE754 BE float
    write16(0x56U, width);             // actual display width (SP+454)
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
    // CP_RB_RPTR (0x710) is CPU-visible MMIO, not just the optional
    // physical writeback slot. Reset both views when a new ring is bound.
    write_gpu_register(0x01C4U, 0U);
    indirect_frames.clear();
    engine.packets_executed = 0;
    engine.draws_submitted = 0;
    engine.last_draw_initiator = 0;
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
    const auto ring_capacity = engine.ring_size_bytes / 4;
    if (write_pointer_dwords >= ring_capacity ||
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
        if (!indirect_frames.empty()) {
            const auto& frame = indirect_frames.back();
            if (index >= frame.word_count)
                throw std::runtime_error("Xenos indirect buffer read out of range");
            return read_be32(frame.guest_address + index * 4U);
        }
        return read_be32(engine.ring_guest_address +
                         (index % ring_capacity) * 4U);
    };
    std::uint32_t completed = 0;
    while ((!indirect_frames.empty() ||
            engine.ring_read_pointer_dwords != write_pointer_dwords) &&
           completed < max_packets) {
        if (!indirect_frames.empty()) {
            const auto& frame = indirect_frames.back();
            const auto bytes = std::size_t(frame.word_count) * 4U;
            if (mm_bound_resolve_physical_range(frame.physical_address, bytes) !=
                    std::optional<std::uint32_t>(frame.guest_address) ||
                !readable(frame.guest_address, bytes))
                throw std::runtime_error("Xenos indirect buffer physical pages released");
            if (frame.read_offset == frame.word_count) {
                indirect_frames.pop_back();
                // The parent opcode is three DWORDs: header, address, length.
                if (indirect_frames.empty()) {
                    engine.ring_read_pointer_dwords =
                        (engine.ring_read_pointer_dwords + 3U) % ring_capacity;
                    write_gpu_register(0x01C4U, engine.ring_read_pointer_dwords);
                    if (engine.read_pointer_writeback_enabled)
                        write_be32(engine.read_pointer_writeback_guest_address,
                                   engine.ring_read_pointer_dwords);
                } else {
                    indirect_frames.back().read_offset += 3U;
                }
                ++engine.packets_executed;
                ++completed;
                continue;
            }
        }
        const bool indirect = !indirect_frames.empty();
        const auto capacity = indirect
            ? indirect_frames.back().word_count : ring_capacity;
        const auto start = indirect
            ? indirect_frames.back().read_offset : engine.ring_read_pointer_dwords;
        const auto available = indirect
            ? capacity - start
            : (write_pointer_dwords + ring_capacity - start) % ring_capacity;
        const auto header = at(start);
        const auto type = header >> 30;
        const auto count = ((header >> 16) & 0x3FFFU) + 1;
        // Retail Xenos treats an all-zero PM4 dword as one-word padding.
        // It is not a type-0 register-zero write with a payload; otherwise
        // zero-padded indirect lists lose alignment and read payload as headers.
        const auto payload = header == 0U || type == 2 ? 0U : type == 1 ? 2U : count;
        // A PM4 packet cannot occupy more than the entire physical ring.
        // A malformed count at or above capacity can never become complete:
        // without this check every future GPU kick silently stalls forever.
        // Reject it without consuming the header or touching RPTR writeback.
        if (payload >= capacity && (!indirect || payload > capacity))
            throw std::runtime_error("Xenos PM4 packet exceeds ring capacity");
        // A CPU producer may publish its write pointer before the final
        // payload dword arrives. Preserve the header, read pointer and
        // writeback until a later kick supplies the entire packet. This is
        // normal producer/consumer flow, not a malformed GPU command.
        if (payload >= available) {
            if (!indirect) return completed;
            auto& frame = indirect_frames.back();
            const auto needed = std::uint64_t(start) + payload + 1U;
            const auto bytes = std::size_t(needed) * 4U;
            // The retail title's 74-dword IB ends with a type-0 packet
            // needing one final dword. Finish that packet only if its
            // extra word belongs to the SAME allocated physical range.
            // Larger overruns remain errors; no unrelated memory is read.
            if (needed != std::uint64_t(frame.word_count) + 1U ||
                mm_bound_resolve_physical_range(frame.physical_address, bytes) !=
                    std::optional<std::uint32_t>(frame.guest_address) ||
                !readable(frame.guest_address, bytes))
                throw std::runtime_error("Truncated Xenos indirect PM4 packet");
            frame.word_count = static_cast<std::uint32_t>(needed);
        }
        const auto first = indirect ? start + 1U : (start + 1U) % ring_capacity;
        if (header == 0U) {
            // One-word Xenos padding NOP; never touch register zero.
        } else if (type == 0) {
            const auto base = header & 0x7FFFU;
            const bool repeat = (header & 0x8000U) != 0;
            if (base >= gpu_registers.size() ||
                (!repeat && std::uint64_t(base) + payload > gpu_registers.size()))
                throw std::runtime_error("Xenos PM4 type-0 register out of range");
            // Fetch the packet into a stable FIFO snapshot before any DMA
            // write can alias the ring and overwrite a later payload word.
            std::vector<std::uint32_t> values;
            values.reserve(payload);
            for (std::uint32_t i = 0; i < payload; ++i)
                values.push_back(at(first + i));
            validate_gpu_register_packet(
                payload, [&](std::uint32_t i) { return base + (repeat ? 0U : i); },
                [&](std::uint32_t i) { return values[i]; });
            for (std::uint32_t i = 0; i < payload; ++i)
                write_gpu_register(base + (repeat ? 0U : i), values[i]);
            engine.register_writes += payload;
        } else if (type == 1) {
            const auto r0 = header & 0x7FFU;
            const auto r1 = (header >> 11) & 0x7FFU;
            const auto value0 = at(first);
            const auto value1 = at(first + 1);
            validate_gpu_register_packet(
                2U, [&](std::uint32_t i) { return i ? r1 : r0; },
                [&](std::uint32_t i) { return i ? value1 : value0; });
            write_gpu_register(r0, value0);
            write_gpu_register(r1, value1);
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
            } else if (opcode == 0x3FU || opcode == 0x37U) {
                // PM4_INDIRECT_BUFFER / PM4_INDIRECT_BUFFER_PFD reference
                // real allocated GPU physical memory. Preserve the parent's
                // cursor until the entire nested command list has executed.
                if (payload != 2U)
                    throw std::runtime_error("Invalid Xenos indirect buffer packet size");
                const auto physical = at(first);
                const auto length_field = at(first + 1U);
                const auto words = length_field & 0xFFFFFU;
                if ((physical & 3U) || !words ||
                    (length_field & ~0xFFFFFU) ||
                    indirect_frames.size() >= kMaxIndirectDepth)
                    throw std::runtime_error("Invalid Xenos indirect buffer address, length or depth");
                const auto bytes = std::size_t(words) * 4U;
                const auto guest = mm_bound_resolve_physical_range(physical, bytes);
                if (!guest || !readable(*guest, bytes))
                    throw std::runtime_error("Xenos indirect buffer requires allocated physical memory");
                indirect_frames.push_back({physical, *guest, words, 0U});
                continue;
            } else if (opcode == 0x36U || opcode == 0x22U) {
                // Original Xenos DRAW_INDX_2 / DRAW_INDX. Decode the
                // hardware VGT_DRAW_INITIATOR, not an invented PC draw.
                // DRAW_INDX carries an additional visibility-query token.
                const bool has_query = opcode == 0x22U;
                const auto initiator_offset = has_query ? 1U : 0U;
                if (payload <= initiator_offset)
                    throw std::runtime_error("Truncated Xenos DRAW_INDX initiator");
                GpuDrawCommand draw;
                // Forward the unmodified query token to the native backend.
                // Do not reject a valid conditional draw or guess its result:
                // the backend must evaluate the actual visibility query.
                draw.has_visibility_query_token = has_query;
                if (has_query) draw.visibility_query_token = at(first);
                draw.initiator = at(first + initiator_offset);
                draw.primitive_type = draw.initiator & 0x3FU;
                draw.source_select = (draw.initiator >> 6U) & 3U;
                draw.major_mode = (draw.initiator >> 8U) & 3U;
                draw.index_size_bytes = (draw.initiator & 0x800U) ? 4U : 2U;
                draw.index_count = draw.initiator >> 16U;
                const auto arguments = payload - initiator_offset - 1U;
                std::uint32_t dma_size = 0U;
                if (draw.source_select == 0U) {
                    if (arguments != 2U)
                        throw std::runtime_error("Invalid Xenos DMA indexed-draw packet size");
                    const auto raw_base = at(first + initiator_offset + 1U);
                    dma_size = at(first + initiator_offset + 2U);
                    draw.dma_physical_address = raw_base & ~(draw.index_size_bytes - 1U);
                    draw.dma_swap_mode = dma_size >> 30U;
                    if (draw.index_count > (dma_size & 0xFFFFFFU))
                        throw std::runtime_error("Xenos DMA draw exceeds index-buffer bounds");
                    const auto bytes = std::size_t(draw.index_count) * draw.index_size_bytes;
                    // Xenos 8-in-32 and 16-in-32 endian modes permute whole
                    // physical DWORD lanes, including a trailing odd 16-bit
                    // index. Fetch the complete final lane, never invent its
                    // missing bytes or decode only half a swapped DWORD.
                    const auto fetch_bytes = draw.dma_swap_mode >= 2U
                        ? (bytes + 3U) & ~std::size_t(3U) : bytes;
                    if (fetch_bytes) {
                        if (draw.dma_swap_mode >= 2U &&
                            (draw.dma_physical_address & 3U))
                            throw std::runtime_error("Xenos dword-swapped index DMA is unaligned");
                        const auto guest = mm_bound_resolve_physical_range(
                            draw.dma_physical_address, fetch_bytes);
                        if (!guest || !readable(*guest, fetch_bytes))
                            throw std::runtime_error("Xenos draw index data requires allocated physical memory");
                        const auto* data = guest_memory->address(*guest);
                        draw.dma_index_bytes.assign(data, data + fetch_bytes);
                    }
                    draw.decoded_indices = decode_xenos_index_buffer(
                        draw.dma_index_bytes, draw.index_count,
                        draw.index_size_bytes, draw.dma_swap_mode);
                } else if (draw.source_select == 2U) {
                    if (arguments != 0U)
                        throw std::runtime_error("Invalid Xenos auto-index draw packet size");
                } else {
                    // Immediate indices and reserved source modes must not
                    // be silently interpreted as a successful draw.
                    throw std::runtime_error("Unsupported Xenos draw index source");
                }
                if (!draw_submitter) {
                    // Report the original PM4 draw and active shader state.
                    // Never acknowledge it or pretend that a frame rendered.
                    // This makes the first unsupported retail draw actionable
                    // without copying proprietary shader code into the log.
                    char diagnostic[320]{};
                    std::snprintf(diagnostic, sizeof(diagnostic),
                        "Xenos draw requires a native graphics backend "
                        "(opcode=0x%02X, initiator=0x%08X, primitive=%u, "
                        "source=%u, indices=%u, VS_dwords=%zu, PS_dwords=%zu)",
                        opcode, draw.initiator, draw.primitive_type,
                        draw.source_select, draw.index_count,
                        active_shader_microcode[0].size(),
                        active_shader_microcode[1].size());
                    throw std::runtime_error(diagnostic);
                }
                draw.register_words.reserve(gpu_registers.size());
                for (std::uint32_t i = 0; i < gpu_registers.size(); ++i)
                    draw.register_words.push_back(read_gpu_register(i));
                draw.register_words[0x21FCU] = draw.initiator;
                draw.vertex_fetch_constants = decode_xenos_vertex_fetch_constants(
                    draw.register_words);
                draw.texture_fetch_constants = decode_xenos_texture_fetch_constants(
                    draw.register_words);
                if (draw.source_select == 0U) {
                    draw.register_words[0x21FAU] = draw.dma_physical_address;
                    draw.register_words[0x21FBU] = dma_size;
                }
                draw.vertex_shader_words = active_shader_microcode[0];
                draw.pixel_shader_words = active_shader_microcode[1];
                draw.copy_physical_memory = &copy_draw_physical_bytes;
                // The scope closes on both success and exception. The GPU
                // packet remains pending if the backend cannot fetch data.
                ScopedDrawReadWindow read_window;
                if (!draw_submitter(draw, draw_submitter_context))
                    throw std::runtime_error("Native graphics backend rejected Xenos draw");
                // Only a successful real backend submission can advance
                // RPTR. Presentation and frame completion are separate.
                write_gpu_register(0x21FCU, draw.initiator);
                ++engine.register_writes;
                if (draw.source_select == 0U) {
                    write_gpu_register(0x21FAU, draw.dma_physical_address);
                    write_gpu_register(0x21FBU, dma_size);
                    engine.register_writes += 2U;
                }
                ++engine.draws_submitted;
                engine.last_draw_initiator = draw.initiator;
            } else if (opcode == 0x3BU) {
                // PM4_INVALIDATE_STATE: synchronize shared physical memory
                // and retain the actual state-pointer invalidation mask.
                // A future renderer must apply this mask to its GPU caches.
                if (payload != 1U)
                    throw std::runtime_error("Invalid Xenos PM4 INVALIDATE_STATE payload size");
                const auto mask = at(first);
                std::atomic_thread_fence(std::memory_order_seq_cst);
                engine.last_state_invalidation_mask = mask;
                ++engine.state_invalidations;
            } else if (opcode == 0x5EU) {
                // Xenos PM4_CONTEXT_UPDATE. Retail hardware accepts the
                // one-DWORD context update command with value zero. This
                // updates the command processor context without a draw or
                // fake presentation. Reject unverified nonzero modes.
                if (payload != 1U)
                    throw std::runtime_error("Invalid Xenos CONTEXT_UPDATE packet size");
                if (at(first) != 0U)
                    throw std::runtime_error("Unsupported Xenos CONTEXT_UPDATE mode");
            } else if (opcode == 0x10U) {
                // PM4_NOP has payload but does not change GPU state.
            } else if (opcode == 0x46U) {
                // PM4_EVENT_WRITE with one initiator DWORD raises an Xenos
                // VGT event without a memory destination. The 0x21F9 register
                // is CPU-visible through the mapped GPU MMIO aperture.
                // Multiword variants require actual completion/DMA semantics;
                // do not acknowledge them until implemented.
                if (payload != 1U)
                    throw std::runtime_error("Unsupported Xenos EVENT_WRITE memory variant");
                write_gpu_register(0x21F9U, at(first) & 0x3FU);
                ++engine.register_writes;
            } else if (opcode == 0x58U) {
                // PM4_EVENT_WRITE_SHD: shader-done event, physical fence
                // address (with Xenos dword endian selector), fence value.
                // VS_DONE_TS=2 and PS_DONE_TS=3 are verified hardware events.
                // Other events require independent GPU timing semantics.
                if (payload != 3U)
                    throw std::runtime_error("Invalid Xenos EVENT_WRITE_SHD payload size");
                const auto initiator = at(first);
                const auto encoded_address = at(first + 1);
                const auto fence_value = at(first + 2);
                const auto event = initiator & 0x7FFFFFFFU;
                if (event != 2U && event != 3U)
                    throw std::runtime_error("Unsupported Xenos shader-done event");
                // Initiator bit 31 selects the GPU swap counter rather than
                // the immediate payload. Without presented frames this stays
                // zero; never invent a completed frame or fence.
                const auto result = (initiator & 0x80000000U)
                    ? engine.gpu_frame_counter : fence_value;
                // Resolve the entire physical destination before consuming
                // the packet, preserving RPTR and writeback on invalid DMA.
                (void)gpu_physical_dword_address(encoded_address);
                write_gpu_physical_dword(encoded_address, result);
                // VGT_EVENT_INITIATOR is a live Xenos register, not just a
                // trace annotation. Update it only after DMA has succeeded.
                write_gpu_register(0x21F9U, event);
                ++engine.register_writes;
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
                    if (poll_address == kCoherStatusHost &&
                        read_gpu_register(kCoherStatusHost) != 0U) {
                        // Direct original PPC MMIO writes bypass
                        // write_gpu_register(), so they do not set our
                        // synthetic pending bit 31. A nonzero coherency
                        // request must complete regardless of whether the
                        // guest submitted it through PM4 or CPU MMIO.
                        // Native physical pages share committed host backing;
                        // a future renderer must flush its resource caches.
                        std::atomic_thread_fence(std::memory_order_seq_cst);
                        gpu_registers[kCoherStatusHost] = 0U;
                        const auto mmio = kXenosMmioBase +
                                          kCoherStatusHost * 4U;
                        if (readable(mmio, 4U))
                            write_be32(mmio, 0U);
                    }
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
            } else if (opcode == 0x27U || opcode == 0x2BU) {
                // Xenos PM4_IM_LOAD / PM4_IM_LOAD_IMMEDIATE. Preserve exact
                // sequencer instructions for native shader translation.
                const bool immediate = opcode == 0x2BU;
                if (payload < 2U || (!immediate && payload != 2U))
                    throw std::runtime_error("Invalid Xenos shader upload packet size");
                const auto address_or_stage = at(first);
                const auto start_size = at(first + 1U);
                const auto stage = immediate ? address_or_stage :
                    (address_or_stage & 3U);
                const auto start = start_size >> 16U;
                const auto words = start_size & 0xFFFFU;
                if (stage > 1U || words == 0U ||
                    std::uint64_t(start) + words > 0x10000U ||
                    (immediate && std::uint64_t(words) + 2U != payload))
                    throw std::runtime_error("Invalid Xenos shader stage or microcode length");
                // The high 16 bits select the instruction offset. An
                // IM_LOAD starting at a nonzero offset updates the already
                // loaded shader rather than discarding its preceding words.
                // Reject holes: translating uninitialized instructions would
                // silently create a different shader than the original game.
                const auto& previous = active_shader_microcode[stage];
                if (start && start > previous.size())
                    throw std::runtime_error("Xenos shader upload skips uninitialized instructions");
                std::vector<std::uint32_t> incoming;
                incoming.reserve(words);
                if (immediate) {
                    // Snapshot FIFO before any subsequent GPU DMA can modify it.
                    for (std::uint32_t i = 0; i < words; ++i)
                        incoming.push_back(at(first + 2U + i));
                } else {
                    const auto physical = address_or_stage & ~3U;
                    const auto bytes = words * 4U;
                    const auto guest = mm_bound_resolve_physical_range(physical, bytes);
                    if (!guest || !readable(*guest, bytes))
                        throw std::runtime_error("Xenos shader source is not allocated physical memory");
                    for (std::uint32_t i = 0; i < words; ++i)
                        incoming.push_back(read_be32(*guest + i * 4U));
                }
                // Assemble and hash the complete instruction image. Commit
                // only after all physical-memory reads and validation succeed.
                std::vector<std::uint32_t> shader = start
                    ? previous : std::vector<std::uint32_t>{};
                shader.resize(std::max(shader.size(), std::size_t(start) + words));
                std::copy(incoming.begin(), incoming.end(), shader.begin() + start);
                auto fingerprint = kPacketHashOffset;
                for (const auto word : shader)
                    fingerprint = hash_packet_word(fingerprint, word);
                const auto total_words = static_cast<std::uint32_t>(shader.size());
                active_shader_microcode[stage].swap(shader);
                ++engine.shader_uploads;
                if (stage == 0U) {
                    engine.vertex_shader_dwords = total_words;
                    engine.vertex_shader_fingerprint = fingerprint;
                } else {
                    engine.pixel_shader_dwords = total_words;
                    engine.pixel_shader_fingerprint = fingerprint;
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
                // Constant writes may target SCRATCH_REG0..7. Those registers
                // perform physical DMA when SCRATCH_UMSK is enabled, so a
                // later invalid fence must not partially commit this packet.
                // Snapshot the FIFO first: scratch DMA may alias a subsequent
                // word of the packet currently being consumed.
                const auto word_count = payload - 1U;
                std::vector<std::uint32_t> values;
                values.reserve(word_count);
                for (std::uint32_t i = 0; i < word_count; ++i)
                    values.push_back(at(first + i + 1U));
                validate_gpu_register_packet(
                    word_count,
                    [&](std::uint32_t i) { return index + i; },
                    [&](std::uint32_t i) { return values[i]; });
                for (std::uint32_t i = 0; i < word_count; ++i)
                    write_gpu_register(index + i, values[i]);
                engine.register_writes += word_count;
            } else {
                throw std::runtime_error("Unsupported Xenos PM4 type-3 opcode " + std::to_string(opcode));
            }
        }
        if (indirect) {
            indirect_frames.back().read_offset = start + payload + 1U;
        } else {
            engine.ring_read_pointer_dwords =
                (start + payload + 1U) % ring_capacity;
            // PPC can poll CP_RB_RPTR directly, even with DMA writeback
            // disabled. Publish only after the whole packet completes.
            write_gpu_register(0x01C4U, engine.ring_read_pointer_dwords);
            if (engine.read_pointer_writeback_enabled)
                write_be32(engine.read_pointer_writeback_guest_address,
                           engine.ring_read_pointer_dwords);
        }
        ++engine.packets_executed;
        ++completed;
    }
    return completed;
}
std::uint32_t vd_bound_drain_mmio_ring(std::uint32_t max_packets) {
    // CP_RB_WPTR is register 0x01C5 (byte offset 0x714). The original PPC
    // driver updates its MMIO page directly, not via a kernel import.
    const auto state = vd_bound_gpu_snapshot();
    if (!state.ring_initialized || !max_packets) return 0U;
    const auto write_pointer = vd_bound_read_gpu_mmio_register(0x01C5U);
    if (write_pointer == state.ring_read_pointer_dwords) return 0U;
    return vd_bound_execute_ring(write_pointer, max_packets);
}
std::vector<std::uint32_t> vd_bound_copy_shader_microcode(std::uint32_t stage) {
    std::lock_guard lock(gpu_mutex);
    if (stage > 1U)
        throw std::runtime_error("Invalid Xenos shader stage");
    return active_shader_microcode[stage];
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