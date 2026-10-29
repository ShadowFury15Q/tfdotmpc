#pragma once
// PUBLIC TEST FIXTURE ONLY. No original DOTM game code or assets.
#include "ppc_context.h"
#define PPC_FUNC(name) void name(PPCContext& ctx, std::uint8_t* base)
#define PPC_FUNC_IMPL(name) PPC_FUNC(name)
#define PPC_EXTERN_FUNC(name) extern PPC_FUNC(name)
PPC_EXTERN_FUNC(_xstart);
PPC_EXTERN_FUNC(__imp__XamInputGetState);
PPC_EXTERN_FUNC(__imp__XamInputGetCapabilities);
PPC_EXTERN_FUNC(__imp__NtAllocateVirtualMemory);
PPC_EXTERN_FUNC(__imp__NtFreeVirtualMemory);
PPC_EXTERN_FUNC(__imp__KeWaitForSingleObject);
