# First original DOTM Xenos draw — DX11 replay

The Windows target `dotm_dx11_original_draw_replay.exe` actually submits the captured **original** first rectangle's vertices through the privately translated original VS/PS to a native D3D11 device. A host geometry shader implements Xenos primitive 8 (rectangle list) **after** the translated vertex stage, instead of treating the three guest vertices as an ordinary triangle. No synthetic game shader, replacement mesh or fabricated DOTM frame is used.

This **is an offline draw replay, not a running game**. It does not execute the original PPC campaign, the full PM4 command ring, other Xenos primitives, depth/EDRAM, texturing, full frame composition or native live presentation. Compiling the target alone is not proof of a visible original draw. The first visible first-draw output can be confirmed only on a GPU with the matching private files; a complete original game frame needs additional draws and frontbuffer presentation.

## Private execution (Windows, no commercial data in git)

1. Run the original title's private recompilation/boot setup, with `DOTM_PRIVATE_DRAW_CAPTURE_DIR` set to a separate writable directory. This saves `first-original-draw/draw.json`, the exact real Xenos shader words and the decoded 84-byte original vertex fetch. A capture is deliberately rejected without live guest physical memory.
2. Recompile the captured shader pair with `python tools/recompile_original_first_draw.py <capture>/first-original-draw <private-hlsl-output> --xenosrecomp <XenosRecomp.exe> --header <XenosRecomp/shader_common.h>`.
3. Build with `cmake -S native -B native-build` and `cmake --build native-build --config Release --target dotm_dx11_original_draw_replay`.
4. Start `dotm_dx11_original_draw_replay.exe <capture>/first-original-draw <private-hlsl-output>`. The DX11 path compiles the actual `vertex.hlsl` and `pixel.hlsl` into native GPU shaders, uploads the original guest vertex DMA, performs the Xenos rectangle expansion and presents the result in its own window. If original shader translation cannot be compiled or the draw data is incomplete, **it fails** rather than displaying an invented frame.

DX11 compile compatibility strips only shader-stage and Vulkan-location decorations plus register-space annotations from the translated HLSL. Its translated shader instructions remain unchanged. This narrow path accepts only verified original first draw shader fingerprints and the 3 × 28-byte geometry. The original capture and translated shaders must stay private.

Next technical validation: run the private capture and replay on the actual original game data; inspect the shader-compiler diagnostics, compare visual output to the Xbox capture, then connect successful Xenos draw submits with renderer state, EDRAM/depth and frontbuffer scanout. Do not increment the project's full-game completion count solely for this diagnostic.
