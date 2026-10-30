#include "guest_memory.hpp"
#include "xex_optional_header.hpp"
#include "xex_title_boot.hpp"
#include <array>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
namespace {
void check(bool b, const char* message = "XEX2 boot wiring failure") { if (!b) throw std::runtime_error(message); }
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
    check(get32(memory, 0x8200085CU) == 0);
    check(get32(memory, 0x82000998U) == 0);
    check(get32(memory, 0x82000978U) == 0);
    check(!memory.committed_range(0x81904000U, 4096));
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
    check(get32(memory, 0x8200085CU) == 0x81904000U);
    check(get32(memory, 0x81904000U + 16) == 0U);
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
    check(memory.committed_range(0x81905000U, 0x64U) &&
          memory.committed_range(0x81906000U, 16U));
    check(get32(memory, 0x81905000U + 0x28U) == 0x81906000U);
    constexpr std::uint32_t name_ptr = 0x82001000U;
    constexpr std::uint32_t handle_ptr = 0x82001080U;
    memory.commit(name_ptr, 4096);
    std::memcpy(memory.address(name_ptr), "xam.xex", 8);
    dotm::bind_xex_system_modules(&memory);
    check(dotm::bound_xex_load_image(name_ptr, 9U, 0U, handle_ptr) == 0U &&
          get32(memory, handle_ptr) == 0x81905000U &&
          memory.address(0x81905040U)[1] == 1U,
          "Built-in XAM load returns real guest HMODULE and increments count");
    check(dotm::bound_xex_get_module_handle(name_ptr, handle_ptr) == 0U &&
          get32(memory, handle_ptr) == 0x81905000U,
          "XAM GetModuleHandle returns the same guest module");
    check(dotm::bound_xex_get_module_handle(0U, handle_ptr) == 0U &&
          get32(memory, handle_ptr) == 0x81901000U,
          "Null module name resolves the original title module");
    check(dotm::bound_xex_get_procedure_address(0x81905000U, 2815U,
                                                handle_ptr) == 0xC0000263U &&
          get32(memory, handle_ptr) == 0U,
          "Unsupported XAM Party export cannot return a fake function pointer");
    check(dotm::bound_xex_unload_image(0x81905000U) == 0U &&
          memory.address(0x81905040U)[1] == 0U,
          "Unloading a kernel module drops the reference but keeps it mapped");
    std::memcpy(memory.address(name_ptr), "other.xex", 10);
    check(dotm::bound_xex_load_image(name_ptr, 9U, 0U, handle_ptr) == 0xC000000FU &&
          get32(memory, handle_ptr) == 0U,
          "Unknown system module must fail without pretending to load it");
    check(dotm::bound_xex_load_image(name_ptr, 9U, 0U,
          dotm::GuestMemory::kFunctionTableOffset) == 0xC000000DU,
          "Module output must not overwrite PPC dispatch table");
    dotm::bind_xex_system_modules(nullptr);
    check(dotm::bound_xex_unload_image(0x81905000U) == 0xC00000BBU,
          "Unbound system module service must fail closed");
    return 0;
}
