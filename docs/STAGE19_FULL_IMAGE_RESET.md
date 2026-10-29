# Stage 19 — Deterministic complete Xbox PPC PE image reset

Date: 2026-10-29. This is a **native host-memory correctness milestone**, not game startup.

## Bug

Stage 18 zeroed only mapped PE sections. Loading an image over reused guest memory could still retain stale data in the gaps between sections, the unused header area and other unlisted pages. This is not a valid clean fixed-base image. The original PE's reserved image span ends immediately before XenonRecomp's native indirect-function dispatch table, so clearing beyond that boundary would also corrupt native function pointers.

## Fix

After validating every PE section and its bounds, `GuestMemory::load_pe` commits and zeroes exactly `[kImageBase, kImageBase + kImageSize)`. It then copies the validated headers and each non-`.reloc` section's initialized bytes. The entire image is reset on every load; no dispatch table bytes are modified. A failure in OS memory commitment can still interrupt a load, so this is **not** a transactional commit/rollback implementation.

## Synthetic tests

The host-only guest memory test now dirties section padding, BSS, an unlisted image page and unused header bytes, then verifies they are cleared. It repeats the load after modifying guest memory again and verifies both previously installed native PPC function pointers survive. The existing malformed-PE validation tests remain in place. No original game bytes are used.

## Blockers

Actual private C++ linkage, Xbox kernel/XAM semantics, renderer, audio, original campaign startup and end-to-end playability are not verified. The public repository contains no XEX, ISO, proprietary generated C++ or original assets.
