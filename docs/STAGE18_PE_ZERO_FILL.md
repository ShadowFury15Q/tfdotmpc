# Stage 18 — Deterministic Xbox PPC PE section initialization

Date: 2026-10-29. This is a **host-side memory correctness fix**, not a playable game milestone.

The native PE loader previously committed virtual section pages and copied initialized bytes without clearing the remaining virtual section. Windows `VirtualAlloc(MEM_COMMIT)` and Linux `mprotect` do not clear already-committed pages, so reloading a PE (or loading over an existing allocation) could retain stale guest memory in BSS and raw padding. This could cause nondeterministic initialization and obscure failures in the original recompiled code.

Every validated non-`.reloc` PE section is now zeroed across its full `max(VirtualSize, SizeOfRawData)` span before raw bytes are copied. Validation still completes before any guest image writes. The synthetic PPC PE regression pre-fills the reused section page with nonzero bytes, verifies zero-filled initialized padding and virtual-only BSS, then corrupts both and reloads the PE to confirm deterministic restoration. No original game bytes are used.

**Still blocked:** private XenonRecomp objects are not present in public CI, so genuine original-game C++ linkage, kernel/XAM services, graphics, audio and full campaign startup remain unverified. No proprietary source, game asset, XEX or ISO was committed.
