#!/usr/bin/env python3
"""Verify private multipart DOTM ISO archive fragments without extracting game data.

Use on an offline/private checkout after downloading all GitHub Release assets.
No original files are modified or published.
"""
import argparse
import hashlib
from pathlib import Path
import re

PART_RE = re.compile(r"^(?P<base>.+\.(?:7z|zip|iso))\.(?P<number>[0-9]{3})$", re.I)
HASH_RE = re.compile(r"^([0-9a-fA-F]{64})  (.+)$")
MAX_ASSET_BYTES = 2 * 1024**3


def find_parts(folder: Path) -> list[Path]:
    if not folder.is_dir():
        raise ValueError(f"Not a directory: {folder}")
    groups: dict[str, list[tuple[int, Path]]] = {}
    for path in folder.iterdir():
        match = PART_RE.fullmatch(path.name)
        if match and path.is_file():
            groups.setdefault(match['base'].casefold(), []).append((int(match['number']), path))
    if len(groups) != 1:
        raise ValueError(f"Expected one complete multipart archive, found {len(groups)}")
    indexed = sorted(next(iter(groups.values())))
    if [n for n, _ in indexed] != list(range(1, len(indexed) + 1)):
        raise ValueError("Missing, repeated or out-of-order ISO archive part numbers")
    parts = [p for _, p in indexed]
    sizes = [p.stat().st_size for p in parts]
    if any(not (0 < size < MAX_ASSET_BYTES) for size in sizes):
        raise ValueError("Empty or oversized ISO archive part")
    if any(size != sizes[0] for size in sizes[:-1]) or sizes[-1] > sizes[0]:
        raise ValueError("Multipart archive has inconsistent part sizes")
    return parts


def read_manifest(path: Path) -> dict[str, str]:
    hashes = {}
    for line_number, line in enumerate(path.read_text(encoding='utf-8-sig').splitlines(), 1):
        match = HASH_RE.fullmatch(line)
        if not match:
            raise ValueError(f"Invalid SHA-256 manifest line {line_number}")
        digest, name = match.groups()
        if (name in hashes or name in ('', '.', '..') or '/' in name or '\\' in name
                or ':' in name):
            raise ValueError(f"Unsafe or duplicate manifest filename: {name}")
        hashes[name] = digest.lower()
    if not hashes:
        raise ValueError("Empty SHA-256 manifest")
    return hashes


def verify(folder: Path, manifest: Path | None = None) -> tuple[int, int]:
    parts = find_parts(folder)
    expected = read_manifest(manifest) if manifest is not None else None
    if expected is not None and set(expected) != {p.name for p in parts}:
        raise ValueError("SHA-256 manifest and ISO part filenames differ")
    total = 0
    for part in parts:
        digest = hashlib.sha256()
        with part.open('rb') as source:
            for block in iter(lambda: source.read(1024 * 1024), b''):
                digest.update(block)
        actual = digest.hexdigest()
        if expected is not None and actual != expected[part.name]:
            raise ValueError(f"SHA-256 mismatch: {part.name}")
        total += part.stat().st_size
        print(f"{actual}  {part.name}")
    return len(parts), total


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('folder', type=Path, help='Directory with .7z.001 ... parts')
    parser.add_argument('--manifest', type=Path, help='SHA256SUMS from private release')
    args = parser.parse_args()
    try:
        count, size = verify(args.folder, args.manifest)
    except (OSError, ValueError) as exc:
        parser.exit(1, f"ISO fragment verification FAILED: {exc}\n")
    print(f"ISO fragment verification PASS: {count} parts, {size} bytes")


if __name__ == '__main__':
    main()
