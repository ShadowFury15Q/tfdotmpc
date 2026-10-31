#include "kernel_file_cache.hpp"
#include "ppc_recomp_shared.h"
#include <cstdint>
#include <stdexcept>
#include <thread>
PPC_EXTERN_FUNC(__imp__FscSetCacheElementCount);
PPC_EXTERN_FUNC(__imp__FscGetCacheElementCount);
namespace {
void check(bool ok) { if (!ok) throw std::runtime_error("Xbox Fsc cache ABI/LRU failure"); }
void set(PPCContext& ctx, std::uint32_t index, std::uint32_t count) {
    ctx.r3.u64 = index; ctx.r4.u64 = count;
    __imp__FscSetCacheElementCount(ctx, nullptr);
}
void get(PPCContext& ctx, std::uint32_t index) {
    ctx.r3.u64 = index;
    __imp__FscGetCacheElementCount(ctx, nullptr);
}
}
int main() {
    dotm::KernelFileCache cache;
    PPCContext ctx{};
    set(ctx, 0, 2);
    check(ctx.r3.u32 == 0xC0000184U);
    dotm::bind_kernel_file_cache(&cache);
    set(ctx, 1, 99);
    check(ctx.r3.u32 == 0xC000000DU && cache.element_count() == 0);
    set(ctx, 0, 2);
    check(ctx.r3.u64 == 0 && ctx.r4.u32 == 2 && cache.element_count() == 2);
    get(ctx, 0);
    check(ctx.r3.u64 == 2);
    using Cache = dotm::KernelFileCache;
    Cache::Element a{}, b{}, c{}, out{};
    a[0] = 11; b[0] = 22; c[0] = 33;
    const Cache::Key key_a{1, 0}, key_b{1, 1}, key_c{2, 0};
    check(cache.write(key_a, a) && cache.write(key_b, b));
    check(cache.read(key_a, out) && out[0] == 11);
    check(cache.write(key_c, c) && cache.cached_elements() == 2);
    check(!cache.read(key_b, out)); // Least recently used entry evicted.
    check(cache.read(key_c, out) && out[0] == 33);
    b[0] = 44;
    check(cache.write(key_c, b) && cache.read(key_c, out) && out[0] == 44);
    set(ctx, 0, 1);
    check(ctx.r3.u64 == 0 && cache.cached_elements() == 1);
    check(cache.read(key_c, out) && !cache.read(key_a, out));
    set(ctx, 0, 0);
    check(ctx.r3.u64 == 0 && cache.cached_elements() == 0);
    check(!cache.write(key_a, a));
    set(ctx, 0, 0xFFFFFFFFU); // Configuration is lazy, not a huge allocation.
    check(ctx.r3.u64 == 0 && cache.element_count() == 0xFFFFFFFFU);
    std::thread unbound([&] {
        PPCContext other{};
        set(other, 0, 3);
        check(other.r3.u32 == 0xC0000184U);
    });
    unbound.join();
    dotm::bind_kernel_file_cache(nullptr);
    set(ctx, 0, 2);
    check(ctx.r3.u32 == 0xC0000184U);
    return 0;
}
