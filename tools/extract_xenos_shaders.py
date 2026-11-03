#!/usr/bin/env python3
"""Extract original Xenos shader containers and optionally invoke XenosRecomp.

Reads only user-supplied extracted game files. Does not include game assets.
"""
import argparse
import hashlib
import json
import pathlib
import struct
import subprocess

HEADER = struct.Struct(">9I")
SIGNATURE = 0x102A1100
MAX_CONTAINER = 64 * 1024 * 1024


def find_containers(data):
    # Match the exact nine-field ShaderContainer layout from hedge-dev/XenosRecomp.
    for offset in range(0, len(data) - HEADER.size + 1, 4):
        flags, virtual_size, physical_size, _, constants, definitions, shader, zero1, zero2 = HEADER.unpack_from(data, offset)
        size = virtual_size + physical_size
        if (flags & 0xFFFFFF00) != SIGNATURE or zero1 or zero2:
            continue
        if size < HEADER.size or size > MAX_CONTAINER or size > len(data) - offset:
            continue
        if not (constants < virtual_size and definitions < virtual_size and shader < virtual_size):
            continue
        yield offset, data[offset:offset + size]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=pathlib.Path, help="Directory of extracted DOTM files")
    parser.add_argument("output", type=pathlib.Path, help="Private shader extraction directory")
    parser.add_argument("--xenosrecomp", type=pathlib.Path, help="Built XenosRecomp executable")
    parser.add_argument("--header", type=pathlib.Path, help="XenosRecomp shader_common.h")
    args = parser.parse_args()
    if bool(args.xenosrecomp) != bool(args.header):
        parser.error("--xenosrecomp and --header must be supplied together")
    if not args.input.is_dir():
        parser.error("Input must be an existing directory of extracted game files")
    args.output.mkdir(parents=True, exist_ok=True)
    records = []
    seen = set()
    for path in sorted(args.input.rglob("*")):
        if not path.is_file() or args.output == path or args.output in path.parents:
            continue
        try:
            data = path.read_bytes()
        except OSError as exc:
            print(f"Skipping unreadable file {path}: {exc}")
            continue
        for offset, blob in find_containers(data):
            digest = hashlib.sha256(blob).hexdigest()
            if digest in seen:
                continue
            seen.add(digest)
            binary = args.output / f"{digest}.bin"
            binary.write_bytes(blob)
            entry = {"source": str(path.relative_to(args.input)), "offset": offset,
                     "size": len(blob), "sha256": digest}
            if args.xenosrecomp:
                target = args.output / f"{digest}.hlsl"
                result = subprocess.run(
                    [str(args.xenosrecomp.resolve()), str(binary.resolve()),
                     str(target.resolve()), str(args.header.resolve())],
                    capture_output=True, text=True, check=False)
                entry["translator_exit_code"] = result.returncode
                if result.returncode:
                    entry["translator_error"] = (result.stderr or result.stdout)[-2000:]
            records.append(entry)
    (args.output / "manifest.json").write_text(
        json.dumps(records, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"Found {len(records)} unique Xenos shader containers in {args.input}")


if __name__ == "__main__":
    main()
