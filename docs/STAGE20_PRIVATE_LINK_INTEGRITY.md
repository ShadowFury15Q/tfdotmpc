# Stage 20 — Fail-closed validation of the private PPC link probe

Date: 2026-10-29. This is a **link-integrity milestone**, not a successful original-game link or game startup.

## Problem

The previous private linker stub generator could generate abort-on-call definitions for any declared but undefined PPC symbol. If an original game function were missing from the 696 private XenonRecomp translation units, the placeholder could hide that omission and allow an apparently successful link. Such a link would not establish completeness of the original recompiled campaign.

## Change

Before writing private-only stub source, the generator now parses the actual `ppc_func_mapping.cpp` and checks that:

- Guest mapping addresses are unique and the mapping is nonempty.
- The original `0x8277F670 -> _xstart` entry and its recompiled implementation exist.
- Every mapped non-platform guest function has a generated definition.
- Undefined non-platform declarations outside recognized PowerPC register-save/restore helper families are rejected rather than stubbed.

Fail-fast placeholders remain permitted **only** for `__imp__` Xbox platform imports and known PPC save/restore helpers. The two already implemented XAM input imports remain excluded. These placeholders are still **not** game-ready implementations.

## Verification and limits

Public tests use synthetic mapping tables to exercise missing game functions, duplicate guest addresses, missing `_xstart`, unmapped undefined game symbols and permitted platform/helper placeholders. Public Windows/Linux CI does not contain or compile the private original game code. A real Windows link of the 696 proprietary translation units is **not yet verified**.

The generator scans macro-shaped source text, not a full C++ AST; a successful check is only a pre-link completeness guard, not a substitute for a successful native linker invocation. The original XEX, ISO, game assets and generated proprietary C++ remain outside GitHub.

## Next blocker

Run the optional `dotm_private_link_probe` against the privately generated PPC directory, resolve any real linker errors, and replace fail-fast Xbox kernel/XAM and PPC ABI helpers with correctly implemented native runtime services before attempting safe original game startup.
