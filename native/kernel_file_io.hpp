#pragma once
#include "guest_memory.hpp"
#include <cstdint>
#include <filesystem>

namespace dotm {
// Mount a user-owned, extracted Xbox 360 title directory as D:\ (read-only).
// An empty root intentionally means no disc is mounted; never fabricate data.
void bind_kernel_file_io(GuestMemory* memory,
                         const std::filesystem::path& game_root) noexcept;
std::uint32_t nt_bound_create_file(std::uint32_t handle_out,
    std::uint32_t access, std::uint32_t attributes, std::uint32_t io_status,
    std::uint32_t allocation_size, std::uint32_t file_attributes,
    std::uint32_t share_access, std::uint32_t disposition,
    std::uint32_t guest_stack);
// Xbox NtOpenFile: six PPC register arguments, existing read-only title files.
std::uint32_t nt_bound_open_file(std::uint32_t handle_out,
    std::uint32_t access, std::uint32_t attributes, std::uint32_t io_status,
    std::uint32_t share_access, std::uint32_t open_options);
std::uint32_t nt_bound_read_file(std::uint32_t handle, std::uint32_t event,
    std::uint32_t apc_routine, std::uint32_t apc_context,
    std::uint32_t io_status, std::uint32_t buffer,
    std::uint32_t length, std::uint32_t offset,
    std::uint32_t guest_stack);
// Query FILE_NETWORK_OPEN_INFORMATION (56 bytes) for a guest D: path.
std::uint32_t nt_bound_query_full_attributes_file(
    std::uint32_t attributes, std::uint32_t output);
std::uint32_t nt_bound_query_information_file(std::uint32_t handle,
    std::uint32_t io_status, std::uint32_t output,
    std::uint32_t length, std::uint32_t info_class);
std::uint32_t nt_bound_close_file(std::uint32_t handle);
}
