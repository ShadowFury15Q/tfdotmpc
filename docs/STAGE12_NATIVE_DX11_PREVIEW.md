# Stage 12 — Direct3D 11 native Windows mesh preview (28 Oct 2026)

**STATUS: Native Windows graphics preview compiled, but original game is NOT playable.**

## Verified

- Stage 11 native C++ implementation decoded renderable LOD0 vertices, index buffers, UVs and material section boundaries for all 32 StaticMesh objects across 17 original game packages.
- Native Linux `dotm_static_obj_export` generated **32 OBJ, 14,536 vertices and 13,540 triangles; exit code 0, zero errors** on private user-owned Xbox 360 UE3 packages.
- Compared C++ OBJ output against an independent Python parser: all 32 meshes match exactly for position/UV floating-point values and for triangle index topology after enabling maximum round-trip float precision.
- Github Actions `40753066899` produced cross-platform native geometry tools successfully.
- Added Windows-native `native/dx11_mesh_viewer.cpp` using Win32 + Direct3D 11: creates a native swapchain, depth buffer, shaders, vertex/index buffers and rotating camera, drawing a selected original StaticMesh LOD0 directly loaded from an original .xxx package. It does NOT invoke the Xbox 360 code or emulate the console.
- CI run `40753606298` successfully compiled and linked **Windows `dotm_dx11_mesh_viewer.exe`** and all other native tools, and passed the Windows/Linux CTest suites.
- **Graphics output itself was not runtime-tested in an interactive Windows desktop session.** Successful CI compilation is not proof of correct visual rendering.

## Private local usage of preview

The preview is an optional diagnostic. On a Windows desktop with the user's legitimately extracted original game data:

```powershell
.\dotm_dx11_mesh_viewer.exe "Q:\DOTM\TransGame\CookedXenon\TransGame.xxx" "AP_HealthPickup_STAT"
```

The command is only an example; actual files may be located elsewhere. Press Escape to close. It does not play the game.

The two archives containing proprietary `StaticMesh` OBJ exports and original files are **not** committed to this public repository. Only reverse-engineered tool source and synthetic tests are public.

## Missing core gameplay work

- Native Windows APIs compatible with imported Xbox 360 system services.
- Linking and executing recompiled original title code with a correct runtime, 64-bit memory layout, Xbox 360 ABI / guest threads, dynamic bindings.
- Game engine initialization, UE3 object lifecycle and package script native calls.
- Full material/texture streaming, shaders, sound, controls, physics, loading full game levels.
- Skeletal vertex buffers, skeleton, weights and animations.
- Full-game asset coverage (current original game subset is 17 of 2,284 .xxx packages).

**A native diagnostic viewer, native asset decoding, and a playable Windows game are distinct milestones.**
