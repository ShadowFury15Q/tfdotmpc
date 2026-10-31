// Private-link only: Xbox 360 nonvolatile GPR/LR helpers required by
// XenonRecomp's original DOTM entry point. No game-derived source here.
#include "ppc_recomp_shared.h"
#include "ppc_nonvolatile_gpr.hpp"

#define DOTM_DEFINE_GPR_ABI(N) \
PPC_FUNC(__savegprlr_##N) { dotm::ppc::save_gprlr(ctx, base, N); } \
PPC_FUNC(__restgprlr_##N) { dotm::ppc::restore_gprlr(ctx, base, N); }

DOTM_DEFINE_GPR_ABI(14)
DOTM_DEFINE_GPR_ABI(15)
DOTM_DEFINE_GPR_ABI(16)
DOTM_DEFINE_GPR_ABI(17)
DOTM_DEFINE_GPR_ABI(18)
DOTM_DEFINE_GPR_ABI(19)
DOTM_DEFINE_GPR_ABI(20)
DOTM_DEFINE_GPR_ABI(21)
DOTM_DEFINE_GPR_ABI(22)
DOTM_DEFINE_GPR_ABI(23)
DOTM_DEFINE_GPR_ABI(24)
DOTM_DEFINE_GPR_ABI(25)
DOTM_DEFINE_GPR_ABI(26)
DOTM_DEFINE_GPR_ABI(27)
DOTM_DEFINE_GPR_ABI(28)
DOTM_DEFINE_GPR_ABI(29)
DOTM_DEFINE_GPR_ABI(30)
DOTM_DEFINE_GPR_ABI(31)

#undef DOTM_DEFINE_GPR_ABI
