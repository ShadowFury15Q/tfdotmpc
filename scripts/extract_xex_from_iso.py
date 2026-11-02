#!/usr/bin/env python3
"""Extract a user-owned Xbox 360 default.xex from XDVDFS without an emulator.

Supports game-partition XISO and common full-disc XGD1/XGD2/XGD3 layouts.
Never modifies the ISO or overwrites an existing destination.
"""
import argparse
import hashlib
import os
from pathlib import Path
import struct
import tempfile

SECTOR = 2048
MAGIC = b'MICROSOFT*XBOX*MEDIA'
PARTITION_OFFSETS = (0, 0x0FD90000, 0x02080000, 0x89D80000, 0x18300000)
MAX_ROOT_TABLE = 32 * 1024 * 1024
MAX_XEX = 128 * 1024 * 1024


def find_volume(image):
    """Return (partition_base, root_sector, root_size) for a validated XDVDFS volume."""
    image.seek(0, os.SEEK_END)
    length = image.tell()
    for base in PARTITION_OFFSETS:
        descriptor_offset = base + 32 * SECTOR
        if descriptor_offset + SECTOR > length:
            continue
        image.seek(descriptor_offset)
        descriptor = image.read(SECTOR)
        if descriptor[:20] != MAGIC:
            continue
        if descriptor[0x7EC:0x800] != MAGIC:
            raise ValueError(f'Corrupt XDVDFS volume descriptor at 0x{descriptor_offset:X}')
        root_sector, root_size = struct.unpack_from('<II', descriptor, 0x14)
        root_offset = base + root_sector * SECTOR
        if not (root_sector and 0 < root_size <= MAX_ROOT_TABLE and
                root_offset >= base and root_offset + root_size <= length):
            raise ValueError('Invalid XDVDFS root directory bounds')
        return base, root_sector, root_size
    raise ValueError('No XDVDFS volume descriptor found in supported Xbox disc layouts')


def find_root_xex(table):
    """Walk the on-disc binary tree; return (start_sector, byte_size)."""
    if not table:
        raise ValueError('Empty XDVDFS root directory')
    pending, visited = [0], set()
    while pending:
        offset = pending.pop()
        if offset in visited:
            raise ValueError('Cyclic XDVDFS directory tree')
        visited.add(offset)
        if offset + 14 > len(table):
            raise ValueError('Truncated XDVDFS directory entry')
        left, right, sector, size, attrs, name_len = struct.unpack_from('<HHIIBB', table, offset)
        if not name_len or offset + 14 + name_len > len(table):
            raise ValueError('Invalid XDVDFS filename length')
        name = table[offset + 14:offset + 14 + name_len].decode('latin-1')
        if name.casefold() == 'default.xex':
            if attrs & 0x10 or not size or size > MAX_XEX:
                raise ValueError('Invalid default.xex entry type or size')
            return sector, size
        for child in (right, left):
            if child:
                child_offset = child * 4
                if child_offset + 14 > len(table):
                    raise ValueError('XDVDFS directory child outside root table')
                pending.append(child_offset)
    raise ValueError('default.xex not present in XDVDFS root directory')


def extract(iso: Path, destination: Path, expected_sha256: str | None = None):
    """Read only the target file and publish atomically without replacing files."""
    if destination.exists():
        raise FileExistsError(f'Refusing to overwrite existing file: {destination}')
    if expected_sha256 is not None and (len(expected_sha256) != 64 or
                                        any(c not in '0123456789abcdefABCDEF' for c in expected_sha256)):
        raise ValueError('Expected SHA-256 must contain 64 hexadecimal digits')
    with iso.open('rb') as image:
        base, root_sector, root_size = find_volume(image)
        image.seek(base + root_sector * SECTOR)
        table = image.read(root_size)
        sector, size = find_root_xex(table)
        start = base + sector * SECTOR
        image.seek(0, os.SEEK_END)
        if start < base or start + size > image.tell():
            raise ValueError('default.xex points outside the ISO')
        image.seek(start)
        destination.parent.mkdir(parents=True, exist_ok=True)
        temp_name = None
        try:
            with tempfile.NamedTemporaryFile(dir=destination.parent, prefix='.dotm-xex-', delete=False) as out:
                temp_name = out.name
                digest = hashlib.sha256()
                remaining = size
                while remaining:
                    chunk = image.read(min(1024 * 1024, remaining))
                    if not chunk:
                        raise ValueError('Truncated default.xex data')
                    out.write(chunk)
                    digest.update(chunk)
                    remaining -= len(chunk)
            actual = digest.hexdigest()
            if expected_sha256 is not None and actual.lower() != expected_sha256.lower():
                raise ValueError(f'Original default.xex SHA-256 mismatch: {actual}')
            os.link(temp_name, destination)
            return size, actual, base
        finally:
            if temp_name is not None:
                os.unlink(temp_name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('iso', type=Path)
    parser.add_argument('output_xex', type=Path)
    parser.add_argument('--expected-sha256')
    args = parser.parse_args()
    try:
        size, digest, base = extract(args.iso, args.output_xex, args.expected_sha256)
    except (OSError, ValueError) as exc:
        parser.exit(1, f'XDVDFS extraction FAILED: {exc}\n')
    print(f'XDVDFS partition base: 0x{base:X}; default.xex: {size} bytes; SHA-256: {digest}')


if __name__ == '__main__':
    main()
