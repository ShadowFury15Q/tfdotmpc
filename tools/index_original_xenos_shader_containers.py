#!/usr/bin/env python3
"""Index ORIGINAL Xenos shader containers into a private multi-shader cache.

This tool never generates placeholder shaders or guesses the original shader
layout. It reads the container's actual vertex-element reflection and matching
VFETCH instructions, and optionally runs XenosRecomp for each original shader.
No original shader bytes are written to this public repository.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys
from dataclasses import dataclass

USAGES = {
    0: "POSITION", 1: "BLENDWEIGHT", 2: "BLENDINDICES",
    3: "NORMAL", 4: "PSIZE", 5: "TEXCOORD", 6: "TANGENT",
    7: "BINORMAL", 8: "TESSFACTOR", 9: "POSITIONT",
    10: "COLOR", 11: "FOG", 12: "DEPTH", 13: "SAMPLE",
}
# Original Xenos hardware vertex formats (see Xenia xenos.h).
# Conversions requiring special signed/normalized or packed handling are
# omitted deliberately; the DX11 backend must reject them rather than guess.
FORMATS = {
    31: "float16x2", 32: "float16x4", 36: "float1",
    37: "float2", 38: "float4", 57: "float3",
}
MAX_SHADER_BYTES = 1 << 20
MAX_CONTAINER_BYTES = 64 << 20


class UnsupportedContainer(ValueError):
    pass


@dataclass(frozen=True)
class OriginalShader:
    stage: str
    hash: str
    layout: tuple[str, ...]


def be32(raw: bytes, pos: int) -> int:
    if pos < 0 or pos + 4 > len(raw):
        raise UnsupportedContainer("Truncated original Xenos container")
    return struct.unpack_from(">I", raw, pos)[0]


def fnv64(data: bytes) -> str:
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return f"{value:016X}"


def parse_original_container(data: bytes) -> OriginalShader:
    if len(data) < 36 or len(data) > MAX_CONTAINER_BYTES:
        raise UnsupportedContainer("Invalid original Xenos container size")
    (flags, virtual_size, _physical_size, _field_c, _constants,
     _definitions, shader_offset, _f1c, _f20) = struct.unpack_from(
        ">9I", data, 0)
    if flags not in (0x102A1100, 0x102A1101):
        raise UnsupportedContainer("No original Xenos shader container signature")
    if shader_offset < 36 or shader_offset + 24 > virtual_size or (
            virtual_size > len(data)):
        raise UnsupportedContainer("Invalid original shader metadata bounds")
    physical_offset = be32(data, shader_offset)
    instruction_bytes = be32(data, shader_offset + 4)
    code_start = virtual_size + physical_offset
    code_end = code_start + instruction_bytes
    if instruction_bytes == 0 or instruction_bytes > MAX_SHADER_BYTES or (
            instruction_bytes % 12) or code_end > len(data):
        raise UnsupportedContainer("Invalid original Xenos shader instruction range")
    original_code = data[code_start:code_end]
    stage = "vertex" if (flags & 1) else "pixel"
    result = OriginalShader(stage, fnv64(original_code), ())
    if stage == "pixel":
        return result
    if shader_offset + 36 > virtual_size:
        raise UnsupportedContainer("Truncated original vertex reflection")
    field18 = be32(data, shader_offset + 24)
    count = be32(data, shader_offset + 28)
    if count > 32:
        raise UnsupportedContainer("Original vertex layout exceeds host attribute limit")
    first_element = shader_offset + 36 + field18 * 4
    if first_element < shader_offset or first_element + count * 4 > virtual_size:
        raise UnsupportedContainer("Original vertex reflection exceeds metadata")
    layout = []
    seen = set()
    used_slots = {}
    for i in range(count):
        decl = be32(data, first_element + i * 4)
        instruction_address = decl & 0xFFF
        usage = (decl >> 12) & 15
        usage_index = (decl >> 16) & 15
        instruction_pos = instruction_address * 12
        if usage not in USAGES or instruction_pos + 12 > len(original_code):
            raise UnsupportedContainer("Unknown original vertex semantic/instruction")
        word0, word1, word2 = struct.unpack_from(
            ">III", original_code, instruction_pos)
        if (word0 & 31) != 0 or ((word0 >> 19) & 1) != 1:
            raise UnsupportedContainer("Reflected instruction is not original VFETCH")
        const_index = (word0 >> 20) & 31
        const_select = (word0 >> 25) & 3
        if const_select >= 3:
            raise UnsupportedContainer("Invalid original Xenos fetch constant selection")
        fetch_slot = const_index * 3 + const_select
        stride_words = word2 & 255
        offset_words = (word2 >> 8) & 0x7FFFFF
        if offset_words & 0x400000:
            offset_words -= 0x800000
        fmt = (word1 >> 16) & 63
        if fmt not in FORMATS:
            raise UnsupportedContainer(
                f"Original VFETCH format {fmt} requires a native format translator")
        if stride_words == 0 or stride_words > 1024 or offset_words < 0:
            raise UnsupportedContainer("Unsupported original indexed/minifetch geometry")
        stride = stride_words * 4
        offset = offset_words * 4
        widths = {"float1": 4, "float2": 8, "float3": 12,
                  "float4": 16, "float16x2": 4, "float16x4": 8}
        if offset + widths[FORMATS[fmt]] > stride:
            raise UnsupportedContainer("Original vertex fetch exceeds original stride")
        signature = (USAGES[usage], usage_index)
        if signature in seen or (fetch_slot in used_slots and
                                 used_slots[fetch_slot] != stride):
            raise UnsupportedContainer("Conflicting original vertex fetch layout")
        seen.add(signature)
        used_slots[fetch_slot] = stride
        layout.append(
            f"stream {fetch_slot} {stride} {signature[0]} {signature[1]} "
            f"{offset} {FORMATS[fmt]}")
    if not layout:
        raise UnsupportedContainer("Original shader has no reflected vertex elements")
    return OriginalShader(stage, fnv64(original_code), tuple(layout))


def index_containers(source: Path, output: Path, compiler: Path | None = None,
                     header: Path | None = None) -> dict:
    source = source.resolve()
    output = output.resolve()
    if not source.is_dir() or output == source or source in output.parents:
        raise ValueError("Input must be an existing directory outside output tree")
    if compiler is not None and (header is None or
                                 not compiler.is_file() or not header.is_file()):
        raise ValueError("XenosRecomp executable and shader_common.h required")
    catalog = {}
    failures = []
    for entry in sorted(source.rglob("*")):
        if not entry.is_file() or entry.suffix.lower() not in {".bin", ".xenos"}:
            continue
        if entry.stat().st_size > MAX_CONTAINER_BYTES:
            continue
        try:
            shader = parse_original_container(entry.read_bytes())
        except UnsupportedContainer:
            continue
        except OSError as exc:
            failures.append({"file": str(entry), "error": str(exc)})
            continue
        key = (shader.stage, shader.hash)
        if key in catalog:
            if catalog[key].layout != shader.layout:
                raise ValueError("Conflicting original shader reflection for same hash")
            continue
        catalog[key] = shader
        if compiler is not None:
            destination = output / shader.stage / (shader.hash + ".hlsl")
            if not destination.exists():
                destination.parent.mkdir(parents=True, exist_ok=True)
                staged = destination.with_suffix(".hlsl.partial")
                result = subprocess.run(
                    [str(compiler.resolve()), str(entry),
                     str(staged), str(header.resolve())],
                    capture_output=True, text=True, timeout=120, check=False)
                if result.returncode != 0 or not staged.is_file():
                    staged.unlink(missing_ok=True)
                    raise RuntimeError(
                        f"Original {shader.stage} shader {shader.hash} "
                        f"XenosRecomp failed: {result.stderr[-1000:]}")
                staged.replace(destination)
        if shader.stage == "vertex":
            dest = output / "layouts" / (shader.hash + ".layout")
            expected = "\n".join(shader.layout) + "\n"
            if dest.exists() and dest.read_text(encoding="utf-8") != expected:
                raise ValueError(f"Refusing to replace different original VFETCH layout: {dest}")
            dest.parent.mkdir(parents=True, exist_ok=True)
            if not dest.exists():
                dest.write_text(expected, encoding="utf-8")
    if not catalog:
        raise RuntimeError("No original Xenos containers recognized (no cache written)")
    output.mkdir(parents=True, exist_ok=True)
    manifest = {
        "vertex_shaders": sum(1 for x in catalog if x[0] == "vertex"),
        "pixel_shaders": sum(1 for x in catalog if x[0] == "pixel"),
        "compiled_hlsl": compiler is not None,
        "failures": failures,
        "entries": [
            {"stage": stage, "fnv64": h, "attributes": len(item.layout)}
            for (stage, h), item in sorted(catalog.items())
        ],
    }
    (output / "index.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original_shader_container_directory", type=Path)
    parser.add_argument("private_cache_directory", type=Path)
    parser.add_argument("--xenosrecomp", type=Path)
    parser.add_argument("--header", type=Path)
    args = parser.parse_args()
    try:
        manifest = index_containers(
            args.original_shader_container_directory, args.private_cache_directory,
            args.xenosrecomp, args.header)
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as exc:
        parser.exit(1, f"Original Xenos shader indexing failed: {exc}\n")
    print(f"Indexed {manifest['vertex_shaders']} VS + "
          f"{manifest['pixel_shaders']} PS original Xenos shader containers")
    return 0


if __name__ == "__main__":
    sys.exit(main())
