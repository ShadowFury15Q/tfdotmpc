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
    // The original title's XEX2 resource directory is independent of PE
    // section names; exercise the same HashSec lookup as the game.
    put32(header, 20, 3);
    put32(header, 40, 0x000002FFU);
    put32(header, 44, 0x180U);
    put32(header, 0x180U, 20U);
    std::memcpy(header.data() + 0x184U, "HashSec", 7);
    put32(header, 0x18CU, 0x82000000U);
    put32(header, 0x190U, 128U);
    {
        std::ofstream out(file.path, std::ios::binary | std::ios::trunc);
        check(static_cast<bool>(out.write(
            reinterpret_cast<const char*>(header.data()), header.size())));
    }
    // Reject corrupted ordinal before allocating any kernel export pages.
    for (int i = 0; i < 4; ++i)
        memory.address(0x820009D8U)[i] = std::uint8_t(0xDEADBEEFU >> (24 - i * 8));
    bool bad_command_line_import = false;
    try { dotm::install_original_xex_title_header(memory, file.path, entry); }
    catch (const std::runtime_error&) { bad_command_line_import = true; }
    check(bad_command_line_import && !memory.committed_range(0x81907000U, 1));
    for (int i = 0; i < 4; ++i)
        memory.address(0x820009D8U)[i] = std::uint8_t(0x000101AEU >> (24 - i * 8));
    check(dotm::install_original_xex_title_header(memory, file.path, entry)
          == 0x81000000U);
    check(get32(memory, 0x8200085CU) == 0x81904000U);
    check(get32(memory, 0x81904000U + 16) == 0U);
    check(get32(memory, 0x82000998U) == 0x81900000U);
    check(get32(memory, 0x820009D8U) == 0x81907000U);
    check(get32(memory, 0x820009B0U) == 0x81908000U);
    check(get32(memory, 0x81908000U) == 27U);
    check(std::strcmp(reinterpret_cast<const char*>(memory.address(0x81907000U)),
                      "\"default.xex\"") == 0,
          "ExLoadedCommandLine points to a real guest ANSI command line");
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
    std::memcpy(memory.address(name_ptr + 0x20U), "HashSec", 8);
    check(dotm::bound_xex_get_module_section(
              0x81901000U, name_ptr + 0x20U, handle_ptr + 0x10U,
              handle_ptr + 0x14U) == 0U &&
          get32(memory, handle_ptr + 0x10U) == 0x82000000U &&
          get32(memory, handle_ptr + 0x14U) == 128U,
          "Original XEX2 HashSec resource resolves to its actual guest range");
    check(dotm::bound_xex_get_module_section(
              0x81905000U, name_ptr + 0x20U, handle_ptr + 0x10U,
              handle_ptr + 0x14U) == 0xC0000225U,
          "XAM must not invent a resource section");
    check(dotm::bound_xex_get_module_section(
              0x12345678U, name_ptr + 0x20U, handle_ptr + 0x10U,
              handle_ptr + 0x14U) == 0xC0000008U,
          "Unknown module handles must be rejected");
    check(dotm::bound_xex_get_module_section(
              0x81901000U, name_ptr + 0x20U, handle_ptr + 0x10U,
              handle_ptr + 0x12U) == 0xC000000DU,
          "Overlapping output pointers must not corrupt the guest");
    std::memcpy(memory.address(name_ptr + 0x20U), "Missing", 8);
    check(dotm::bound_xex_get_module_section(
              0x81901000U, name_ptr + 0x20U, handle_ptr + 0x10U,
              handle_ptr + 0x14U) == 0xC0000225U,
          "Unknown resource must return STATUS_NOT_FOUND");
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
