#!/usr/bin/env python3
"""Verify and stage the original private DOTM_BOOT_RESOURCES.zip for native boot.

The independent DOTM_BOOT_HEADERS.json supplied with the original files is
required. No game data is written to the public repository or altered.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import stat
import tempfile
import zipfile


def prepare(archive_path: Path, headers_path: Path, output: Path) -> tuple[int, int]:
    manifest = json.loads(headers_path.read_text(encoding="utf-8"))
    records = manifest["files"]
    expected = {}
    for record in records:
        name = record["path"]
        path = PurePosixPath(name)
        if (not name or path.is_absolute() or
                any(part in ("", ".", "..") or ":" in part or "\\" in part
                    for part in path.parts) or
                name != path.as_posix() or name.casefold() in expected):
            raise ValueError("Unsafe or duplicate DOTM boot resource path")
        if (record["bytes"] < 0 or len(record["sha256"]) != 64):
            raise ValueError("Invalid original resource size or digest")
        expected[name.casefold()] = record
    toc = expected.get("transgame/xbox360toc.txt")
    if not toc or toc["sha256"] != "f55f581ed407f4be8210a88b130ce3431baccc36a893a4a31a41c752d304e8dd":
        raise ValueError("Expected verified original Xbox360TOC.txt is missing")
    if output.exists() or output.is_symlink():
        raise FileExistsError("Refusing to replace existing game root: " + str(output))
    output.parent.mkdir(parents=True, exist_ok=True)
    total = 0
    with zipfile.ZipFile(archive_path) as archive:
        found = {}
        for member in archive.infolist():
            if member.is_dir():
                continue
            name = member.filename
            if name == "DOTM_BOOT_RESOURCES_HASHES.json":
                continue
            if (name.casefold() in found or name.casefold() not in expected or
                    stat.S_IFMT(member.external_attr >> 16) == stat.S_IFLNK):
                raise ValueError("Unexpected, duplicate or linked ZIP member: " + name)
            found[name.casefold()] = member
        if set(found) != set(expected):
            raise ValueError("Original DOTM boot ZIP does not match independent manifest")
        temporary = Path(tempfile.mkdtemp(prefix=".dotm-original-", dir=output.parent))
        try:
            for name, record in expected.items():
                member = found[name]
                if member.file_size != record["bytes"]:
                    raise ValueError("Original ZIP resource has wrong size: " + record["path"])
                destination = temporary.joinpath(*PurePosixPath(record["path"]).parts)
                destination.parent.mkdir(parents=True, exist_ok=True)
                digest = hashlib.sha256()
                size = 0
                prefix = bytearray()
                with archive.open(member) as source, destination.open("xb") as target:
                    while chunk := source.read(1024 * 1024):
                        size += len(chunk)
                        digest.update(chunk)
                        if len(prefix) < 256:
                            prefix.extend(chunk[:256 - len(prefix)])
                        target.write(chunk)
                if (size != record["bytes"] or
                        digest.hexdigest().lower() != record["sha256"].lower() or
                        ("first_256_bytes_hex" in record and
                         prefix.hex() != record["first_256_bytes_hex"].lower())):
                    raise ValueError("Original DOTM resource hash mismatch: " + record["path"])
                total += size
            if output.exists() or output.is_symlink():
                raise FileExistsError("Game root appeared during verification: " + str(output))
            temporary.rename(output)
        finally:
            if temporary.exists():
                shutil.rmtree(temporary)
    return len(expected), total


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("boot_zip", type=Path)
    parser.add_argument("headers_json", type=Path)
    parser.add_argument("game_root", type=Path)
    args = parser.parse_args()
    try:
        count, size = prepare(args.boot_zip, args.headers_json, args.game_root)
    except (OSError, KeyError, TypeError, ValueError, zipfile.BadZipFile) as error:
        parser.exit(1, f"Original DOTM boot-resource verification failed: {error}\n")
    print(f"Verified {count} original DOTM boot files ({size} bytes)")
    print(f"DOTM_GAME_ROOT={args.game_root.resolve()}")


if __name__ == "__main__":
    main()
