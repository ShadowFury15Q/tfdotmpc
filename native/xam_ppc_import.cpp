#include "ppc_recomp_shared.h" // Private XenonRecomp header; synthetic fixture in public CI
#include "xam_guest_input.hpp"
#include "xam_system_version.hpp"
#include <cstdint>

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
