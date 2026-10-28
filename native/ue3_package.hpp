#pragma once
#include <cstddef>
#include <cstdint>
#include <array>
#include <span>
#include <filesystem>
#include <string>
#include <vector>

namespace dotm {
struct Ue3Import {
    std::string class_package;
    std::string class_name;
    std::int32_t outer_index = 0;
    std::string object_name;
};
// Xbox 360 Transformers: Dark of the Moon UE3 FObjectExport (537/174).
// Extension byte == 1: uint32 generation count, generation counts,
// 16-byte GUID, 4-byte package metadata. Zero omits that extension.
struct Ue3Export {
    std::int32_t class_index = 0;
    std::int32_t super_index = 0;
    std::int32_t outer_index = 0;
    std::string object_name;
    std::int32_t object_name_number = 0;
    std::int32_t archetype_index = 0;
    std::uint64_t object_flags = 0;
    std::uint32_t serial_size = 0;
    std::uint32_t serial_offset = 0;
    std::uint32_t export_flags = 0;
    bool has_package_metadata = false;
    std::vector<std::uint32_t> generation_net_object_counts;
    std::array<std::uint8_t, 16> package_guid{};
    std::uint32_t package_metadata = 0;
};
struct Ue3PackageInfo {
    std::uint32_t version = 0;
    std::uint32_t licensee_version = 0;
    std::uint32_t package_flags = 0;
    std::uint32_t import_count = 0;
    std::uint32_t export_count = 0;
    std::uint32_t compression_flags = 0;
    std::uint32_t compressed_chunks = 0;
    std::uint32_t decompressed_blocks = 0;
};
class Ue3Package {
public:
    static Ue3Package load(const std::filesystem::path& path);
    static Ue3Package parse(const std::vector<std::uint8_t>& bytes);
    const Ue3PackageInfo& info() const noexcept { return info_; }
    const std::vector<std::string>& names() const noexcept { return names_; }
    const std::vector<Ue3Import>& imports() const noexcept { return imports_; }
    const std::vector<Ue3Export>& exports() const noexcept { return exports_; }
    // Returns raw serialized UE3 object bytes, NOT a decoded UObject.
    std::span<const std::uint8_t> serialized_object(std::size_t index) const;
    const std::vector<std::uint8_t>& uncompressed_data() const noexcept { return data_; }
    std::vector<std::string> imported_package_names() const;
private:
    Ue3PackageInfo info_{};
    std::vector<std::string> names_;
    std::vector<Ue3Import> imports_;
    std::vector<Ue3Export> exports_;
    std::vector<std::uint8_t> data_;
};
}
