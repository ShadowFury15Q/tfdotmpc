#include "xenos_gpu_texture_cache.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool ok, const char* why) {
    if (!ok) throw std::runtime_error(why);
}
}
int main() {
    try {
        dotm::XenosGpuTextureCache<unsigned> cache(32U, 2U);
        dotm::XenosTextureUpload a;
        a.slot = 1; a.width = 2; a.height = 2;
        a.format = 6; a.row_pitch = 8; a.bytes.assign(16, 0x12);
        unsigned uploads = 0;
        const auto upload = [&](const dotm::XenosTextureUpload&) {
            return ++uploads;
        };
        const auto first = cache.get_or_upload(a, upload);
        require(first == 1 && uploads == 1 && cache.entry_count() == 1,
                "First original GPU texture must upload");
        a.slot = 4;  // Same allocation can appear in different sampler slots.
        require(cache.get_or_upload(a, upload) == first && uploads == 1,
                "Byte-identical original texture must reuse GPU allocation");
        a.bytes[0] = 0x34;  // Guest can DMA into the same bound physical address.
        const auto changed = cache.get_or_upload(a, upload);
        require(changed == 2 && cache.entry_count() == 2,
                "Changed guest texels require a new GPU upload");
        a.bytes[0] = 0x12;
        require(cache.get_or_upload(a, upload) == first && uploads == 2,
                "Returning to original guest texels must reuse cached resource");
        a.format = 18;
        require(cache.get_or_upload(a, upload) == 3 &&
                cache.entry_count() == 2 && cache.retained_bytes() == 32,
                "GPU texture cache must account for host format and evict LRU");
        a.format = 6; a.bytes[0] = 0x34;
        require(cache.get_or_upload(a, upload) == 4,
                "Evicted original texture must be uploaded again");
        a.bytes.resize(48);
        require(cache.get_or_upload(a, upload) == 5 &&
                cache.entry_count() == 2 && cache.retained_bytes() == 32,
                "Oversized immutable texture must bypass bounded cache");
        cache.clear();
        require(cache.entry_count() == 0 && cache.retained_bytes() == 0,
                "Released device resources must clear texture cache");
        dotm::XenosGpuTextureCache<unsigned> disabled(0, 0);
        require(disabled.get_or_upload(a, upload) == 6 &&
                disabled.get_or_upload(a, upload) == 7,
                "Disabled cache must never reuse GPU resources");
        std::cout << "Original multi-draw GPU texture content cache PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
