#pragma once
// Public compiler compatibility for private XenonRecomp output.
// Do not edit generated PPC headers or publish game-derived translation units.
#include <x86/sse.h>

#ifndef SIMDE_MM_ROUND_MASK
#define SIMDE_MM_ROUND_MASK \
    (SIMDE_MM_ROUND_DOWN | SIMDE_MM_ROUND_UP | SIMDE_MM_ROUND_TOWARD_ZERO)
#endif
