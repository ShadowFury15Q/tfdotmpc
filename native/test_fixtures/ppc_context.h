#pragma once
// Test-only synthetic substitute for XenonRecomp's PPCContext ABI and mapping.
// Original game code and XEX content are never included.
#include <cstddef>
#include <cstdint>
struct PPCRegister {
 union { std::uint64_t u64; std::uint32_t u32; };
 PPCRegister() : u64(0) {}
};
struct PPCContext {
 std::uint32_t calls=0;
 PPCRegister r1{},r3{},r4{},r5{},r6{},r7{},r8{},r9{},r12{},r13{};
 PPCRegister r14{},r15{},r16{},r17{},r18{},r19{},r20{},r21{},r22{},r23{},r24{},r25{},r26{},r27{},r28{},r29{},r30{},r31{};
 PPCRegister f14{},f15{},f16{},f17{},f18{},f19{},f20{},f21{},f22{},f23{},f24{},f25{},f26{},f27{},f28{},f29{},f30{},f31{};
 std::uint64_t lr=0;
};
typedef void PPCFunc(PPCContext& __restrict ctx,std::uint8_t* base);
struct PPCFuncMapping { std::size_t guest; PPCFunc* host; };
extern PPCFuncMapping PPCFuncMappings[];
