# Stage 17 — Transactional native PPC dispatch installation

Date: 2026-10-29. **No playable game or executed original entry point.**

The XenonRecomp host function table now validates the complete requested batch before writing any 8-byte native function pointers. It rejects null native targets, out-of-range or unaligned original PPC addresses, and duplicate guest addresses. Previously, a late invalid entry could leave earlier entries modified, making subsequent indirect calls inconsistent.

The synthetic public regression test deliberately puts invalid, null, duplicate, and out-of-range mappings *after* a valid entry. Each rejection must leave both existing dispatch slots unchanged. It also checks a valid two-function batch is installed.

**Boundary:** This makes dispatch-table updates safer; it does not prove a private XenonRecomp link, original game boot, working input in the game, or playable campaign. No original assets, XEX, ISO or proprietary generated C++ were committed.
