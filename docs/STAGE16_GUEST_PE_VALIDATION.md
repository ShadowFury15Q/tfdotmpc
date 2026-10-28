# Stage 16 — Validate native guest PE image before mapping

Date: 2026-10-29. **No playable game or executed original entry point.**

The Xbox 360 fixed-base PE loader now validates the entire COFF/optional header and section table before committing any guest image bytes. It rejects truncated headers, out-of-file raw section data, virtual sections overlapping the PE headers, overlapping virtual sections, and any non-relocation section crossing the XenonRecomp native dispatch-table boundary. The fixed-base .reloc section remains intentionally unmapped.

The public memory test creates a synthetic Xbox PPC PE, checks mapping and zero-filled virtual data, and verifies six malformed variations are rejected without replacing previously mapped guest section bytes. No game resources or proprietary recompiled C++ are committed.

**Boundary:** This improves native guest-memory image validation, not game boot. The private full XenonRecomp link, Xbox kernel/XAM runtime, UE3 loading, GPU, audio and full campaign remain unverified.
