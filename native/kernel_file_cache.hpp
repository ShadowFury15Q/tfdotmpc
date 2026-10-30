#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <list>
#include <mutex>
#include <unordered_map>

namespace dotm {
// File-system cache elements are 4 KiB in the original title's startup
// callsite (sub_8351DC60 rounds bytes to 0x1000-byte elements).
class KernelFileCache {
public:
    static constexpr std::size_t kElementBytes = 4096;
    using Element = std::array<std::uint8_t, kElementBytes>;
    struct Key {
        std::uint64_t file_id;
        std::uint64_t block_index;
        bool operator==(const Key&) const = default;
    };
    bool set_element_count(std::uint32_t cache_index, std::uint32_t count);
    std::uint32_t element_count() const;
    std::size_t cached_elements() const;
    bool read(const Key& key, Element& output);
    bool write(const Key& key, const Element& input);
private:
    struct KeyHash {
        std::size_t operator()(const Key& key) const noexcept;
    };
    struct Entry {
        Element bytes;
        std::list<Key>::iterator recency;
    };
    void evict_to_limit(); // Caller holds mutex_.
    mutable std::mutex mutex_;
    std::uint32_t capacity_ = 0;
    std::list<Key> recency_;
    std::unordered_map<Key, Entry, KeyHash> entries_;
};

void bind_kernel_file_cache(KernelFileCache* cache) noexcept;
std::uint32_t fsc_bound_set_cache_element_count(std::uint32_t cache_index,
                                                std::uint32_t count) noexcept;
std::uint32_t fsc_bound_get_cache_element_count(std::uint32_t cache_index) noexcept;
}
