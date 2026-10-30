#!/usr/bin/env python3
"""Read-only local preflight for DOTM private PPC linkage; never uploads game data."""
import argparse
import hashlib
from pathlib import Path
import re
import struct
import sys

EXPECTED_XEX_SHA256 = "6fd9ae9fb94ea8e9cc91a6a0cc62b04441411bdafe2479f0d380d4a3a9957ca6"
EXPECTED_ENTRY = 0x8277F670
EXPECTED_IMAGE_BASE = 0x82000000
EXPECTED_UNITS = 697
REQUIRED = ("ppc_recomp_shared.h", "ppc_context.h", "ppc_func_mapping.cpp")
UNIT_NAME = re.compile(r"ppc_recomp\.(\d+)\.cpp$")
SKIP_DIRS = {".git", "node_modules", "native-build", "build", ".venv", "__pycache__"}


def candidate_directories(root: Path):
    """Bounded walk: no full-drive recursion or reading generated C++ bodies."""
    if not root.is_dir():
        return
    todo = [(root, 0)]
    while todo:
        directory, depth = todo.pop()
        yield directory
        if depth >= 4:
            continue
        try:
            children = sorted((p for p in directory.iterdir() if p.is_dir()
                               and p.name not in SKIP_DIRS), reverse=True)
        except OSError:
            continue
        todo.extend((child, depth + 1) for child in children)


def inspect_units(directory: Path):
    try:
        names = {p.name for p in directory.iterdir() if p.is_file()}
    except OSError:
        return None
    indices = {int(match.group(1)) for name in names
               if (match := UNIT_NAME.fullmatch(name))}
    if not indices and not any(name in names for name in REQUIRED):
        return None
    missing = [i for i in range(EXPECTED_UNITS) if i not in indices]
    extra = sorted(i for i in indices if i >= EXPECTED_UNITS)
    headers = [name for name in REQUIRED if name not in names]
    return len(indices), missing, extra, headers


def pe_entry(path: Path):
    """Inspect only fixed-size PE headers; do not expose or copy executable bytes."""
    try:
        with path.open("rb") as stream:
            dos = stream.read(64)
            if len(dos) < 64 or dos[:2] != b"MZ":
                return None
            nt = struct.unpack_from("<I", dos, 0x3c)[0]
            if nt > 0x100000:
                return None
            stream.seek(nt)
            header = stream.read(128)
        if len(header) < 88 or header[:4] != b"PE\0\0":
            return None
        if struct.unpack_from("<H", header, 4)[0] != 0x01F2:
            return None
        if struct.unpack_from("<H", header, 24)[0] != 0x010B:
            return None
        image = struct.unpack_from("<I", header, 24 + 28)[0]
        entry = struct.unpack_from("<I", header, 24 + 16)[0]
        return image, image + entry
    except (OSError, ValueError, struct.error):
        return None


def sha256(path: Path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run(root: Path):
    print(f"Private DOTM preflight root: {root}")
    if not root.is_dir():
        print("BLOCKED: local project directory is not accessible")
        return 2
    found_units = []
    found_xex = []
    found_pe = []
    for directory in candidate_directories(root):
        inspected = inspect_units(directory)
        if inspected is not None:
            found_units.append((directory, inspected))
        try:
            files = (p for p in directory.iterdir() if p.is_file())
            for path in files:
                if path.name.lower() == "default.xex":
                    found_xex.append(path)
                elif path.suffix.lower() in (".pe", ".exe", ".bin"):
                    if (info := pe_entry(path)) is not None:
                        found_pe.append((path, info))
        except OSError:
            continue
    ready = False
    for directory, (count, missing, extra, headers) in found_units:
        print(f"PPC directory: {directory} ({count} numbered units)")
        if missing:
            print(f"  Missing numbered units: {len(missing)}; first: {missing[:8]}")
        if extra:
            print(f"  Unexpected indices >=697: {extra[:8]}")
        if headers:
            print(f"  Missing required headers/mapping: {', '.join(headers)}")
        if not (missing or extra or headers):
            ready = True
            print("  READY: 697 contiguous units and required headers present (not yet compiled)")
            print(f'  Configure: cmake -S native -B private-build -DDOTM_PRIVATE_PPC_DIR="{directory}"')
            print("  Build: cmake --build private-build --target dotm_private_link_probe --config Release")
    if not found_units:
        print("BLOCKED: no generated PPC units in this project tree")
    for path in found_xex:
        try:
            match = sha256(path) == EXPECTED_XEX_SHA256
            print(f"Original XEX: {path} (SHA256 {'MATCH' if match else 'MISMATCH'})")
        except OSError as error:
            print(f"Original XEX: {path} (unreadable: {error})")
    if not found_xex:
        print("Original default.xex: not found in this project tree")
    valid_pe = False
    for path, (image, entry) in found_pe:
        match = image == EXPECTED_IMAGE_BASE and entry == EXPECTED_ENTRY
        print(f"PPC PE: {path} (base=0x{image:08X}, entry=0x{entry:08X}, {'MATCH' if match else 'MISMATCH'})")
        valid_pe |= match
    if not valid_pe:
        print("BLOCKED: no decrypted Xbox PPC PE with original _xstart entry")
        if found_xex:
            print("  Extract locally with xex2ool basefile <default.xex> -o <private-game.pe>")
            print("  Alternative: XexTool -b <private-game.pe> <default.xex>")
    if ready and valid_pe:
        print("Private inputs found. Compile and run the probe with --run-entry <private-game.pe>.")
        return 0
    return 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path, nargs="?", default=Path.cwd(),
                        help="Local DOTM project root (not the whole drive)")
    return run(parser.parse_args().root)


if __name__ == "__main__":
    sys.exit(main())
