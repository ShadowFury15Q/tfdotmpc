# Stage 14 — Native Windows keyboard/mouse to original Xbox 360 XAM ABI
Date: 2026-10-28

## This is not a playable game
The original `_xstart` remains unexecuted. This changes the input subsystem only. The test target uses an independently constructed, **synthetic** PPCContext, not the generated original game's executable. Game-specific mapping, vehicle handling, menu controls, and sensitivity tuning remain to be validated during real mission gameplay.

## What's implemented

- `native/xam_input.hpp/.cpp`: receives Windows `NativeInput::next_frame()`, translates it to virtual Xbox 360 gamepad buttons, triggers and analog sticks, generates a real XInput packet number (increments only on changes), and explicitly writes the **16-byte, big-endian** `X_INPUT_STATE` serialized data expected by Xbox 360.
- `native/xam_guest_input.hpp/.cpp`: handles guest-memory copying of the XAM state, validates guest pointers, avoids writing into native XenonRecomp function dispatch table, and distinguishes gamepad user 0, any-user and not-connected users.
- `native/xam_ppc_import.cpp`: implements the exact PowerPC ABI for `__imp__XamInputGetState`, with `r3 = user, r4 = flags, r5 = guest state address` and Xbox return status in `r3`. The same file provides `__imp__XamInputGetCapabilities`, with a 20-byte big-endian XAM capabilities structure (virtual keyboard/mouse gamepad, **no** rumble).
- `native/generate_imports.py`: new optional `--native-xam-input` excludes these implemented functions from the automatically generated aborting Xbox platform stubs when linking PRIVATE generated XenonRecomp C++.
- `native/test_xam_input.cpp` and `native/test_xam_ppc_import.cpp`: test WASD diagonal normalization, mouse camera conversion, triggers, transform, packet-change semantics, user index and flag semantics, release-on-focus-loss, 16/20-byte big endian serialization, and r3-r5 original PPC import calling convention.
- Other Xbox 360 imports still fail-fast instead of pretending to be implemented.

## Initial PC key-to-controller layout

| Native input | Virtual Xbox gamepad |
| --- | --- |
| WASD | left stick X/Y |
| Relative raw mouse | right stick X/Y |
| Left mouse | right trigger |
| Right mouse | left trigger |
| Middle mouse | right shoulder |
| F | Y button (transform candidate) |
| Space | A |
| Q | left shoulder |
| V | right stick press |
| Shift | B |
| E | X |
| R | D-pad up |
| Esc | Start |

These are **provisional**, NOT yet proved correct for Transformers DOTM's robot, vehicle, abilities, or in-game menus. All should be configurable; the final adapter must map them to actual original game action semantics, and support mouse without Xbox-style stick acceleration when gameplay access allows.

## Important technical limits

- Win32 raw input is currently connected to the native DX11 mesh viewer, not a running game.
- Actual original `ppc_recomp.*.cpp` code is private and **not linked into GitHub Actions**.
- The input host must be bound with `bind_xam_guest_input(&service, &memory)` and be fed frames from Windows before calling original guest code; unbind before destruction.
- This is **2 supported Xbox XAM imports**, not complete kernel, graphics, audio or game startup.
- `XamInputGetCapabilitiesEx` and other XAM variants remain separate future work.
- Never publish the game's XEX/ISO/UE3 assets or generated commercial C++ to public GitHub.

## References
- Microsoft's Win32 `XINPUT_GAMEPAD` public ABI: https://learn.microsoft.com/en-us/windows/win32/api/xinput/ns-xinput-xinput_gamepad
- Independent Xbox 360 ABI descriptions and `XamInputGetState` signature, available in public Xenia project under BSD license. This port has its **own native implementation**, and does not execute Xenia or use emulation.
