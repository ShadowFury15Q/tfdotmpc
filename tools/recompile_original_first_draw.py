#!/usr/bin/env python3
"""Translate the real first DOTM Xenos DRAW shaders privately (not a renderer)."""
import argparse
import json
import math
import os
from pathlib import Path
import struct
import subprocess

FINGERPRINTS = {"vertex": 0x539EA9E08AA83F0C, "pixel": 0x438C2AF84C78A133}
DWORDS = {"vertex": 27, "pixel": 9}


def fnv64(raw):
    value = 14695981039346656037
    for byte in raw:
        value = ((value ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return value


def be(*args):
    return struct.pack(">" + "I" * len(args), *args)


def wrap(stage, raw):
    if stage not in FINGERPRINTS or len(raw) != DWORDS[stage] * 4 or fnv64(raw) != FINGERPRINTS[stage]:
        raise ValueError("Not the verified original first-draw shader")
    constants = be(32, 28, 0, 0, 0, 28, 0, 0)
    interp = 5 << 4
    if stage == "vertex":
        # VFETCH instructions 3 and 4: POSITION0, COLOR0 from real stride 28.
        desc = (be(0, len(raw), 0, 0, 0, 1 << 5) +
                be(0, 2, 0) + be(3, 4 | (10 << 12)) + be(interp))
    else:
        desc = be(0, len(raw), 0, 0, 0, 1 << 5) + be(0, 1, interp)
    shader_offset = 36 + len(constants)
    header = be(0x102A1101 if stage == "vertex" else 0x102A1100,
                shader_offset + len(desc), len(raw), 0, 36, 0,
                shader_offset, 0, 0)
    return header + constants + desc + raw


def verify(root):
    info = json.loads((root / "draw.json").read_text(encoding="utf-8"))
    expected = {"initiator": 0x00030088, "primitive_type": 8,
                "source_select": 2, "index_count": 3,
                "vertex_shader_dwords": 27, "pixel_shader_dwords": 9}
    if any(info.get(k) != v for k, v in expected.items()):
        raise ValueError("Not the original first Xenos rectangle draw")
    bindings = info.get("vertex_fetch")
    if not isinstance(bindings, list) or len(bindings) != 1 or any(
            bindings[0].get(k) != v for k, v in
            {"slot": 0, "size_dwords": 21, "endian_mode": 2}.items()):
        raise ValueError("Unexpected original vertex buffer binding")
    vertices = (root / "vertex_fetch_slot_0.decoded.bin").read_bytes()
    if len(vertices) != 84:
        raise ValueError("Original three 28-byte vertices missing")
    xy = [struct.unpack_from("<2f", vertices, i * 28) for i in range(3)]
    if not all(math.isfinite(value) for pair in xy for value in pair):
        raise ValueError("Nonfinite original vertex coordinates")
    if not (xy[1][0] > xy[0][0] and xy[2][1] > xy[1][1]):
        raise ValueError("Invalid original rectangle geometry")
    return xy


def translate(capture, output, compiler, header):
    capture, output = capture.resolve(), output.resolve()
    project = Path(__file__).resolve().parent.parent
    if output == capture or capture in output.parents or output == project or project in output.parents:
        raise ValueError("Private output must be outside capture and public project")
    if output.exists():
        raise FileExistsError("Refusing to overwrite original shader output")
    xy = verify(capture)
    if not compiler.is_file() or not header.is_file():
        raise FileNotFoundError("XenosRecomp binary and shader_common.h required")
    output.mkdir(parents=True)
    containers = output / "containers"
    containers.mkdir()
    env = os.environ.copy()
    for key in ("LD_LIBRARY_PATH", "PATH"):
        env[key] = str(compiler.parent.resolve()) + os.pathsep + env.get(key, "")
    for stage in ("vertex", "pixel"):
        original = (capture / f"{stage}_shader.be").read_bytes()
        path = containers / f"{stage}.bin"
        path.write_bytes(wrap(stage, original))
        result = subprocess.run([str(compiler.resolve()), str(path),
                                 str(output / f"{stage}.hlsl"), str(header.resolve())],
                                env=env, capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise RuntimeError(f"Original {stage} shader translation: {result.stderr[-1000:]}")
    cache = output / "first_draw_cache.cpp"
    result = subprocess.run([str(compiler.resolve()), str(containers), str(cache),
                             str(header.resolve())], env=env, capture_output=True,
                            text=True, timeout=120)
    if result.returncode or not cache.is_file() or "g_shaderCacheEntryCount = 2;" not in cache.read_text():
        raise RuntimeError("Original shaders did not produce a two-entry SPIR-V cache")
    (output / "metadata.json").write_text(json.dumps({
        "first_rectangle_xy": xy, "shader_count": 2,
        "fingerprints": {stage: f"{value:016x}" for stage, value in FINGERPRINTS.items()},
        "caution": "Reflection reconstructed from real draw; GPU presentation not implemented"
    }, indent=2) + "\n", encoding="utf-8")
    return cache


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("capture_dir", type=Path)
    p.add_argument("output_dir", type=Path)
    p.add_argument("--xenosrecomp", type=Path, required=True)
    p.add_argument("--header", type=Path, required=True)
    args = p.parse_args()
    try:
        cache = translate(args.capture_dir, args.output_dir, args.xenosrecomp, args.header)
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        p.exit(1, f"Original Xenos translation FAILED: {error}\n")
    print(f"Two original first-draw shaders compiled: {cache}")


if __name__ == "__main__":
    main()
