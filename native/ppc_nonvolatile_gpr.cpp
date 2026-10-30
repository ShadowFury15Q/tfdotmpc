#include "ppc_recomp_shared.h"
#include "ppc_nonvolatile_gpr.hpp"

// The original XenonRecomp output declares these Xbox 360 ABI helpers but
// does not emit their bodies. Implement them instead of fail-fast stubs.
#define DOTM_DEFINE_GPR_HELPERS(N) \
    PPC_FUNC(__savegprlr_##N) { dotm::ppc::save_gprlr(ctx, base, N); } \
    PPC_FUNC(__restgprlr_##N) { dotm::ppc::restore_gprlr(ctx, base, N); }

DOTM_DEFINE_GPR_HELPERS(14)
DOTM_DEFINE_GPR_HELPERS(15)
DOTM_DEFINE_GPR_HELPERS(16)
DOTM_DEFINE_GPR_HELPERS(17)
DOTM_DEFINE_GPR_HELPERS(18)
DOTM_DEFINE_GPR_HELPERS(19)
DOTM_DEFINE_GPR_HELPERS(20)
DOTM_DEFINE_GPR_HELPERS(21)
DOTM_DEFINE_GPR_HELPERS(22)
DOTM_DEFINE_GPR_HELPERS(23)
DOTM_DEFINE_GPR_HELPERS(24)
DOTM_DEFINE_GPR_HELPERS(25)
DOTM_DEFINE_GPR_HELPERS(26)
DOTM_DEFINE_GPR_HELPERS(27)
DOTM_DEFINE_GPR_HELPERS(28)
DOTM_DEFINE_GPR_HELPERS(29)
DOTM_DEFINE_GPR_HELPERS(30)
DOTM_DEFINE_GPR_HELPERS(31)
#undef DOTM_DEFINE_GPR_HELPERS
