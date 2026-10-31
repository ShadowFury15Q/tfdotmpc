# DOTM native PC port — Stage 13: actual recompilation/host ABI bridge
Date: 2026-10-28

**Not playable. No actual game boot or game-code linkage was claimed or performed.**

## Original private game code audit
Audited the pre-existing, privately generated `ppc/` C++ from the user's own Xbox 360 `default.xex`:

| Item | Checked count |
| --- | ---: |
| `ppc_recomp.N.cpp` translation units | 696 |
| Guest-to-host entries in `PPCFuncMappings` | 178,330 |
| Unique function addresses | 178,330 |
| Missing recompiled guest implementations (excluding Xbox OS imports) | 0 |
| Original startup mapping | `0x8277F670 -> _xstart` |
| Xbox 360 external platform imports in mapping | 250 |
| Guest save/restore ABI helper declarations | 236 |

**Important:** the 250 Xbox 360 OS APIs and save/restore register helpers are currently **not implemented** to game-ready fidelity. The 178,330 symbols are mapping entries, not a count of unique game features.

## New native Windows bridge
- `native/guest_memory.hpp`: corrected `NativeFunction` ABI from inaccurate `void*` to `void(PPCContext&, uint8_t*)`, exactly matching XenonRecomp's caller type (forward declaring `PPCContext`).
- `native/recomp_bridge.hpp/.cpp`: validates duplicate/out-of-range/null addresses before installing function pointers into XenonRecomp's 4 GiB guest address space and 8-byte host pointer dispatch table. Requires the _real_ `_xstart` address.
- `native/compiled_ppc_adapter.hpp/.cpp`: adapter for XenonRecomp's generated `PPCFuncMappings[]`, with compile-time verification of the actual `PPCFunc*` ABI. The adapter expects proprietary generated code only in the *private* working tree.
- `native/test_recomp_bridge.cpp` and `native/test_compiled_ppc_adapter.cpp`: Windows/Linux tests use **synthetic guest functions**, invoking a test entry through the native dispatch table and writing to guest memory. They verify unknown-address fallback and the original DOTM entry address. **These tests do not execute the original game.**
- `scripts/audit_private_recomp.py`: local-only private source audit, reporting entry, map and linker blocker counts without uploading game code.

## Validation
- GitHub Actions Windows 2022 and Ubuntu 24.04 CI run: https://github.com/ShadowFury15Q/tfdotmpc/actions/runs/40756195636
- The shared source and test fixtures can compile publicly; **actual private game source is not in GitHub Actions**.
- Native game entry `_xstart` is **not yet safely callable** because Xbox 360 runtime state (threads, stack/TLS, memory, GPU, audio, scheduler, imported XAM/kernel APIs) is missing.

## Next developer tasks
1. Build a private linkage target for all generated C++ using the validated mapping adapter. Keep proprietary game source local.
2. Implement and verify `__savegprlr_*`, `__restgprlr_*`, `__savefpr_*`, `__restfpr_*`, VMX save/restore helpers with actual XEX ABI semantics.
3. Identify first imported Xbox kernel calls via trace of **controlled, safe** entry startup. Implement them with correct calling conventions, error codes and memory semantics, not fake-success stubs.
4. Guest stack, TLS, kernel initialization, memory allocations and synchronization.
5. Integrate original game code with resource loader, Direct3D 11 renderer, audio and input.
6. Verify title boot and real level rendering/gameplay. No delivery date is guaranteed.

Public repo contains no copyrighted original XEX, ISO, generated game C++, or game assets.
