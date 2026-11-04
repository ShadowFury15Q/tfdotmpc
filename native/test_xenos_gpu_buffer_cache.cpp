#include "xenos_gpu_buffer_cache.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
void require(bool ok, const char* reason) {
    if (!ok) throw std::runtime_error(reason);
}
}
int main() {
    try {
        using dotm::XenosGpuBufferRole;
        dotm::XenosGpuBufferCache<unsigned> cache(12U, 2U);
        unsigned uploads = 0;
        const auto create = [&](const std::vector<std::uint8_t>&) {
            return ++uploads;
        };
        std::vector<std::uint8_t> first{1, 2, 3, 4};
        require(cache.get_or_upload(XenosGpuBufferRole::vertex, first, create) == 1,
                "Initial guest vertex DMA must upload");
        require(cache.get_or_upload(XenosGpuBufferRole::vertex, first, create) == 1 &&
                uploads == 1, "Repeated guest vertex DMA must reuse GPU buffer");
        first[0] = 9;
        require(cache.get_or_upload(XenosGpuBufferRole::vertex, first, create) == 2,
                "Guest writes at same address must invalidate byte-exact cache");
        require(cache.get_or_upload(XenosGpuBufferRole::index, first, create) == 3,
                "Index and vertex allocations must have distinct bind roles");
        require(cache.entries() == 2 && cache.retained_bytes() == 8,
                "GPU buffer cache must evict oldest allocation");
        first[0] = 1;
        require(cache.get_or_upload(XenosGpuBufferRole::vertex, first, create) == 4,
                "Evicted vertex allocation must upload again");
        std::vector<std::uint8_t> oversized(16, 0x7F);
        require(cache.get_or_upload(XenosGpuBufferRole::vertex, oversized, create) == 5 &&
                cache.entries() == 2 && cache.retained_bytes() == 8,
                "Oversized guest allocation must bypass GPU cache");
        bool rejected = false;
        try { cache.get_or_upload(XenosGpuBufferRole::vertex, {}, create); }
        catch (const std::runtime_error&) { rejected = true; }
        require(rejected, "Empty guest GPU buffer must be rejected");
        cache.clear();
        require(cache.entries() == 0 && cache.retained_bytes() == 0,
                "GPU buffer cache clear must release retained resources");
        std::cout << "Original PM4 indexed and vertex DMA GPU cache PASS\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
