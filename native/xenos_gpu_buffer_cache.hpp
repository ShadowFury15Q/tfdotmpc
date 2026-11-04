#pragma once
#include <cstddef>
#include <cstdint>
#include <list>
#include <stdexcept>
#include <utility>
#include <vector>

namespace dotm {
// GPU allocations are immutable. Original guest DMA can overwrite the same
// address between PM4 draws; cache only exact copied bytes and buffer role.
enum class XenosGpuBufferRole { vertex, index };
template <class Handle>
class XenosGpuBufferCache {
public:
    explicit XenosGpuBufferCache(std::size_t budget = 64U * 1024U * 1024U,
                                 std::size_t limit = 512U)
        : budget_(budget), limit_(limit) {}
    template <class Upload>
    Handle get_or_upload(XenosGpuBufferRole role,
                         const std::vector<std::uint8_t>& bytes,
                         Upload&& upload) {
        if (bytes.empty() || bytes.size() > 64U * 1024U * 1024U)
            throw std::runtime_error("Invalid original PM4 GPU buffer size");
        const auto hash = digest(bytes);
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->role == role && it->hash == hash && it->bytes == bytes) {
                entries_.splice(entries_.begin(), entries_, it);
                return entries_.front().handle;
            }
        }
        Handle handle = std::forward<Upload>(upload)(bytes);
        if (!limit_ || bytes.size() > budget_) return handle;
        used_ += bytes.size();
        entries_.push_front({role, hash, bytes, std::move(handle)});
        while (used_ > budget_ || entries_.size() > limit_) {
            used_ -= entries_.back().bytes.size();
            entries_.pop_back();
        }
        return entries_.front().handle;
    }
    std::size_t entries() const noexcept { return entries_.size(); }
    std::size_t retained_bytes() const noexcept { return used_; }
    void clear() noexcept { entries_.clear(); used_ = 0; }
private:
    static std::uint64_t digest(const std::vector<std::uint8_t>& bytes) {
        std::uint64_t hash = 14695981039346656037ULL;
        for (const auto byte : bytes)
            hash = (hash ^ byte) * 1099511628211ULL;
        return hash;
    }
    struct Entry {
        XenosGpuBufferRole role;
        std::uint64_t hash;
        std::vector<std::uint8_t> bytes;
        Handle handle;
    };
    std::list<Entry> entries_;
    std::size_t budget_, limit_, used_ = 0;
};
} // namespace dotm
