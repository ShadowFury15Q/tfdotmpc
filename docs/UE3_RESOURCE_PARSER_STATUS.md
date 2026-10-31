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

## Stage 8 — Native UE3 export map and object-reference validation (28 Oct 2026)

- Reverse-engineered the DOTM-specific UE3 licensee 174 export serialization: a 45-byte mandatory record plus an optional 1-byte flag-dependent extension.
- Extended records contain generation counts, a 16-byte GUID and a 4-byte metadata field.
- Validated **108,220 exported objects** across **17 original Xbox 360 .xxx packages**.
- **422** records use the optional extension. All export maps end at the package-specified DependsOffset.
- Native C++ reader now exposes export metadata, serialized-object byte ranges, class/super/outer/archetype references and FName-qualified object paths.
- Ran the GitHub-built native Linux probe **dotm_ue3_probe --verify-all** privately on all 17 original game packages: **108,220 object references checked, zero failures, exit code 0**.
- Synthetic asset-free unit tests cover export flags, generation counts, LZO, object data views and cyclic reference rejection.
- Game archives and generated game code remain private; public repository contains source code and tests only.

**Not a playable port yet.** Object metadata and raw data ranges are readable, but actual UObject serialization, renderer, audio and Xbox 360 platform services remain unimplemented.
## Stage 9 — Serialized UObject properties and Texture2D metadata (28 October 2026)

- Inspected the original game object streams. DOTM uses a 4-byte UObject net index followed by big-endian tagged properties, terminated by the FName `None`.
- Corrected DOTM tag semantics: `BoolProperty` stores a 32-bit boolean inside the tag with zero data length, `StructProperty` has an 8-byte FName struct subtype, and `ByteProperty` has no extra header enum FName. Treating bools as 1 byte caused misalignment.
- Added `native/ue3_properties.hpp` / `.cpp` with bounded parsing of tagged properties, safe payload views and typed big-endian integer/float reads.
- Added `native/ue3_texture.hpp` / `.cpp` to decode original Texture2D dimensions, original dimensions, mip-tail base index, format FName and native data span. It does NOT convert the Xbox GPU mip/tiled compression layout to Windows textures.
- Native C++ probe `dotm_ue3_probe --verify-tags` validated **1,428 gameplay assets, 9,273 tagged properties, zero failures** across all 17 private, original Xbox 360 UE3 packages.
- Native C++ probe `dotm_ue3_probe --verify-textures` validated **760 Texture2D exports, zero failures**: PF_DXT1 = 259, PF_DXT5 = 474, PF_A8R8G8B8 = 19, PF_G8 = 8.
- Synthetic-property C++ tests cover 32-bit bools, ints, vector floats, enum name values, native object trailing data and invalid name indices; tests and compilation pass on Windows and Linux cloud runners.
- CI run: https://github.com/ShadowFury15Q/tfdotmpc/actions/runs/40746609587
- Original game assets and per-object metadata have not been uploaded to the public repository; local original-file verification was done privately.

**Status: not a playable native PC build.** Actual UObject class-native data, Xenos swizzled/tiled texture mip payloads, model rendering, Xbox 360 platform APIs and game boot remain to be implemented and tested.
## Stage 10 — Xbox 360 Texture2D mip loading and native DXT-to-DDS conversion (28 Oct 2026)

- 760/760 original Texture2D objects, 3,570 mip records validated, zero metadata errors.
- 1,369 embedded GPU-tiled mips with uncompressed UE3 bulk storage, 1 embedded LZO mip, 440 externally streamed mips, and 1,760 nonresident mip placeholders.
- Native C++ decoder `native/ue3_mip.cpp` reads flags 0x00 (inline), 0x10 (LZO), 0x11 (external), 0x21 (missing) and enforces exact logical-offset/data-length consistency.
- Native C++ probe `dotm_ue3_probe --verify-mips` processed 760 textures/3,570 mip entries and loaded 40,185,856 embedded mip bytes on original private DOTM game files, zero errors, exit code 0.
- `native/ue3_dds.cpp` detiles Xbox 360 Xenos DXT1 / DXT5 BC blocks, swaps their 16-bit word byte order and writes standard 128-byte-header DDS assets.
- `native/dotm_dds_export` produced **600 PC-readable DDS textures** from the private 17-package boot asset collection: **0 conversion errors**; 160 assets remain nonresident in this subset or use formats not yet supported.
- Independently created Python DDS and native C++ DDS were **byte-identical for 8 matched textures**. All 600 exported DDS files successfully loaded and decompressed with Pillow.
- Synthetic unit tests check bulk flags, inline/LZO payloads, missing external data refusal, BC1/BC3 tiling and DDS header consistency on Windows and Linux.
- CI: https://github.com/ShadowFury15Q/tfdotmpc/actions/runs/40749241765 — both Windows and Ubuntu builds/tests successful.
- A private ZIP with 600 DDS resources and preview PNGs was delivered to the game owner; original commercial game resources are NOT committed to this public GitHub repository.

**Still not a playable native port.** Windows graphics API integration and Unreal Engine UObject/material/mesh loaders, audio, import stubs and game startup are not implemented at game-ready fidelity.
## Still missing for a playable native port
- Resolve native UE3 package export objects (currently only headers, name and import metadata are decoded).
- Native UE3 object/asset serialization and game-specific engine integrations.
- Xbox 360 game OS services, renderer, audio system and input.
- Link the recompiled game code with a working Windows runtime, boot and test actual gameplay.

**This is NOT yet a working or playable PC version of Transformers: Dark of the Moon.**

No original copyrighted XEX, UE3 packages, game art, or generated game source is committed to this public repository.
