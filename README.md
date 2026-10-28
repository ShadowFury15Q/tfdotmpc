# Transformers: Dark of the Moon (Xbox 360) — native PC port research

**Status: NOT PLAYABLE.** A working `DOTM.exe` does not exist yet.

## Verified, 2026-10-28
- Original Xbox 360 title ID `415608A4`, SHA-256 `6fd9ae9fb94ea8e9cc91a6a0cc62b04441411bdafe2479f0d380d4a3a9957ca6`.
- Xbox 360 `default.xex` successfully processed with XenonRecomp.
- 36 previously unsupported PowerPC instruction mnemonics patched; diagnostics decreased from 4,048 to **0**.
- Corrected `vcmpbfp` CR6 record handling and linked-branch-through-LR (`blrl`).
- 16 manually analyzed function boundary overrides decreased 316 out-of-function switch errors to **0**.
- Record-bit warnings decreased from 149 to **0**.
- 697/697 generated C++ translation units passed Clang 17 syntax-only check. Latest `blrl` change was syntax-tested separately.
- C++ generation is not a linked Windows executable or tested runtime.
- Approximately 250 imported Xbox 360 platform functions remain to be implemented or mapped; memory, graphics, audio, I/O, and game-resource loading need native runtime support.

## Source and workflow
- `patches/dotm_instruction_cases.inc`: initial PowerPC instruction handlers.
- `patches/dotm_extra_cases.inc`: additional PowerPC instruction handlers.
- `scripts/apply_xenon_patch.py`: applies these to a fresh upstream XenonRecomp checkout, replaces `vcmpbfp` and `blrl` traps.
- `config/DOTM.toml`: verified diagnostic-clean function-boundary configuration for the specified Xbox 360 XEX.
- GitHub Actions builds patched XenonRecomp for Windows and Linux.

## Important
This public repository deliberately does **not** include the original game's `default.xex`, assets, or generated recompiled proprietary game source. The privately generated C++ package is delivered to the XEX owner separately.

Zero translator warnings and successful C++ syntax checking **do not demonstrate gameplay correctness**. In the generated output, one `vpkd3d128` debug trap remains in apparently unreachable code; that has not been proven safe by game execution.
