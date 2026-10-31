#include "kernel_tls.hpp"
#include "ppc_recomp_shared.h"
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <vector>
namespace {
void check(bool condition) { if (!condition) throw std::runtime_error("Xbox PPC TLS ABI regression"); }
}
int main() {
    PPCContext c{};
    __imp__KeTlsAlloc(c, nullptr);
    const auto index = c.r3.u32;
    check(index != dotm::kInvalidTlsIndex);
    c.r3.u64 = index;
    __imp__KeTlsGetValue(c, nullptr);
    check(c.r3.u64 == 0);
    c.r3.u64 = index;
    c.r4.u64 = 0xDEADC0DE;
    __imp__KeTlsSetValue(c, nullptr);
    check(c.r3.u64 == 1);
    c.r3.u64 = index;
    __imp__KeTlsGetValue(c, nullptr);
    check(c.r3.u64 == 0xDEADC0DE);
    std::thread other([index] {
        check(dotm::kernel_tls_get_value(index) == 0);
        check(dotm::kernel_tls_set_value(index, 42));
        check(dotm::kernel_tls_get_value(index) == 42);
    });
    other.join();
    check(dotm::kernel_tls_get_value(index) == 0xDEADC0DE);
    c.r3.u64 = index;
    __imp__KeTlsFree(c, nullptr);
    check(c.r3.u64 == 1);
    check(dotm::kernel_tls_get_value(index) == 0);
    check(!dotm::kernel_tls_set_value(index, 7));
    const auto reused = dotm::kernel_tls_alloc();
    check(reused == index);
    check(dotm::kernel_tls_get_value(reused) == 0);
    check(dotm::kernel_tls_free(reused));
    check(!dotm::kernel_tls_free(reused));
    check(!dotm::kernel_tls_free(dotm::kInvalidTlsIndex));
    std::vector<std::uint32_t> all;
    for (;;) {
        const auto i = dotm::kernel_tls_alloc();
        if (i == dotm::kInvalidTlsIndex) break;
        all.push_back(i);
        check(all.size() <= 256);
    }
    check(all.size() == 256);
    for (auto i : all) check(dotm::kernel_tls_free(i));
    return 0;
}
