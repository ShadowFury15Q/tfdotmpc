#pragma once
// PUBLIC TEST FIXTURE ONLY. No original DOTM game code or assets.
#include "ppc_context.h"
#define PPC_FUNC(name) void name(PPCContext& ctx, std::uint8_t* base)
#define PPC_FUNC_IMPL(name) PPC_FUNC(name)
#define PPC_EXTERN_FUNC(name) extern PPC_FUNC(name)
PPC_EXTERN_FUNC(_xstart);
PPC_EXTERN_FUNC(__imp__XamInputGetState);
PPC_EXTERN_FUNC(__imp__XamGetExecutionId);
PPC_EXTERN_FUNC(__imp__XamInputGetCapabilities);
PPC_EXTERN_FUNC(__imp__NetDll_XNetStartup);
PPC_EXTERN_FUNC(__imp__NetDll_XNetCleanup);
PPC_EXTERN_FUNC(__imp__NetDll_WSAStartup);
PPC_EXTERN_FUNC(__imp__NetDll_WSACleanup);
PPC_EXTERN_FUNC(__imp__XamLoaderSetLaunchData);
PPC_EXTERN_FUNC(__imp__XamLoaderGetLaunchDataSize);
PPC_EXTERN_FUNC(__imp__XamLoaderGetLaunchData);
PPC_EXTERN_FUNC(__imp__NtAllocateVirtualMemory);
PPC_EXTERN_FUNC(__imp__NtFreeVirtualMemory);
PPC_EXTERN_FUNC(__imp__KeInitializeApc);
PPC_EXTERN_FUNC(__imp__KeTlsAlloc);
PPC_EXTERN_FUNC(__imp__KeTlsFree);
PPC_EXTERN_FUNC(__imp__KeTlsGetValue);
PPC_EXTERN_FUNC(__imp__KeTlsSetValue);
PPC_EXTERN_FUNC(__imp__RtlInitializeCriticalSection);
PPC_EXTERN_FUNC(__imp__RtlInitializeCriticalSectionAndSpinCount);
PPC_EXTERN_FUNC(__imp__RtlEnterCriticalSection);
PPC_EXTERN_FUNC(__imp__RtlTryEnterCriticalSection);
PPC_EXTERN_FUNC(__imp__RtlLeaveCriticalSection);
PPC_EXTERN_FUNC(__imp__RtlImageXexHeaderField);
PPC_EXTERN_FUNC(__imp__XexCheckExecutablePrivilege);
// Link the actual native PPC nonvolatile helper objects in public CI.
PPC_EXTERN_FUNC(__savegprlr_28);
PPC_EXTERN_FUNC(__restgprlr_28);
PPC_EXTERN_FUNC(__savefpr_28);
PPC_EXTERN_FUNC(__restfpr_28);
