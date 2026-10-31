#include "kernel_file_cache.hpp"
#include <functional>

namespace dotm {
namespace {
thread_local KernelFileCache* bound_cache = nullptr;
constexpr std::uint32_t kStatusSuccess = 0;
constexpr std::uint32_t kStatusInvalidParameter = 0xC000000DU;
constexpr std::uint32_t kStatusInvalidDeviceState = 0xC0000184U;
}

std::size_t KernelFileCache::KeyHash::operator()(const Key& key) const noexcept {
    const auto a = std::hash<std::uint64_t>{}(key.file_id);
    const auto b = std::hash<std::uint64_t>{}(key.block_index);
    return a ^ (b + std::size_t{0x9e3779b9U} + (a << 6) + (a >> 2));
}

void KernelFileCache::evict_to_limit() {
    while (entries_.size() > capacity_) {
        entries_.erase(recency_.back());
        recency_.pop_back();
    }
}

bool KernelFileCache::set_element_count(std::uint32_t cache_index,
                                        std::uint32_t count) {
    // DOTM passes cache index zero; no semantics have been verified for other
    // indices. Fail rather than silently configuring the wrong cache.
    if (cache_index != 0) return false;
    std::lock_guard lock(mutex_);
    capacity_ = count;
    evict_to_limit();
    return true;
}

std::uint32_t KernelFileCache::element_count() const {
    std::lock_guard lock(mutex_);
    return capacity_;
}

std::size_t KernelFileCache::cached_elements() const {
    std::lock_guard lock(mutex_);
    return entries_.size();
}

bool KernelFileCache::read(const Key& key, Element& output) {
    std::lock_guard lock(mutex_);
    const auto found = entries_.find(key);
    if (found == entries_.end()) return false;
    output = found->second.bytes;
    recency_.splice(recency_.begin(), recency_, found->second.recency);
    return true;
}

bool KernelFileCache::write(const Key& key, const Element& input) {
    std::lock_guard lock(mutex_);
    if (capacity_ == 0) return false;
    const auto found = entries_.find(key);
    if (found != entries_.end()) {
        found->second.bytes = input;
        recency_.splice(recency_.begin(), recency_, found->second.recency);
        return true;
    }
    recency_.push_front(key);
    try {
        entries_.emplace(key, Entry{input, recency_.begin()});
    } catch (...) {
        recency_.pop_front();
        throw;
    }
    evict_to_limit();
    return true;
}

void bind_kernel_file_cache(KernelFileCache* cache) noexcept { bound_cache = cache; }

std::uint32_t fsc_bound_set_cache_element_count(std::uint32_t cache_index,
                                                std::uint32_t count) noexcept {
    if (!bound_cache) return kStatusInvalidDeviceState;
    return bound_cache->set_element_count(cache_index, count)
        ? kStatusSuccess : kStatusInvalidParameter;
}

std::uint32_t fsc_bound_get_cache_element_count(std::uint32_t cache_index) noexcept {
    if (!bound_cache || cache_index != 0) return 0;
    return bound_cache->element_count();
}
}
