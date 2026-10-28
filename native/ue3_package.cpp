#include "ue3_package.hpp"
#include <lzokay.hpp>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <set>
#include <span>
#include <stdexcept>
#include <vector>

namespace dotm {
namespace {
constexpr std::uint32_t kUe3Tag = 0x9E2A83C1U;
constexpr std::size_t kHeaderLength = 0x71;
constexpr std::size_t kMaxSize = 256u * 1024u * 1024u;
std::uint32_t be32(std::span<const std::uint8_t> data, std::size_t off) {
    if (off > data.size() || data.size() - off < 4) throw std::runtime_error("Invalid UE3 offset");
    return (std::uint32_t(data[off]) << 24) | (std::uint32_t(data[off + 1]) << 16) |
           (std::uint32_t(data[off + 2]) << 8) | std::uint32_t(data[off + 3]);
}
std::int32_t si32(std::span<const std::uint8_t> data, std::size_t off) {
    const auto v = be32(data, off);
    return v < 0x80000000U ? std::int32_t(v) : std::int32_t(std::int64_t(v) - 0x100000000LL);
}
void check_span(std::size_t off, std::size_t len, std::size_t limit) {
    if (off > limit || len > limit - off) throw std::runtime_error("UE3 file span out of range");
}
std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::ate | std::ios::binary);
    if (!stream) throw std::runtime_error("Could not open UE3 package");
    const auto length = stream.tellg();
    if (length <= 0 || std::uint64_t(length) > kMaxSize) throw std::runtime_error("Invalid UE3 input file size");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    stream.seekg(0);
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), length)) throw std::runtime_error("Unable to read UE3 package");
    return bytes;
}
std::vector<std::uint8_t> expand(std::span<const std::uint8_t> raw, Ue3PackageInfo& info) {
    if (raw.size() < kHeaderLength || be32(raw, 0) != kUe3Tag) throw std::runtime_error("Not Xbox 360 big-endian UE3");
    const auto version = be32(raw, 4);
    info.version = version & 0xffffU;
    info.licensee_version = version >> 16;
    if (info.version != 537 || info.licensee_version != 174) throw std::runtime_error("Unexpected DOTM UE3 package version");
    info.compression_flags = be32(raw, 0x65);
    info.compressed_chunks = be32(raw, 0x69);
    if (info.compression_flags == 0 && info.compressed_chunks == 0)
        return std::vector<std::uint8_t>(raw.begin(), raw.end());
    if (info.compression_flags != 2 || info.compressed_chunks == 0 || info.compressed_chunks > 2048)
        throw std::runtime_error("Unsupported UE3 package compression");

    const std::size_t chunk_table = 0x6D;
    check_span(chunk_table, 16ULL * info.compressed_chunks, raw.size());
    std::size_t virtual_size = kHeaderLength;
    for (std::size_t chunk = 0; chunk < info.compressed_chunks; ++chunk) {
        const auto off = chunk_table + chunk * 16;
        const auto guest_off = be32(raw, off);
        const auto guest_size = be32(raw, off + 4);
        if (guest_off > kMaxSize || guest_size > kMaxSize - guest_off)
            throw std::runtime_error("UE3 guest chunk is too large");
        virtual_size = std::max(virtual_size, std::size_t(guest_off) + guest_size);
    }
    std::vector<std::uint8_t> logical(virtual_size);
    std::memcpy(logical.data(), raw.data(), 0x6D);
    std::size_t previous_guest_end = 0x6D;
    for (std::size_t chunk = 0; chunk < info.compressed_chunks; ++chunk) {
        const auto off = chunk_table + chunk * 16;
        const std::size_t guest_off = be32(raw, off);
        const std::size_t guest_size = be32(raw, off + 4);
        const std::size_t disk_off = be32(raw, off + 8);
        const std::size_t disk_size = be32(raw, off + 12);
        if (guest_off < previous_guest_end) throw std::runtime_error("Overlapping UE3 uncompressed chunks");
        check_span(disk_off, disk_size, raw.size());
        if (disk_size < 16 || be32(raw, disk_off) != kUe3Tag)
            throw std::runtime_error("Invalid UE3 compressed chunk header");
        const auto block_size = be32(raw, disk_off + 4);
        const auto payload_compressed = be32(raw, disk_off + 8);
        const auto payload_raw = be32(raw, disk_off + 12);
        if (block_size == 0 || block_size > 1024u * 1024u || payload_raw != guest_size)
            throw std::runtime_error("Unexpected UE3 LZO block geometry");
        const std::size_t blocks = (guest_size + block_size - 1) / block_size;
        check_span(disk_off + 16, blocks * 8, disk_off + disk_size);
        std::size_t read_pos = disk_off + 16 + blocks * 8;
        std::size_t write_pos = guest_off;
        std::size_t counted_compressed = 0;
        std::size_t counted_raw = 0;
        for (std::size_t b = 0; b < blocks; ++b) {
            const auto table_entry = disk_off + 16 + 8 * b;
            const auto packed = be32(raw, table_entry);
            const auto unpacked = be32(raw, table_entry + 4);
            if (!packed || !unpacked || unpacked > block_size)
                throw std::runtime_error("Invalid UE3 LZO sub-block length");
            check_span(read_pos, packed, disk_off + disk_size);
            check_span(write_pos, unpacked, logical.size());
            std::size_t written = 0;
            auto result = lzokay::decompress(raw.data() + read_pos, packed,
                                             logical.data() + write_pos, unpacked, written);
            if (result != lzokay::EResult::Success || written != unpacked)
                throw std::runtime_error("LZO decompression failure in UE3 package");
            read_pos += packed;
            write_pos += unpacked;
            counted_compressed += packed;
            counted_raw += unpacked;
        }
        if (counted_raw != guest_size || counted_compressed != payload_compressed ||
            disk_size != 16 + 8 * blocks + counted_compressed)
            throw std::runtime_error("UE3 LZO chunk size mismatch");
        previous_guest_end = guest_off + guest_size;
        info.decompressed_blocks += std::uint32_t(blocks);
    }
    return logical;
}
} // namespace

Ue3Package Ue3Package::load(const std::filesystem::path& path) {
    return parse(read_file(path));
}
Ue3Package Ue3Package::parse(const std::vector<std::uint8_t>& raw) {
    Ue3Package package;
    package.data_ = expand(raw, package.info_);
    std::span<const std::uint8_t> d(package.data_);
    const auto folder_len = si32(d, 16);
    if (folder_len < 1 || folder_len > 512) throw std::runtime_error("Invalid UE3 folder name");
    std::size_t h = 20 + std::size_t(folder_len);
    check_span(h, 32, d.size());
    package.info_.package_flags = be32(d, h);
    const auto name_count = be32(d, h + 4);
    const auto name_offset = be32(d, h + 8);
    package.info_.export_count = be32(d, h + 12);
    package.info_.import_count = be32(d, h + 20);
    const auto import_offset = be32(d, h + 24);
    if (name_count > 100000 || package.info_.import_count > 100000)
        throw std::runtime_error("UE3 table count exceeds safety limit");
    std::size_t p = name_offset;
    for (std::size_t i = 0; i < name_count; ++i) {
        auto n = si32(d, p); p += 4;
        std::string value;
        if (n > 0 && n <= 4096) {
            check_span(p, std::size_t(n) + 8, d.size());
            if (d[p + n - 1] != 0) throw std::runtime_error("Unterminated UE3 name");
            value.assign(reinterpret_cast<const char*>(d.data() + p), std::size_t(n - 1));
            p += n;
        } else if (n < 0 && n >= -2048) {
            const auto size = std::size_t(-n) * 2;
            check_span(p, size + 8, d.size());
            if (d[p + size - 1] != 0 || d[p + size - 2] != 0) throw std::runtime_error("Unterminated UTF16 UE3 name");
            for (std::size_t j = 0; j + 2 < size; j += 2) {
                auto cp = (std::uint16_t(d[p + j]) << 8) | d[p + j + 1];
                value.push_back(cp <= 127 ? char(cp) : '?');
            }
            p += size;
        } else {
            throw std::runtime_error("Invalid UE3 name size");
        }
        p += 8; // serialized UE3 FName flags
        package.names_.push_back(std::move(value));
    }
    check_span(import_offset, std::size_t(package.info_.import_count) * 28, d.size());
    auto at_name = [&](std::size_t index) -> std::string {
        if (index >= package.names_.size()) throw std::runtime_error("Invalid UE3 name index");
        return package.names_[index];
    };
    for (std::size_t i = 0; i < package.info_.import_count; ++i) {
        std::size_t off = import_offset + 28 * i;
        auto outer = si32(d, off + 16);
        package.imports_.push_back({at_name(be32(d, off)), at_name(be32(d, off + 8)),
                                    outer, at_name(be32(d, off + 20))});
    }
    return package;
}
std::vector<std::string> Ue3Package::imported_package_names() const {
    std::set<std::string> names;
    for (const auto& imp : imports_) {
        if (imp.outer_index == 0 && imp.class_name == "Package")
            names.insert(imp.object_name);
    }
    return {names.begin(), names.end()};
}
}
