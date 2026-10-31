# Full native Windows campaign acceptance contract — DOTM

## Non-negotiable release target
A **native Windows x64 game executable** that plays the **complete original Transformers: Dark of the Moon (Xbox 360)** story from first playable scene to final credits. **No Xenia, RPCS3, or recreated replacement missions.** Keep the original gameplay, characters and their robot/vehicle modes, transformations, combat actions, enemies, bosses, level scripts, cinematics, voice acting, music, HUD, physics, checkpoints and saves.

This file defines the release objective. **None of the milestone requirements below is claimed complete.** Existing C++ compilers, texture converters and DX11 mesh viewers do not demonstrate game execution.

## Original campaign level groups to cover

Identified in the original Xbox 360 disc manifest; real mission boundaries/order must be confirmed during live game testing:

| Resource family | Observed name |
| --- | --- |
| L1 | `SAMERICA` |
| L2 | `DETROIT` |
| L3 | `MAYAN` |
| L4 | `VOLCANO` |
| L5 | `NEPAL` |
| L6 | `RUSSIA` |
| L7 | `SHOCKWAVE` |

The complete disc includes 2,284 Unreal Engine 3 `.xxx` packages. The research subset used so far contained only 17 boot packages. Full mission coverage requires loading the actual level packages and their external dependencies; don't treat L1-L7 folder names as proof of playable missions.

## Native keyboard and mouse — REQUIRED
- WASD movement; relative **Raw Input** mouse camera and aiming.
- Left click primary fire, right click aim (when relevant), middle click alternate fire (provisional defaults).
- A directly accessible transformation action; robot and vehicle modes each receive correct controls, not a one-size-fits-all robot movement scheme.
- Contextual actions for abilities, melee, boost/dash, jump, interact, reload, pause and menu navigation as required by each original playable character.
- Remappable binds, mouse sensitivity, inverted-Y option and persisted user settings; no gamepad required.
- Correct window focus behavior, key-release edges and pause/menu input routing.
- The current `native/native_input.hpp/.cpp` + `native/win32_input.cpp` represent only the **native host input foundation**. Mapping actions to the ORIGINAL GAME's Xbox control interface is **not done** and must be validated in actual game missions.

## Release milestones and acceptance evidence

1. **Recompiled code linked:** all private generated original game C++ object files + dispatch table linked into an x64 host executable without unresolved symbols; maintain private source ownership.
2. **System ABI and CPU state:** correctly implement PowerPC CPU register/stack/TLS/ABI helpers, Xbox 360 thread synchronization and imported OS services. Log tested API semantics rather than return fake success.
3. **Original boot:** successfully enter original `_xstart`, initialize game subsystems, reach original start menu. A synthetic entry or PE reader does NOT pass.
4. **Original content:** load the full disc's level/character/music/cutscene packages and streamed resources without placeholders.
5. **Native rendering:** translate original engine and GPU operations, display original models/textures/materials and all HUD/UI, correct animations and cinematics.
6. **Real mouse/keyboard gameplay:** translate native input into original title actions (movement, aiming, shooting, transforming, skills, vehicle controls, game menus), test rebinding and sensitivity in each control mode.
7. **Audio and saves:** original sound effects/music/dialogue and streaming cutscenes, checkpoints/save/load progression.
8. **End-to-end story:** play every original mission in order, defeat every mandatory boss and reach original credits, with no progress-blocking bugs. Record independent pass/fail checks for all level groups and mission transitions.

## Development policy
- Work iteratively and commit tested code with Windows GitHub Actions builds.
- Keep game XEX/ISO/proprietary game assets and generated commercial C++ **out of the public GitHub repository**.
- Do not describe a test fixture, recompiler, model viewer or separately exported mesh as a playable game.
- Do not promise a completion deadline or claim work happens continuously between scheduled runs. Report verified functionality and blockers precisely.
