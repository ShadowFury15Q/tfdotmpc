#pragma once
#include <cstdint>
namespace dotm {
constexpr std::uint32_t kInvalidTlsIndex = 0xFFFFFFFFU;
std::uint32_t kernel_tls_alloc() noexcept;
bool kernel_tls_free(std::uint32_t index) noexcept;
std::uint32_t kernel_tls_get_value(std::uint32_t index) noexcept;
bool kernel_tls_set_value(std::uint32_t index, std::uint32_t value) noexcept;
}
