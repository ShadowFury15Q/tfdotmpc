#pragma once
#include "xenos_texture_upload.hpp"
#include <cstddef>
#include <cstdint>
#include <list>
#include <stdexcept>
#include <utility>
#include <vector>

namespace dotm {

// Retain immutable host resources across original PM4 draws. The guest's
// physical memory may be modified without rebinding a fetch descriptor, so
// reuse requires byte-exact content equality, not just an Xbox address.
template <typename Handle>
class XenosGpuTextureCache {
public:
    explicit XenosGpuTextureCache(
        std::size_t budget_bytes = 128U * 1024U * 1024U,
        std::size_t max_entries = 256U)
        : budget_bytes_(budget_bytes), max_entries_(max_entries) {}

    template <typename Uploader>
    Handle get_or_upload(const XenosTextureUpload& image, Uploader&& upload) {
        if (!image.width || !image.height || !image.row_pitch ||
            image.bytes.empty())
            throw std::runtime_error("Invalid original Xenos texture cache input");
        const auto digest = hash_bytes(image.bytes);
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->digest == digest &&
                it->width == image.width && it->height == image.height &&
                it->format == image.format && it->row_pitch == image.row_pitch &&
                it->bytes == image.bytes) {
                entries_.splice(entries_.begin(), entries_, it);
                return entries_.front().handle;
            }
        }
        // Upload before changing cache state: a driver failure must not
        // expose a partially initialized GPU object on the next draw.
        Handle resource = std::forward<Uploader>(upload)(image);
        if (!max_entries_ || image.bytes.size() > budget_bytes_)
            return resource;
        const std::size_t size = image.bytes.size();
        entries_.push_front(Entry{image.width, image.height, image.format,
                                  image.row_pitch, digest, image.bytes,
                                  std::move(resource)});
        used_bytes_ += size;
        while (entries_.size() > max_entries_ || used_bytes_ > budget_bytes_) {
            used_bytes_ -= entries_.back().bytes.size();
            entries_.pop_back();
        }
        return entries_.front().handle;
    }

    std::size_t entry_count() const noexcept { return entries_.size(); }
    std::size_t retained_bytes() const noexcept { return used_bytes_; }
    void clear() noexcept { entries_.clear(); used_bytes_ = 0; }

private:
    struct Entry {
        std::uint32_t width, height, format, row_pitch;
        std::uint64_t digest;
        std::vector<std::uint8_t> bytes;
        Handle handle;
    };
    static std::uint64_t hash_bytes(const std::vector<std::uint8_t>& bytes) {
        std::uint64_t hash = 14695981039346656037ULL;
        for (const auto byte : bytes) {
            hash ^= byte;
            hash *= 1099511628211ULL;
        }
        return hash;
    }
    std::list<Entry> entries_;
    std::size_t budget_bytes_, max_entries_, used_bytes_ = 0;
};
} // namespace dotm
