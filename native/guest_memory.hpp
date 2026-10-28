#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
namespace dotm {
using NativeFunction=void(*)(void*,std::uint8_t*);
struct FunctionEntry {std::uint32_t guest_address;NativeFunction native_function;};
class GuestMemory {
public:
 static constexpr std::uint64_t kAddressSpaceSize=0x100000000ULL;
 static constexpr std::uint32_t kImageBase=0x82000000U,kImageSize=0x01B10000U,kCodeBase=0x82500000U,kCodeSize=0x0132235CU;
 static constexpr std::uint64_t kFunctionTableOffset=std::uint64_t{kImageBase}+kImageSize,kFunctionTableSize=std::uint64_t{kCodeSize}*2;
 GuestMemory();~GuestMemory();GuestMemory(const GuestMemory&)=delete;GuestMemory& operator=(const GuestMemory&)=delete;
 std::uint8_t* base() const noexcept{return base_;}
 std::uint8_t* address(std::uint32_t addr) const noexcept{return base_+addr;}
 void commit(std::uint64_t addr,std::size_t length);
 void copy_to_guest(std::uint32_t addr,std::span<const std::uint8_t> bytes);
 void load_pe(const std::filesystem::path& path);
 void initialize_function_table(NativeFunction fallback);
 void install_functions(std::span<const FunctionEntry> entries);
 NativeFunction find_function(std::uint32_t addr) const;
private:std::uint8_t* base_=nullptr;bool table_initialized_=false;
};
}