#!/usr/bin/env python3
"""Extract user-owned Xbox 360 XDVDFS files without distributing game data.

Supports the same XGD partition layouts as extract_xex_from_iso.py.
Runs locally or in PRIVATE CI; refuses symlinks, path traversal and overwrite.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import tempfile

from extract_xex_from_iso import SECTOR, MAX_ROOT_TABLE, find_volume

NODE = struct.Struct("<HHIIBB")
BAD_NAME = re.compile(r'[\\/\x00-\x1f<>:"|?*]')
WINDOWS_RESERVED = re.compile(r"(?i)^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)")
MAX_ENTRIES = 200_000
MAX_DEPTH = 64


def safe_component(raw):
    name = raw.decode("latin-1")
    if not name or name in (".", "..") or name[-1] in (".", " ") or BAD_NAME.search(name) or WINDOWS_RESERVED.match(name):
        raise ValueError("Unsafe XDVDFS filename")
    return name


def check_range(image_size, base, sector, length):
    start = base + sector * SECTOR
    if start < base or length < 0 or start > image_size or length > image_size - start:
        raise ValueError("XDVDFS extent outside original ISO")
    return start


def list_entries(image):
    """Yield (relative Posix-style path, offset, byte length) for regular files."""
    base, sector, size = find_volume(image)
    image.seek(0, os.SEEK_END)
    image_size = image.tell()
    pending = [("", sector, size, 0)]
    seen_dirs = set()
    seen_paths = set()
    total_nodes = 0
    while pending:
        prefix, sector, size, depth = pending.pop()
        if depth > MAX_DEPTH or size <= 0 or size > MAX_ROOT_TABLE:
            raise ValueError("Invalid XDVDFS directory depth or size")
        directory_key = (sector, size)
        if directory_key in seen_dirs:
            raise ValueError("Cyclic or multiply referenced XDVDFS directory")
        seen_dirs.add(directory_key)
        offset = check_range(image_size, base, sector, size)
        image.seek(offset)
        table = image.read(size)
        if len(table) != size:
            raise ValueError("Truncated XDVDFS directory table")
        stack, visited_nodes = [0], set()
        while stack:
            position = stack.pop()
            if position in visited_nodes:
                raise ValueError("Cyclic XDVDFS directory tree")
            visited_nodes.add(position)
            total_nodes += 1
            if total_nodes > MAX_ENTRIES or position + NODE.size > len(table):
                raise ValueError("Invalid XDVDFS directory node")
            left, right, file_sector, length, attrs, name_length = NODE.unpack_from(table, position)
            end = position + NODE.size + name_length
            if not name_length or end > len(table):
                raise ValueError("Truncated XDVDFS directory filename")
            name = safe_component(table[position + NODE.size:end])
            path = prefix + name
            key = path.casefold()
            if key in seen_paths:
                raise ValueError("Duplicate XDVDFS path")
            seen_paths.add(key)
            check_range(image_size, base, file_sector, length)
            if attrs & 0x10:
                pending.append((path + "/", file_sector, length, depth + 1))
            else:
                yield path, base + file_sector * SECTOR, length
            for child in (right, left):
                if child:
                    child_offset = child * 4
                    if child_offset + NODE.size > len(table):
                        raise ValueError("XDVDFS child outside directory table")
                    stack.append(child_offset)


def extract(iso, destination, only=(), required=()):
    """Stream original assets in 1 MiB chunks; never replace existing outputs."""
    only = tuple(x.replace("\\", "/").strip("/") for x in only)
    required = tuple(x.replace("\\", "/").strip("/") for x in required)
    for item in (*only, *required):
        if not item or any(part in ("", ".", "..") or BAD_NAME.search(part) for part in item.split("/")):
            raise ValueError("Unsafe requested path")
    destination = Path(destination)
    extracted, total = set(), 0
    with Path(iso).open("rb") as image:
        for path, start, size in list_entries(image):
            if only and not any(path.casefold() == x.casefold() or
                                path.casefold().startswith(x.casefold() + "/") for x in only):
                continue
            target = destination.joinpath(*path.split("/"))
            if target.exists() or target.is_symlink():
                raise FileExistsError("Refusing to overwrite XDVDFS destination: " + str(target))
            # Existing parent links must not redirect writes outside the destination.
            root = destination.resolve()
            parent = target.parent
            parent.mkdir(parents=True, exist_ok=True)
            if not parent.resolve().is_relative_to(root):
                raise ValueError("XDVDFS parent symlink escapes extraction root")
            image.seek(start)
            temp_name = None
            try:
                with tempfile.NamedTemporaryFile(dir=parent, prefix=".xdvdfs-", delete=False) as out:
                    temp_name = out.name
                    left = size
                    while left:
                        data = image.read(min(left, 1024 * 1024))
                        if not data:
                            raise ValueError("Truncated XDVDFS file")
                        out.write(data)
                        left -= len(data)
                os.link(temp_name, target)
            finally:
                if temp_name is not None:
                    os.unlink(temp_name)
            extracted.add(path.casefold())
            total += size
    missing = [p for p in required if p.casefold() not in extracted]
    if missing:
        raise ValueError("Required original title content missing: " + ", ".join(missing))
    return len(extracted), total


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path)
    parser.add_argument("output_directory", type=Path)
    parser.add_argument("--only", action="append", default=[], help="Extract one file or directory (repeatable)")
    parser.add_argument("--require", action="append", default=[], help="Require a particular extracted file")
    args = parser.parse_args()
    try:
        count, total = extract(args.iso, args.output_directory, args.only, args.require)
    except (OSError, ValueError) as error:
        parser.exit(1, f"Private XDVDFS extraction FAILED: {error}\n")
    print(f"Extracted {count} original title files ({total} bytes) to {args.output_directory}")


if __name__ == "__main__":
    main()
