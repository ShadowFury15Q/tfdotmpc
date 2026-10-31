#pragma once
#include "guest_memory.hpp"
#include <cstddef>
#include <cstdint>
#include <span>

namespace dotm {
// Binary-compatible with XenonRecomp's PPCFuncMapping on a 64-bit target.
// The game code / PPCContext definition itself remains outside this public repo.
struct RecompiledFunction {
    std::size_t guest;
    NativeFunction host;
};
struct DispatchSummary {
    std::size_t mapped_functions=0;
    std::uint32_t guest_entry=0;
};
// Validates full map first, then installs actual native recompiled CPU pointers
// into XenonRecomp's 8-byte-per-PPC-instruction host dispatch table.
// Requires entry point to be in the real function mappings, not merely the code segment.
DispatchSummary install_recompiled_dispatch(
    GuestMemory& memory,
    std::span<const RecompiledFunction> mappings,
    std::uint32_t entry_point,
    NativeFunction unresolved);
// The native entry pointer can only be invoked AFTER the Xbox 360 runtime has
// initialized guest stack, registers, APIs, TLS and required memory allocations.
}
