#include "guest_memory.hpp"
#include "xex_optional_header.hpp"
#include "xex_title_boot.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
namespace {
void check(bool b) { if (!b) throw std::runtime_error("XEX2 boot wiring failure"); }
void put32(std::array<std::uint8_t, 512>& b, std::size_t offset, std::uint32_t value) {
    b[offset] = std::uint8_t(value >> 24);
    b[offset + 1] = std::uint8_t(value >> 16);
    b[offset + 2] = std::uint8_t(value >> 8);
    b[offset + 3] = std::uint8_t(value);
}
std::uint32_t get32(const dotm::GuestMemory& m, std::uint32_t address) {
    const auto* p = m.address(address);
    return std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 |
           std::uint32_t(p[2]) << 8 | std::uint32_t(p[3]);
}
struct TempFile {
    std::filesystem::path path;
    ~TempFile() { std::error_code error; std::filesystem::remove(path, error); }
};
}
int main() {
    constexpr std::uint32_t entry = 0x8277F670U;
    dotm::GuestMemory memory;
    memory.commit(0x82000000U, 4096);
    std::array<std::uint8_t, 512> header{};
    put32(header, 0, 0x58455832U);
    put32(header, 8, 512);
    put32(header, 20, 2);
    put32(header, 24, 0x00010100U);
    put32(header, 28, entry);
    put32(header, 32, 0x00040006U);
    put32(header, 36, 256);
    put32(header, 256 + 12, 0x415608A4U);
    TempFile file{
        std::filesystem::temp_directory_path() /
        ("dotm_xex_title_boot_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
         ".xex")
    };
    {
        std::ofstream out(file.path, std::ios::binary | std::ios::trunc);
        check(static_cast<bool>(out.write(
            reinterpret_cast<const char*>(header.data()), header.size())));
    }
    bool bad_entry = false;
    try { dotm::install_original_xex_title_header(memory, file.path, entry + 4); }
    catch (const std::runtime_error&) { bad_entry = true; }
    check(bad_entry && !memory.committed_range(0x81000000U, 24));
    put32(header, 256 + 12, 0x41560890U);
    {
        std::ofstream out(file.path, std::ios::binary | std::ios::trunc);
        check(static_cast<bool>(out.write(
            reinterpret_cast<const char*>(header.data()), header.size())));
    }
    bool bad_title = false;
    try { dotm::install_original_xex_title_header(memory, file.path, entry); }
    catch (const std::runtime_error&) { bad_title = true; }
    check(bad_title && !memory.committed_range(0x81000000U, 24));
    check(get32(memory, 0x82000998U) == 0);
    check(get32(memory, 0x82000978U) == 0);
    check(!memory.committed_range(0x81902000U, 4096));
    check(!memory.committed_range(0x81903000U, 4096));
    put32(header, 256 + 12, 0x415608A4U);
    put32(header, 36, 500);
    {
        std::ofstream out(file.path, std::ios::binary | std::ios::trunc);
        check(static_cast<bool>(out.write(
            reinterpret_cast<const char*>(header.data()), header.size())));
    }
    bool bad_offset = false;
    try { dotm::install_original_xex_title_header(memory, file.path, entry); }
    catch (const std::runtime_error&) { bad_offset = true; }
    check(bad_offset && !memory.committed_range(0x81000000U, 24));
    put32(header, 36, 256);
    {
        std::ofstream out(file.path, std::ios::binary | std::ios::trunc);
        check(static_cast<bool>(out.write(
            reinterpret_cast<const char*>(header.data()), header.size())));
    }
    check(dotm::install_original_xex_title_header(memory, file.path, entry)
          == 0x81000000U);
    check(get32(memory, 0x82000998U) == 0x81900000U);
    check(get32(memory, 0x82000978U) == 0x81902000U);
    check(get32(memory, 0x81902000U) == 0x20U);
    check(memory.address(0x81902004U)[0] == 6U);
    check(memory.address(0x81902005U)[0] == 0U);
    check(get32(memory, 0x820009A0U) == 0x81903000U);
    check(get32(memory, 0x81903000U) == 0U);
    check(get32(memory, 0x81903018U) == 0U);
    check(get32(memory, 0x81900000U) == 0x81901000U);
    check(get32(memory, 0x81901000U + 0x58) == 0x81000000U);
    check(dotm::xex_optional_header_field(memory, 0x81000000U, 0x00010100U) == entry);
    check(dotm::xex_optional_header_field(memory, 0x81000000U, 0x00040006U)
          == 0x81000100U);
    return 0;
}
