# DOTM native static mesh geometry — Stage 11 (2026-10-28)

**Status: game asset parsing works; full PC port is NOT yet playable.**

## Verification on the user's original Xbox 360 resources

- Read 17 private UE3 `.xxx` packages from the user's original DOTM Xbox 360 ISO.
- Verified **32 Engine.StaticMesh** objects with **one LOD each**.
- Decoded render LOD0 vertex buffers: **14,536 original 32-bit XYZ positions**.
- Decoded **40,620 original mesh indices**, corresponding to **13,540 render triangles**.
- Extracted primary UV0 coordinates, including 16-bit half UVs and 32-bit float UVs as appropriate. **28 meshes have 1 UV set, 4 have 2.**
- Parsed mesh material sections: 29 models have 1 section, 3 models have 2 sections.
- Parsed collision-tree metadata separately: **2,422 kDOP nodes** and **13,376 collision triangle references**.
- Extracted bounding volumes from **6 Engine.SkeletalMesh** objects, including `RB_Bumblebee_SKEL`. **Skeletal vertex buffers, bones, weights and animation were not decoded.**
- Native C++ verification of collision metadata: **32 StaticMesh, 6 SkeletalMesh; 0 errors; exit code 0**.
- Native C++ `dotm_static_obj_export` on the same 17 original packages: **32 OBJ, 14,536 vertices, 13,540 triangles; 0 errors; exit code 0**.
- Independent private Python parser/exporter generated the same 32 meshes. Compared 32 OBJ sets: vertex counts, UV counts, triangle count and topology match. The native C++ exporter was subsequently updated to write at maximum float round-trip precision.
- Native test on Windows and Linux: https://github.com/ShadowFury15Q/tfdotmpc/actions/runs/40752768189

## Code paths

- `native/ue3_mesh.hpp` / `.cpp`: kDOP collision nodes, triangle references, SkeletalMesh FBoxSphereBounds.
- `native/ue3_lod.hpp` / `.cpp`: DOTM-specific StaticMesh LOD0 serialization with material sections, original float32 positions, UV0/UV1, index buffers.
- `native/static_obj_export_main.cpp`: writes one OBJ per StaticMesh in original coordinates and scale; no recentering or reorientation.
- `native/test_ue3_mesh.cpp`: synthetic nonproprietary UE3 test with collision tree, embedded render vertices, UV half floats, 16-bit indices, skeletal bounds and corruption detection.

## Limitations

- The 32 meshes come **only from the initial 17 packages** extracted for boot analysis; this is not the full game's 2,284 packages, and does not contain all game actors or levels.
- OBJ currently exports primary UV channel, sections, and raw positions. Packed normals and materials are not yet reconstructed.
- Material sections preserve original signed object references, but materials are not loaded and no MTL is generated.
- 6 SkeletalMesh headers are recognized, but **no skeletal geometry/animation** is exported.
- No runtime mesh renderer integration, Xbox 360 platform API adaptation or in-game boot is implemented.
- Copyrighted original game assets and derived proprietary game data remain **private**, not committed to this public repo.
