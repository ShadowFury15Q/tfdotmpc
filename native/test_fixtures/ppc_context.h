#pragma once
// Test-only synthetic substitute for XenonRecomp's PPCContext ABI and mapping.
// Original game code and XEX content are never included.
#include <cstddef>
#include <cstdint>
struct PPCContext { std::uint32_t calls=0; };
typedef void PPCFunc(PPCContext& __restrict ctx,std::uint8_t* base);
struct PPCFuncMapping { std::size_t guest; PPCFunc* host; };
extern PPCFuncMapping PPCFuncMappings[];
