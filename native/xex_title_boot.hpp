#pragma once
#include <cstdint>
#include <filesystem>
namespace dotm {
class GuestMemory;
// Loads the actual, unmodified XEX2 header from a private original file into
// guest memory and wires the DOTM executable-module import slot. Never uses
// synthetic metadata or substitutes for the decrypted PE game image.
std::uint32_t install_original_xex_title_header(
    GuestMemory& memory, const std::filesystem::path& original_xex,
    std::uint32_t expected_entry);
}
