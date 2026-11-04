# Native original PM4 GPU batching (Windows) — verified technical scope

This port now has a **single streaming GPU backend**, not a new renderer per frame or per draw.

The original PowerPC runtime's `GpuDrawCommand` is delivered synchronously
from the PM4 command processor. The renderer identifies the original VS/PS pair
by hashing the REAL big-endian shader instruction words; the same compiled
shader pair can be reused indefinitely across draws, frames and guest physical
buffers. The host decoder supports genuine Xenos point/line/triangle topologies
and auto-indexed and DMA-indexed geometry, including multiple fetch streams.
Each draw copies only its referenced live original vertex DMA (bounded and
byte-swapped appropriately) while the guest memory read window is valid.

The host DX11 backend retains compiled shader pipelines in a 512-entry
session cache. Rather than calling Present after each draw, it accumulates
ordered original draw submissions until the observed validated guest VdSwap
request, then submits a *preview* composition. The DX11 device remains alive
for repeated frames. VdSwap only constructs guest PM4 fetch data and is not
a substitute for a correct original EDRAM resolve or a true Xbox scanout.
No synthetic shader instruction, invented game model or fabricated frame is
accepted into the pipeline.

## Private shader layout and cache (outside this public repository)

`DOTM_PRIVATE_LIVE_HLSL_DIR` points to an existing private cache root.
Supply the ORIGINAL shaders translated to HLSL as:

- `vertex/<VERTEX_SHADER_FNV64>.hlsl`
- `pixel/<PIXEL_SHADER_FNV64>.hlsl`
- `layouts/<VERTEX_SHADER_FNV64>.layout`

Hashes are 16-digit uppercase hexadecimal FNV-1a over original shader words
in big-endian byte order. Each layout describes real vertex-fetch semantics,
using one entry per attribute in the form
`stream <fetch-slot> <stride-bytes> <SEMANTIC> <index> <byte-offset> <format>`.
For example, for a shader whose actual input uses POSITION0 in vertex fetch
slot 0 with a 16-byte float4: `stream 0 16 POSITION 0 0 float4`.
Do not invent declarations: derive slots, strides, offsets, formats and semantics
from original Xenos VFETCH instructions and original shader reflection.

Recognized host formats include float1/2/3/4, unorm8x4, uint8x4,
sint16x2/4, unorm16x2/4 and float16x2/4. Unknown formats, shader resources,
visibility queries, major modes, texture samplers, depth/EDRAM operations
and unrecognized rectangle shader signatures deliberately fail rather than
pretend that the original draw was rendered correctly. Only the known
original rectangle shader pair may reuse the legacy `vertex.hlsl` and
`pixel.hlsl` names, and only that pair has a built-in VFETCH layout.

**Compatibility caution:** Upstream XenosRecomp primarily targets DX12 DXIL
and Vulkan SPIR-V, not Direct3D 11 FXC. Some translated HLSL cannot compile
for SM5 and must not be replaced with placeholder shaders. An eventual
full-native production pipeline should use a DX12/Vulkan shader execution
layer (with real resource binding, texture tiling, EDRAM and scanout) rather
than assuming this limited DX11 bridge can render the entire game.

### Generate the cache without writing every shader layout by hand

Run `python tools/index_original_xenos_shader_containers.py <private-original-shader-containers> <private-cache> --xenosrecomp <XenosRecomp> --header <shader_common.h>`.
The indexer scans genuine original Xenos binary shader containers,
derives each shader FNV64 identity, decodes vertex element reflection and
the original matching 96-bit VFETCH instructions, generates
`layouts/<hash>.layout`, and invokes upstream XenosRecomp for each shader.
For a metadata-only audit, omit both compiler options. Indexed shaders
with formats or vertex addressing beyond implemented DX11 support are
**rejected**, not provided with fake host layouts. The original files stay
outside GitHub. This cache is a preparation step, not proof of a running game.

To opt in to private live rendering on Windows, set
`DOTM_PRIVATE_LIVE_HLSL_DIR` and run the normal original PPC private boot.
Optional `DOTM_PRIVATE_LIVE_FIRST_DRAW_BMP` writes one readback of the
first accumulated presentable draw batch, without overwriting existing images.
Do not also set `DOTM_PRIVATE_DRAW_CAPTURE_DIR` (mutually exclusive).

### Verification / not yet proven

Cross-platform public CI tests PM4 draw planning, shader pair hashing, multiple
fetch slots, indexed bounds, unknown primitive rejection and other native
runtime behavior without distributing original game files. Windows CI builds
the real D3D11 backend. This does NOT prove original guest runtime rendering:
a completed game frame, coherent materials, depth or campaign boot requires
private game execution and actual GPU comparison to a retail capture.
