#include "compiled_ppc_adapter.hpp"
#include "guest_memory.hpp"
#include "guest_virtual_allocator.hpp"
#include "native_input.hpp"
#include "xam_guest_input.hpp"
#include "ppc_recomp_shared.h"
#include <cstdint>
#include <cstdio>
#include <exception>
#include <stdexcept>

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

        dotm::NativeInput input;
        dotm::XamInputHost xam;
        const auto allocated = heap.allocate(dotm::kXboxInputStateBytes);
        fail_if(!allocated.has_value(), "Guest heap could not allocate XAM state");
        const std::uint32_t output = *allocated;
        dotm::bind_xam_guest_input(&xam, &memory);
        input.set_button('F', true);
        xam.publish(input.next_frame());
        context.r3.u64 = 0;
        context.r4.u64 = 1;
        context.r5.u64 = output;
        memory.find_function(0x82500000U)(context, memory.base());
        fail_if(context.r3.u64 != dotm::kXamOk, "Linked XAM PPC import failed");
        fail_if(memory.address(output)[4] != 0x80, "Guest XAM output not written");
        dotm::bind_xam_guest_input(nullptr, nullptr);
        fail_if(!heap.release(output), "Guest XAM allocation was not released");
        std::puts("Synthetic multi-unit PPC + XAM + fail-fast import linked and ran");
        return 0;
    } catch (const std::exception& e) {
        dotm::bind_xam_guest_input(nullptr, nullptr);
        std::fprintf(stderr, "Synthetic link test failed: %s\n", e.what());
        return 1;
    }
}
