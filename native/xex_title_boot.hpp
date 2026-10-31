#pragma once
#include <cstdint>
#include <filesystem>
namespace dotm {
class GuestMemory;
// Loads the actual, unmodified XEX2 header from a private original file into
// guest memory and wires the DOTM executable-module import slot. Never uses
// synthetic metadata or substitutes for the decrypted PE game image.
void bind_xex_system_modules(GuestMemory* memory) noexcept;
std::uint32_t bound_xex_load_image(
    std::uint32_t name_pointer, std::uint32_t flags,
    std::uint32_t minimum_version, std::uint32_t output_pointer);
std::uint32_t bound_xex_get_module_handle(
    std::uint32_t name_pointer, std::uint32_t output_pointer);
std::uint32_t bound_xex_get_module_section(
    std::uint32_t module_handle, std::uint32_t section_name,
    std::uint32_t output_address, std::uint32_t output_size);
std::uint32_t bound_xex_get_procedure_address(
    std::uint32_t module_handle, std::uint32_t ordinal,
    std::uint32_t output_pointer);
std::uint32_t bound_xex_unload_image(std::uint32_t module_handle);
std::uint32_t install_original_xex_title_header(
    GuestMemory& memory, const std::filesystem::path& original_xex,
    std::uint32_t expected_entry);
}
