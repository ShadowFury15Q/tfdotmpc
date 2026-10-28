#pragma once
#include <cstddef>
#include <cstdint>
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
    const std::vector<std::uint8_t>& uncompressed_data() const noexcept { return data_; }
    std::vector<std::string> imported_package_names() const;
private:
    Ue3PackageInfo info_{};
    std::vector<std::string> names_;
    std::vector<Ue3Import> imports_;
    std::vector<std::uint8_t> data_;
};
}
