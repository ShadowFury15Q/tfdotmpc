// Private-link only: native implementations of the 36 Xbox 360
// XenonRecomp floating-point nonvolatile save/restore ABI helpers.
#include "ppc_recomp_shared.h"
#include "ppc_nonvolatile_fpr.hpp"

#define DOTM_DEFINE_FPR_ABI(N) \
PPC_FUNC(__savefpr_##N) { dotm::ppc::save_fpr(ctx, base, N); } \
PPC_FUNC(__restfpr_##N) { dotm::ppc::restore_fpr(ctx, base, N); }

DOTM_DEFINE_FPR_ABI(14)
DOTM_DEFINE_FPR_ABI(15)
DOTM_DEFINE_FPR_ABI(16)
DOTM_DEFINE_FPR_ABI(17)
DOTM_DEFINE_FPR_ABI(18)
DOTM_DEFINE_FPR_ABI(19)
DOTM_DEFINE_FPR_ABI(20)
DOTM_DEFINE_FPR_ABI(21)
DOTM_DEFINE_FPR_ABI(22)
DOTM_DEFINE_FPR_ABI(23)
DOTM_DEFINE_FPR_ABI(24)
DOTM_DEFINE_FPR_ABI(25)
DOTM_DEFINE_FPR_ABI(26)
DOTM_DEFINE_FPR_ABI(27)
DOTM_DEFINE_FPR_ABI(28)
DOTM_DEFINE_FPR_ABI(29)
DOTM_DEFINE_FPR_ABI(30)
DOTM_DEFINE_FPR_ABI(31)

#undef DOTM_DEFINE_FPR_ABI
