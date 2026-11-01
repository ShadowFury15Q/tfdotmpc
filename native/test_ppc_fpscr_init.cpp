#include "ppc_fpscr_init.hpp"
#include <cstdint>
#include <xmmintrin.h>
#include <stdexcept>
#include <iostream>

namespace {
struct HostFpscr {
    std::uint32_t csr = 0;
    std::uint32_t loadFromHost() noexcept {
        csr = _mm_getcsr();
        return csr & 0x6000U;
    }
};
struct GeneratedContext { HostFpscr fpscr; };
struct SyntheticContext { std::uint32_t marker = 0x42; };
}
int main() {
    try {
        const auto host = _mm_getcsr();
        GeneratedContext context{};
        dotm::initialize_guest_fpscr(context);
        if (context.fpscr.csr != host ||
            (context.fpscr.csr & 0x1F80U) != (host & 0x1F80U))
            throw std::runtime_error("Guest FPSCR lost host exception masks");
        SyntheticContext synthetic{};
        dotm::initialize_guest_fpscr(synthetic);
        if (synthetic.marker != 0x42)
            throw std::runtime_error("Synthetic PPC context modified");
        std::cout << "XenonRecomp host FPSCR initialization PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
