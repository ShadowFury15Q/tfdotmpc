#include "compiled_ppc_adapter.hpp"
#include "guest_memory.hpp"
#include "guest_virtual_allocator.hpp"
#include "kernel_guest_memory.hpp"
#include "kernel_critical_sections.hpp"
#include "kernel_rtl_memory.hpp"
#include "xex_optional_header.hpp"
#include "xex_title_boot.hpp"
#include "native_input.hpp"
#include "ppc_recomp_shared.h" // Includes the generated PPC configuration and context
#include "xam_guest_input.hpp"
#include "xam_launch_data.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <atomic>
#include <thread>
#include <chrono>
#include <stdexcept>
#ifdef DOTM_SYNTHETIC_PRIVATE_BOOT
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>
#endif

namespace {
constexpr std::uint32_t kStackBase = 0x70000000U;
constexpr std::uint32_t kStackBytes = 0x00100000U;
constexpr std::uint32_t kInitialStackPointer = kStackBase + kStackBytes - 0x100U;

void unresolved_indirect(PPCContext&, std::uint8_t*) {
    throw std::runtime_error(
        "Unimplemented indirect PPC target; native game execution is blocked");
}

// The current host bindings implement only a subset of Xbox 360 imports.
// Keep them alive for the entire guest entry call, including exception paths.
struct ScopedHostBindings {
    ScopedHostBindings(dotm::GuestMemory& memory,
                       dotm::GuestVirtualAllocator& allocator,
                       dotm::XamInputHost& input,
                       dotm::XamLaunchDataHost& launch_data) {
        dotm::bind_kernel_guest_memory(&memory, &allocator);
        dotm::bind_kernel_critical_sections(&memory);
        dotm::bind_kernel_rtl_memory(&memory);
        dotm::bind_xex_optional_header(&memory);
        dotm::bind_xex_system_modules(&memory);
        dotm::bind_xam_guest_input(&input, &memory);
        dotm::bind_xam_launch_data(&launch_data, &memory);
    }
    ~ScopedHostBindings() {
        dotm::bind_xam_launch_data(nullptr, nullptr);
        dotm::bind_xam_guest_input(nullptr, nullptr);
        dotm::bind_xex_system_modules(nullptr);
        dotm::bind_xex_optional_header(nullptr);
        dotm::bind_kernel_rtl_memory(nullptr);
        dotm::bind_kernel_critical_sections(nullptr);
        dotm::bind_kernel_guest_memory(nullptr, nullptr);
    }
    ScopedHostBindings(const ScopedHostBindings&) = delete;
    ScopedHostBindings& operator=(const ScopedHostBindings&) = delete;
};

#ifdef DOTM_SYNTHETIC_PRIVATE_BOOT
// Exercise the real PE loading and entry-verification path in public CI.
// This PE contains only generated test bytes, never original game content.
void load_synthetic_boot_pe(dotm::GuestMemory& memory) {
    std::vector<std::uint8_t> pe(0x600, 0);
    const auto put16 = [&](std::size_t offset, std::uint16_t value) {
        pe[offset] = static_cast<std::uint8_t>(value);
        pe[offset + 1] = static_cast<std::uint8_t>(value >> 8);
    };
    const auto put32 = [&](std::size_t offset, std::uint32_t value) {
        for (int i = 0; i < 4; ++i)
            pe[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
    };
    put16(0, 0x5A4D);
    put32(0x3C, 0x80);
    put32(0x80, 0x4550);
    put16(0x84, 0x01F2);
    put16(0x86, 1);
    put16(0x94, 0xE0);
    put16(0x98, 0x10B);
    put32(0x98 + 16, 0x0077F670U);
    put32(0x98 + 28, dotm::GuestMemory::kImageBase);
    put32(0x98 + 60, 0x200);
    std::memcpy(pe.data() + 0x178, ".text", 5);
    put32(0x178 + 8, 0x1000);
    put32(0x178 + 12, 0x0077F000U);
    put32(0x178 + 16, 0x100);
    put32(0x178 + 20, 0x200);

    const auto path = std::filesystem::temp_directory_path() /
        ("dotm_synthetic_boot_" +
         std::to_string(reinterpret_cast<std::uintptr_t>(&memory)) + "_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
         ".pe");
    struct RemoveTemporaryFile {
        std::filesystem::path path;
        ~RemoveTemporaryFile() {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    } cleanup{path};
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output.write(reinterpret_cast<const char*>(pe.data()),
                          static_cast<std::streamsize>(pe.size())))
            throw std::runtime_error("Cannot create synthetic PPC PE fixture");
    }
    memory.load_pe(path);
}
#endif

void execute_guest_entry(dotm::GuestMemory& memory, std::uint32_t entry) {
    // Reserve a dedicated 1 MiB guest stack outside the NT heap range.
    // Xbox startup/TLS, graphics and audio imports are not yet implemented.
    if (memory.any_committed_range(kStackBase, kStackBytes))
        throw std::runtime_error("Guest boot stack overlaps committed memory");
    memory.commit(kStackBase, kStackBytes);
    std::memset(memory.address(kStackBase), 0, kStackBytes);

    // Retail Xbox CRT heap occupies the 64 KiB virtual-page region.
    dotm::GuestVirtualAllocator allocator(memory, 0x40000000U, 0x70000000U);
    dotm::NativeInput native_input;
    dotm::XamInputHost xam_input;
    xam_input.publish(native_input.next_frame());
    dotm::XamLaunchDataHost launch_data;
    ScopedHostBindings bindings(memory, allocator, xam_input, launch_data);
#ifndef DOTM_SYNTHETIC_PRIVATE_BOOT
    // KeTimeStampBundle is a kernel data import. Its millisecond tick must
    // advance while the original PPC code runs, even without host calls.
    constexpr std::uint32_t kTimestampTickAddress = 0x81904010U;
    if (!memory.committed_range(kTimestampTickAddress, sizeof(std::uint32_t)))
        throw std::runtime_error("Original KeTimeStampBundle was not initialized");
    std::jthread kernel_timestamp([&memory](std::stop_token stop) {
        const auto start = std::chrono::steady_clock::now();
        auto& raw = *reinterpret_cast<std::uint32_t*>(
            memory.address(kTimestampTickAddress));
        while (!stop.stop_requested()) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            const auto ticks = static_cast<std::uint32_t>(elapsed);
            const auto big_endian = ((ticks & 0x000000FFU) << 24) |
                ((ticks & 0x0000FF00U) << 8) |
                ((ticks & 0x00FF0000U) >> 8) |
                ((ticks & 0xFF000000U) >> 24);
            std::atomic_ref<std::uint32_t>(raw).store(
                big_endian, std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
#endif

#ifndef DOTM_SYNTHETIC_PRIVATE_BOOT
    // The PPC r13 ABI points to KPCR. KPCR+0x100 is current KTHREAD;
    // KTHREAD+0x14C holds the main thread ID.
    constexpr std::uint32_t kPcr = 0x71000000U;
    constexpr std::uint32_t kKthread = 0x71001000U;
    constexpr std::uint32_t kTls = 0x71002000U;
    if (memory.any_committed_range(kPcr, 0x3000))
        throw std::runtime_error("Main title thread KPCR overlaps guest state");
    memory.commit(kPcr, 0x3000);
    const auto write_guest32 = [&](std::uint32_t address, std::uint32_t value) {
        auto* p = memory.address(address);
        for (int i=0;i<4;++i) p[i] = std::uint8_t(value >> (24-8*i));
    };
    write_guest32(kPcr, kTls);
    write_guest32(kPcr + 0x30U, kPcr);
    write_guest32(kPcr + 0x70U, kStackBase + kStackBytes);
    write_guest32(kPcr + 0x74U, kStackBase);
    write_guest32(kPcr + 0x100U, kKthread);
    write_guest32(kKthread + 0x5CU, kStackBase + kStackBytes);
    write_guest32(kKthread + 0x60U, kStackBase);
    write_guest32(kKthread + 0x68U, kTls);
    write_guest32(kKthread + 0x14CU, 1U);
#endif
    PPCContext context{};
#ifndef DOTM_SYNTHETIC_PRIVATE_BOOT
    context.r13.u64 = kPcr;
#endif
    context.r1.u64 = kInitialStackPointer;
    std::printf("Executing original PPC entry 0x%08X, guest SP 0x%08X\n",
                entry, kInitialStackPointer);
    std::fflush(stdout);
    memory.find_function(entry)(context, memory.base());
#ifdef DOTM_SYNTHETIC_PRIVATE_BOOT
    if (context.calls != 1 ||
        memory.address(kInitialStackPointer - 16)[0] != 0x42)
        throw std::runtime_error("Synthetic PPC entry did not execute on committed guest stack");
#endif
}
}

int main(int argc, char** argv) {
    const bool link_only = argc == 1;
    const bool run_original =
        argc == 5 && std::strcmp(argv[1], "--run-entry") == 0 &&
        std::strcmp(argv[3], "--xex-header") == 0;
#ifdef DOTM_SYNTHETIC_PRIVATE_BOOT
    const bool synthetic_entry = argc == 2 && std::strcmp(argv[1], "--synthetic-entry") == 0;
#else
    const bool synthetic_entry = false;
#endif
    if (!link_only && !run_original && !synthetic_entry) {
        std::fputs("Usage: dotm_private_link_probe [--run-entry <decrypted PPC PE> --xex-header <original default.xex>]\\n", stderr);
        return 2;
    }
    try {
        dotm::GuestMemory memory;
        if (run_original) memory.load_pe(argv[2]);
#ifdef DOTM_SYNTHETIC_PRIVATE_BOOT
        if (synthetic_entry) load_synthetic_boot_pe(memory);
#endif
        const auto result = dotm::install_actual_xenon_table(memory, unresolved_indirect);
        if ((run_original || synthetic_entry) &&
            memory.loaded_pe_entry() != result.guest_entry)
            throw std::runtime_error("Loaded PPC PE entry differs from linked _xstart mapping");
        if (run_original) {
            const auto header_address =
                dotm::install_original_xex_title_header(memory, argv[4], result.guest_entry);
            std::printf("Original XEX2 header mapped at guest 0x%08X\\n", header_address);
        }
        std::printf("Private native PPC link verified: %zu mapped functions, entry 0x%08X\n",
                    result.mapped_functions, result.guest_entry);
        if (!run_original && !synthetic_entry) {
            std::puts("No game entry executed; use --run-entry with a private decrypted PE and original XEX2 header.");
            return 0;
        }
        execute_guest_entry(memory, result.guest_entry);
        std::puts("Guest entry returned to host. This does not establish playable gameplay.");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Private native PPC boot probe failed: %s\n", e.what());
        return 1;
    }
}
