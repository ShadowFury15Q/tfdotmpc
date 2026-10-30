#!/usr/bin/env python3
"""Losslessly extract the exact original DOTM PowerPC PE from its own retail XEX2.

Supports the original DOTM executable revision using AES-128-CBC and XEX2
Basic compression. Never changes the input or uploads original game bytes.
Requires pip install cryptography.
"""
import argparse
import hashlib
from pathlib import Path
import struct
import sys

XEX_SHA256 = "6fd9ae9fb94ea8e9cc91a6a0cc62b04441411bdafe2479f0d380d4a3a9957ca6"
PE_SHA256 = "df8a064cf30e30a10d371679cb83cb349890f78c770e80e27778c465e17d6b0e"
PE_SIZE = 28377088
RETAIL_KEY = bytes.fromhex("20b185a59d28fdc340583fbb0896bf91")
ORIGINAL_ENTRY = 0x8277F670
ORIGINAL_BASE = 0x82000000
MAX_PE_SIZE = 128 * 1024 * 1024


def decode_basic_xex(source: bytes) -> bytes:
    """Decrypt and restore a real XEX2 AES/Basic image; refuse unknown formats."""
    try:
        from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    except ImportError as exc:
        raise ValueError("AES dependency missing: pip install cryptography") from exc
    if len(source) < 0x200 or source[:4] != b"XEX2":
        raise ValueError("Expected Xbox 360 XEX2 executable")
    be32 = lambda pos: struct.unpack_from(">I", source, pos)[0]
    header_size = be32(8)
    security_offset = be32(16)
    count = be32(20)
    if not (24 <= header_size <= len(source) and
            0 < count <= (header_size - 24) // 8):
        raise ValueError("Corrupt XEX2 header directory")
    headers = {}
    for index in range(count):
        key, value = struct.unpack_from(">II", source, 24 + 8 * index)
        if key in headers:
            raise ValueError("Duplicate XEX optional header")
        headers[key] = value
    if (headers.get(0x00010100) != ORIGINAL_ENTRY or
            headers.get(0x00010201) != ORIGINAL_BASE):
        raise ValueError("XEX entrypoint/base does not match original DOTM")
    info = headers.get(0x000003FF)
    if info is None or not (24 <= info and info + 8 <= header_size):
        raise ValueError("Missing XEX file-format information")
    info_bytes, encryption, compression = struct.unpack_from(">IHH", source, info)
    if encryption != 1 or compression != 1:
        raise ValueError("Expected retail AES + Basic compression")
    if info_bytes < 16 or info_bytes % 8 or info + info_bytes > header_size:
        raise ValueError("Invalid Basic compression table")
    pairs = [struct.unpack_from(">II", source, info + i)
             for i in range(8, info_bytes, 8)]
    image_size = sum(data + zeros for data, zeros in pairs)
    data_size = sum(data for data, _ in pairs)
    if (security_offset + 0x160 > header_size or
            not (0 < image_size <= MAX_PE_SIZE) or
            image_size != be32(security_offset + 4)):
        raise ValueError("XEX Basic image-size or security-header mismatch")
    if data_size != len(source) - header_size or data_size % 16:
        raise ValueError("XEX encrypted data length mismatch")
    key_offset = security_offset + 0x150
    encrypted_key = source[key_offset:key_offset + 16]
    session = Cipher(algorithms.AES(RETAIL_KEY), modes.ECB()).decryptor().update(
        encrypted_key)
    decryptor = Cipher(algorithms.AES(session), modes.CBC(bytes(16))).decryptor()
    decrypted = decryptor.update(source[header_size:]) + decryptor.finalize()
    output = bytearray(image_size)
    src = 0
    dest = 0
    for block_bytes, zeros in pairs:
        output[dest:dest + block_bytes] = decrypted[src:src + block_bytes]
        src += block_bytes
        dest += block_bytes + zeros
    return bytes(output)


def validate_original_pe(image: bytes) -> None:
    if len(image) != PE_SIZE or hashlib.sha256(image).hexdigest() != PE_SHA256:
        raise ValueError("Original DOTM PE length or SHA-256 mismatch")
    if image[:2] != b"MZ" or len(image) < 0x40:
        raise ValueError("Not a decrypted PE image")
    nt = struct.unpack_from("<I", image, 0x3C)[0]
    if nt + 64 > len(image) or image[nt:nt + 4] != b"PE\x00\x00":
        raise ValueError("Invalid PPC PE signature")
    machine = struct.unpack_from("<H", image, nt + 4)[0]
    entry = struct.unpack_from("<I", image, nt + 24 + 16)[0]
    base = struct.unpack_from("<I", image, nt + 24 + 28)[0]
    if machine != 0x01F2 or base != ORIGINAL_BASE or base + entry != ORIGINAL_ENTRY:
        raise ValueError("PPC machine/image-base/_xstart mismatch")


def extract(original_xex: Path, destination: Path) -> Path:
    original_xex = Path(original_xex)
    destination = Path(destination)
    source = original_xex.read_bytes()
    if hashlib.sha256(source).hexdigest() != XEX_SHA256:
        raise ValueError("Not the verified original DOTM default.xex")
    if destination.exists():
        validate_original_pe(destination.read_bytes())
        return destination
    image = decode_basic_xex(source)
    validate_original_pe(image)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open("xb") as stream:
        stream.write(image)
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original_xex", type=Path)
    parser.add_argument("-o", "--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        path = extract(args.original_xex, args.output)
        print(f"VERIFIED ORIGINAL DOTM PE: {path} ({path.stat().st_size} bytes)")
        return 0
    except (OSError, ValueError) as exc:
        print("DOTM XEX extraction failed:", exc, file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
