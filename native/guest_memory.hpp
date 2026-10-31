#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
struct PPCContext; // XenonRecomp's original CPU context; definition stays in private generated code.
namespace dotm {
using NativeFunction=void(*)(PPCContext&,std::uint8_t*);
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
 // Revoke host access and discard bytes; recommitted pages start zeroed.
 // Caller must own all pages in the requested range.
 void decommit(std::uint64_t addr,std::size_t length);
 // Query only pages actually committed by this GuestMemory instance.
 bool committed_range(std::uint64_t addr,std::size_t length) const noexcept;
 // True when ANY page in the range is already committed. Invalid nonempty
 // ranges fail closed so allocators cannot overwrite unrelated guest memory.
 bool any_committed_range(std::uint64_t addr,std::size_t length) const noexcept;
 void copy_to_guest(std::uint32_t addr,std::span<const std::uint8_t> bytes);
 void load_pe(const std::filesystem::path& path);
 // PE AddressOfEntryPoint from the last successfully loaded guest image.
 // Never infer it from a hardcoded DOTM symbol without checking the PE.
 std::uint32_t loaded_pe_entry() const;
 bool has_loaded_pe() const noexcept {return pe_loaded_;}
 void initialize_function_table(NativeFunction fallback);
 void install_functions(std::span<const FunctionEntry> entries);
 NativeFunction find_function(std::uint32_t addr) const;
private:std::uint8_t* base_=nullptr;bool table_initialized_=false;
 std::uint32_t pe_entry_=0;bool pe_loaded_=false;
 std::array<std::uint64_t,16384> committed_pages_{};
};
}