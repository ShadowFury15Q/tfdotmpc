#include "compiled_ppc_adapter.hpp"
#include "guest_memory.hpp"
#include "guest_virtual_allocator.hpp"
#include "kernel_guest_memory.hpp"
#include "native_input.hpp"
#include "xam_guest_input.hpp"
#include "xnet_startup.hpp"
#include "ppc_recomp_shared.h"
#include <cstdint>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>

namespace {
void fail_if(bool condition, const char* message) {
    if (condition) throw std::runtime_error(message);
}
void unresolved_indirect(PPCContext&, std::uint8_t*) {
    throw std::runtime_error("Unexpected indirect guest function call");
}
}

int main() {
    try {
        dotm::GuestMemory memory;
        dotm::GuestVirtualAllocator heap(memory);
        const auto result = dotm::install_actual_xenon_table(memory, unresolved_indirect);
        fail_if(result.mapped_functions != 3, "Synthetic table was not linked in full");
        fail_if(result.guest_entry != 0x8277F670U, "Wrong guest entry point");
        PPCContext context{};
        memory.find_function(result.guest_entry)(context, memory.base());
        fail_if(context.calls != 1, "Synthetic _xstart did not execute");

        // The first unsupported Xbox import must surface an actionable
        // error to the host, rather than aborting the entire process.
        bool unresolved_import_reported = false;
        try {
            memory.find_function(0x82500004U)(context, memory.base());
        } catch (const std::runtime_error& error) {
            unresolved_import_reported =
                std::string(error.what()).find("__imp__KeInitializeApc")
                != std::string::npos;
        }
        fail_if(!unresolved_import_reported,
                "Unimplemented PPC import did not return a named diagnostic");

        dotm::NativeInput input;
        dotm::XamInputHost xam;
        const auto allocated = heap.allocate(dotm::kXboxInputStateBytes);
        fail_if(!allocated.has_value(), "Guest heap could not allocate XAM state");
        const std::uint32_t output = *allocated;
        dotm::bind_xam_guest_input(&xam, &memory);
        dotm::XNetStartupHost xnet;
        dotm::bind_xnet_startup(&xnet, &memory);
        context.r3.u64 = 1;
        context.r4.u64 = 0;
        __imp__NetDll_XNetStartup(context, memory.base());
        fail_if(context.r3.u32 != dotm::kXNetSuccess || xnet.reference_count() != 1,
                "Linked NetDll_XNetStartup did not initialize network configuration");
        context.r3.u64 = 1;
        __imp__NetDll_XNetCleanup(context, memory.base());
        fail_if(context.r3.u32 != dotm::kXNetSuccess || xnet.reference_count() != 0,
                "Linked NetDll_XNetCleanup did not release network configuration");
        input.set_button('F', true);
        xam.publish(input.next_frame());
        context.r3.u64 = 0;
        context.r4.u64 = 1;
        context.r5.u64 = output;
        memory.find_function(0x82500000U)(context, memory.base());
        fail_if(context.r3.u64 != dotm::kXamOk, "Linked XAM PPC import failed");
        fail_if(memory.address(output)[4] != 0x80, "Guest XAM output not written");
        fail_if(!heap.release(output), "Guest XAM allocation was not released");
        fail_if(memory.committed_range(output, dotm::kXboxInputStateBytes),
                "Released XAM output must no longer be committed");
        context.r3.u64 = 0;
        context.r4.u64 = 1;
        context.r5.u64 = output;
        memory.find_function(0x82500000U)(context, memory.base());
        fail_if(context.r3.u64 != dotm::kXamBadArguments,
                "Linked PPC XAM import must reject a freed output buffer");
        // Verify the real native helper object files are linked into the
        // synthetic PPC executable, not silently replaced with throw-stubs.
        // These are synthetic register values, not proprietary game code.
        constexpr std::uint32_t helper_page = 0x82040000U;
        constexpr std::uint32_t helper_sp = helper_page + 0x800U;
        memory.commit(helper_page, 4096);
        context.r1.u32 = helper_sp;
        context.r12.u64 = 0x0102030405060708ULL;
        context.r28.u64 = 0x1122334455667788ULL;
        context.r31.u64 = 0xAABBCCDDEEFF0011ULL;
        __savegprlr_28(context, memory.base());
        fail_if(memory.address(helper_sp - 0x98U + 14U * 8U)[0] != 0x11,
                "Linked GPR save did not write big-endian guest bytes");
        context.r28.u64 = 0;
        context.r31.u64 = 0;
        context.lr = 0;
        __restgprlr_28(context, memory.base());
        fail_if(context.r28.u64 != 0x1122334455667788ULL ||
                context.r31.u64 != 0xAABBCCDDEEFF0011ULL ||
                context.lr != 0x0102030405060708ULL,
                "Linked GPR/LR restore did not preserve guest registers");
        context.r12.u32 = helper_sp;
        context.f28.u64 = 0x7FF8000000000028ULL;
        context.f31.u64 = 0x3FF0000000000031ULL;
        __savefpr_28(context, memory.base());
        fail_if(memory.address(helper_sp - 0x90U + 14U * 8U)[0] != 0x7F,
                "Linked FPR save did not write big-endian guest bytes");
        context.f28.u64 = 0;
        context.f31.u64 = 0;
        __restfpr_28(context, memory.base());
        fail_if(context.f28.u64 != 0x7FF8000000000028ULL ||
                context.f31.u64 != 0x3FF0000000000031ULL,
                "Linked FPR restore did not preserve IEEE-754 register bits");
        // The same multi-object native link must resolve the kernel PPC
        // imports as well as XAM and the synthetic recompiled _xstart.
        constexpr std::uint32_t nt_args = 0x82030000U;
        memory.commit(nt_args, 4096);
        auto put_be = [&](std::uint32_t addr, std::uint32_t value) {
            auto* p = memory.address(addr);
            for (int i = 0; i < 4; ++i)
                p[i] = static_cast<std::uint8_t>(value >> (24 - 8 * i));
        };
        dotm::bind_kernel_guest_memory(&memory, &heap);
        put_be(nt_args, 0);
        put_be(nt_args + 4, 4096);
        context.r3.u64 = nt_args;
        context.r4.u64 = nt_args + 4;
        context.r5.u64 = 0x3000;
        context.r6.u64 = 4;
        context.r7.u64 = 0;
        __imp__NtAllocateVirtualMemory(context, memory.base());
        fail_if(context.r3.u64 != dotm::kNtSuccess,
                "Linked native NT allocate import failed");
        const auto nt_base = (std::uint32_t(memory.address(nt_args)[0]) << 24) |
                             (std::uint32_t(memory.address(nt_args)[1]) << 16) |
                             (std::uint32_t(memory.address(nt_args)[2]) << 8) |
                             std::uint32_t(memory.address(nt_args)[3]);
        put_be(nt_args + 4, 0);
        context.r3.u64 = nt_args;
        context.r4.u64 = nt_args + 4;
        context.r5.u64 = 0x8000;
        context.r6.u64 = 0;
        __imp__NtFreeVirtualMemory(context, memory.base());
        fail_if(context.r3.u64 != dotm::kNtSuccess ||
                memory.committed_range(nt_base, 1),
                "Linked native NT free import failed to revoke pages");
        dotm::bind_kernel_guest_memory(nullptr, nullptr);
        dotm::bind_xnet_startup(nullptr, nullptr);
        dotm::bind_xam_guest_input(nullptr, nullptr);
        std::puts("Synthetic multi-unit PPC + XAM + fail-fast import linked and ran");
        return 0;
    } catch (const std::exception& e) {
        dotm::bind_xnet_startup(nullptr, nullptr);
        dotm::bind_xam_guest_input(nullptr, nullptr);
        std::fprintf(stderr, "Synthetic link test failed: %s\n", e.what());
        return 1;
    }
}
