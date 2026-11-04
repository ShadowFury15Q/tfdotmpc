# Live original Xenos GPU submission (first DRAW, Windows)

The private, **original** PPC/Xbox 360 ring worker now supports the opt-in
\`Dx11LiveFirstDraw\` backend. This is not the offline capture replay. It uses
the actual \`GpuDrawCommand\` passed by PM4 \`DRAW_INDX_2\` and copies 84 bytes
of real guest physical vertex DMA *during the draw-scoped callback*.
It verifies the original first rectangle initiator 0x00030088, the vertex
binding and both 64-bit shader fingerprints before accepting the command.
The two private shaders must be translated from the corresponding original
guest Xenos microcode (see \`tools/recompile_original_first_draw.py\`).
The renderer sends those original translated shaders through D3D11
VS / geometry rectangle expansion / PS and presents a one-draw image.

On Windows, set \`DOTM_PRIVATE_LIVE_HLSL_DIR\` to the directory with private
\`vertex.hlsl\` and \`pixel.hlsl\` and optionally set
\`DOTM_PRIVATE_LIVE_FIRST_DRAW_BMP\` to a new .bmp path for the real GPU
readback. Use these with the existing *private* \`dotm_private_link_probe
--run-entry ...\` workflow. Both files and the original executable stay outside
the public repository. \`DOTM_PRIVATE_DRAW_CAPTURE_DIR\` and the live renderer
are mutually exclusive modes. Missing shaders or unrecognized original draw
fail closed, leaving the PM4 ring pending rather than reporting success.

This is **not** a completed original frame, game menu or playable campaign.
Only the first verified rectangle draw is supported, its presentation happens
per draw rather than via a proper guest frontbuffer swap, and other shaders,
GPU register state, textures, depth/EDRAM and frame composition are not yet
implemented. Validation requires running the private original title and
inspecting output on a Windows D3D11 GPU. CI compiles the GPU backend and
unit-tests endian/shape validation but cannot access proprietary game assets.
