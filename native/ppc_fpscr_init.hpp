#pragma once

namespace dotm {
// Preserve the host FP exception masks when initializing a recompiled PPC
// context. XenonRecomp's FPSCR stores the full host MXCSR/FPCR, not just the
// guest rounding bits. A zero-initialized csr would unmask host exceptions.
template <typename Context>
void initialize_guest_fpscr(Context& context) noexcept {
    if constexpr (requires(Context& c) { c.fpscr.loadFromHost(); }) {
        (void)context.fpscr.loadFromHost();
    }
}
}
