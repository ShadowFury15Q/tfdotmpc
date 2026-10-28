# DOTM: original Xbox 360 Unreal Engine 3 resource parsing (28 Oct 2026)

## What was tested against the original game
- User-owned ISO: `Transformers - Dark of the Moon (USA) (En,Fr,De,Es,It).iso`.
- Private 30,879,540-byte `DOTM_BOOT_RESOURCES.zip` from the user's Google Drive, containing 20 original files.
- 20/20 original files matched the SHA-256 values in the Colab-created manifest.
- All 17 `.xxx` packages in the ZIP are valid Xbox 360 **big-endian UE3** package files (package version **537**, licensee version **174**).
- **Nine** of the 17 packages use UE3 **LZO1X** compressed chunks, all of whose inner blocks were decompressed and validated using `liblzo2`.
- All **17/17** name and import tables parsed with valid name indexes; `FObjectImport` serialized entry size was verified as **28 bytes**.
- Packages `TransGame.xxx`, `Engine.xxx`, `HM_Engine.xxx`, `Startup_int.xxx`, `UI_FrontEnd_m.xxx` and others were included.

## Native implementation
- `native/ue3_package.hpp` and `native/ue3_package.cpp` read big-endian UE3 package headers, reconstruct logical file contents from LZO1X blocks, parse name and import tables, and enumerate imported package names.
- `native/ue3_package_probe.cpp` prints metadata for any user-supplied Xbox 360 DOTM `.xxx` file without converting or publishing it.
- LZO1X decoder: pinned `AxioDL/lzokay`, MIT license, built as a C++ static library for Windows and Linux.
- `native/test_ue3_package.cpp` generates **synthetic**, non-proprietary packages for uncompressed and LZO compressed parsing tests.
- GitHub Actions Windows 2022 and Ubuntu 24.04 CI checks passed on source commit `0003c61b` (including synthetic UE3 and LZO tests).

## Dependency graph observed
- `TransGame.xxx` imports `Core`, `Demonware`, `Engine`, `GFxUI`, `HM_Engine`, `IpDrv`, `SND_SoundConfig_p`, `SoundEvents_Footsteps`.
- `Startup_int.xxx` additionally imports `ENV_AllShader_p`, `ENV_L7_Water_p`, `EngineResources`, `TR_T3_AllShader_p`, `UI_GFxCLIK_p`.
- `UI_FrontEnd_m.xxx` also imports `WL_DX_BUMBEE`.
- Those extra packages are present in the ISO manifest. Their contents were not included in the initial private 20-file ZIP.

## Still missing for a playable native port
- Resolve native UE3 package export objects (currently only headers, name and import metadata are decoded).
- Native UE3 object/asset serialization and game-specific engine integrations.
- Xbox 360 game OS services, renderer, audio system and input.
- Link the recompiled game code with a working Windows runtime, boot and test actual gameplay.

**This is NOT yet a working or playable PC version of Transformers: Dark of the Moon.**

No original copyrighted XEX, UE3 packages, game art, or generated game source is committed to this public repository.
