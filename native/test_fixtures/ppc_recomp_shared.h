#pragma once
#include "ppc_context.h"
#define PPC_FUNC(x) void x(PPCContext& ctx,std::uint8_t* base)
#define PPC_EXTERN_FUNC(x) extern PPC_FUNC(x)
