#include "ppc_recomp_shared.h" // Private XenonRecomp header; synthetic fixture in public CI
#include "xam_guest_input.hpp"
#include "xam_system_version.hpp"
#include "kernel_guest_memory.hpp"
#include <cstdint>

// The original DOTM PPC call sites pass flags in r3 (zero), allocation
// bytes in r4 and a writable guest void** in r5. Return HRESULT in r3.
PPC_FUNC(__imp__XamAlloc) {
    (void)base;
    ctx.r3.u64 = dotm::xam_bound_allocate(ctx.r3.u32, ctx.r4.u32, ctx.r5.u32);
}
// XamFree takes the guest allocation pointer in r3 and has void ABI.
PPC_FUNC(__imp__XamFree) {
    (void)base;
    dotm::xam_bound_free(ctx.r3.u32);
}

// Xbox 360 XAM export called from ORIGINAL recompiled code (when linked).
// XenonRecomp PowerPC ABI: r3=user, r4=flags, r5=guest state pointer;
// r3 receives Xbox X_ERROR / Win32-compatible status.
PPC_FUNC(__imp__XamInputGetState) {
    const auto user=static_cast<std::uint32_t>(ctx.r3.u64);
    const auto flags=static_cast<std::uint32_t>(ctx.r4.u64);
    const auto output=static_cast<std::uint32_t>(ctx.r5.u64);
    ctx.r3.u64=dotm::xam_bound_input_get_state(user,flags,output);
}


PPC_FUNC(__imp__XamInputGetCapabilities) {
    const auto user=static_cast<std::uint32_t>(ctx.r3.u64);
    const auto flags=static_cast<std::uint32_t>(ctx.r4.u64);
    const auto output=static_cast<std::uint32_t>(ctx.r5.u64);
    ctx.r3.u64=dotm::xam_bound_input_get_capabilities(user,flags,output);
}

// XamGetSystemVersion has no arguments. Return the selected real retail
// dashboard version encoding, not the game XEX execution-ID version.
PPC_FUNC(__imp__XamGetSystemVersion) {
    (void)base;
    ctx.r3.u64 = dotm::xam_system_version();
}

// XGetLanguage is called during the original DOTM _xstart configuration path.
PPC_FUNC(__imp__XGetLanguage) {
    (void)base;
    ctx.r3.u64 = dotm::xam_console_language();
}
