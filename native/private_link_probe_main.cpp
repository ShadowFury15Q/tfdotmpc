#include "compiled_ppc_adapter.hpp"
#include "guest_memory.hpp"
#include <cstdio>
#include <cstdlib>
#include <exception>

namespace {
void unresolved_indirect(PPCContext&, std::uint8_t*) {
    std::fputs("Unimplemented indirect PPC target; native game execution is blocked\n", stderr);
    std::abort();
}
}

int main() {
    try {
        dotm::GuestMemory memory;
        const auto result = dotm::install_actual_xenon_table(memory, unresolved_indirect);
        std::printf("Private native PPC link verified: %zu mapped functions, entry 0x%08X\n",
                    result.mapped_functions, result.guest_entry);
        std::puts("No game entry executed. Xbox kernel/XAM, graphics, audio and TLS remain incomplete.");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Private native PPC link probe failed: %s\n", e.what());
        return 1;
    }
}
