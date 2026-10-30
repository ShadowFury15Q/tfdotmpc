#include "compiled_ppc_adapter.hpp"
#include "guest_memory.hpp"
#include "guest_virtual_allocator.hpp"
#include "kernel_guest_memory.hpp"
#include "native_input.hpp"
#include "ppc_recomp_shared.h" // Includes the generated PPC configuration and context
#include "xam_guest_input.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <stdexcept>

namespace {
constexpr std::uint32_t kStackBase = 0x70000000U;
constexpr std::uint32_t kStackBytes = 0x00100000U;
constexpr std::uint32_t kInitialStackPointer = kStackBase + kStackBytes - 0x100U;

void unresolved_indirect(PPCContext&, std::uint8_t*) {
    std::fputs("Unimplemented indirect PPC target; native game execution is blocked\n", stderr);
    std::abort();
}

// The current host bindings implement only a subset of Xbox 360 imports.
// Keep them alive for the entire guest entry call, including exception paths.
struct ScopedHostBindings {
    ScopedHostBindings(dotm::GuestMemory& memory,
                       dotm::GuestVirtualAllocator& allocator,
                       dotm::XamInputHost& input) {
        dotm::bind_kernel_guest_memory(&memory, &allocator);
        dotm::bind_xam_guest_input(&input, &memory);
    }
    ~ScopedHostBindings() {
        dotm::bind_xam_guest_input(nullptr, nullptr);
        dotm::bind_kernel_guest_memory(nullptr, nullptr);
    }
    ScopedHostBindings(const ScopedHostBindings&) = delete;
    ScopedHostBindings& operator=(const ScopedHostBindings&) = delete;
};

void execute_guest_entry(dotm::GuestMemory& memory, std::uint32_t entry) {
    // Reserve a dedicated 1 MiB guest stack outside the NT heap range.
    // Xbox startup/TLS, graphics and audio imports are not yet implemented.
    if (memory.any_committed_range(kStackBase, kStackBytes))
        throw std::runtime_error("Guest boot stack overlaps committed memory");
    memory.commit(kStackBase, kStackBytes);
    std::memset(memory.address(kStackBase), 0, kStackBytes);

    dotm::GuestVirtualAllocator allocator(memory);
    dotm::NativeInput native_input;
    dotm::XamInputHost xam_input;
    xam_input.publish(native_input.next_frame());
    ScopedHostBindings bindings(memory, allocator, xam_input);

    PPCContext context{};
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
    const bool run_original = argc == 3 && std::strcmp(argv[1], "--run-entry") == 0;
#ifdef DOTM_SYNTHETIC_PRIVATE_BOOT
    const bool synthetic_entry = argc == 2 && std::strcmp(argv[1], "--synthetic-entry") == 0;
#else
    const bool synthetic_entry = false;
#endif
    if (!link_only && !run_original && !synthetic_entry) {
        std::fputs("Usage: dotm_private_link_probe [--run-entry <decrypted Xbox 360 PPC PE>]\n", stderr);
        return 2;
    }
    try {
        dotm::GuestMemory memory;
        if (run_original) memory.load_pe(argv[2]);
        const auto result = dotm::install_actual_xenon_table(memory, unresolved_indirect);
        std::printf("Private native PPC link verified: %zu mapped functions, entry 0x%08X\n",
                    result.mapped_functions, result.guest_entry);
        if (!run_original && !synthetic_entry) {
            std::puts("No game entry executed; use --run-entry with a private decrypted PE.");
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
