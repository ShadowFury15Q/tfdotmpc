#pragma once
// Test-only synthetic substitute for XenonRecomp's PPCContext ABI and mapping.
// Original game code and XEX content are never included.
#include <cstddef>
#include <cstdint>
struct PPCRegister { std::uint64_t u64=0; };
struct PPCContext {
 std::uint32_t calls=0;
 PPCRegister r1{},r3{},r4{},r5{},r6{},r7{};
};
typedef void PPCFunc(PPCContext& __restrict ctx,std::uint8_t* base);
struct PPCFuncMapping { std::size_t guest; PPCFunc* host; };
extern PPCFuncMapping PPCFuncMappings[];
