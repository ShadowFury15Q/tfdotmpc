#include "xenos_sampler_state.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool ok, const char* error) {
    if (!ok) throw std::runtime_error(error);
}
template<class F> void rejects(F fn) {
    bool failed = false;
    try { fn(); } catch (const std::runtime_error&) { failed = true; }
    require(failed, "Unsupported guest sampler state must fail closed");
}
}
int main() {
    try {
        dotm::XenosTextureFetchConstant t;
        t.dimension = 1;
        // U wrap, V mirror, W edge clamp, linear magnification and point min.
        t.raw_words[0] = (1U << 13U) | (2U << 16U);
        t.raw_words[3] = (1U << 19U);
        const auto decoded = dotm::decode_xenos_host_sampler(t);
        require(decoded.u == dotm::XenosHostAddress::wrap &&
                decoded.v == dotm::XenosHostAddress::mirror &&
                decoded.w == dotm::XenosHostAddress::clamp &&
                decoded.mag_linear && !decoded.min_linear,
                "Decode original clamp and independent filter bits");
        t.raw_words[3] |= (1U << 21U);
        require(dotm::decode_xenos_host_sampler(t).min_linear,
                "Independent Xenos minification linear mode");
        t.raw_words[3] = (3U << 19U);
        rejects([&] { (void)dotm::decode_xenos_host_sampler(t); });
        t.raw_words[3] = (3U << 21U);
        rejects([&] { (void)dotm::decode_xenos_host_sampler(t); });
        t.raw_words[3] = (2U << 25U);
        rejects([&] { (void)dotm::decode_xenos_host_sampler(t); });
        t.raw_words[3] = 0;
        t.raw_words[0] |= 6U << 10U;
        rejects([&] { (void)dotm::decode_xenos_host_sampler(t); });
        t.raw_words[0] &= ~(7U << 10U);
        t.mip_max_level = 4U;
        rejects([&] { (void)dotm::decode_xenos_host_sampler(t); });
        std::cout << "Xenos original 2D sampler clamp/filter mapping PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
