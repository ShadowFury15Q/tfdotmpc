#!/usr/bin/env python3
"""Regression tests for the original XEX2 retail AES/Basic extraction logic.

The fixture is generated locally from artificial bytes. No game data included.
"""
import hashlib
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parent))
from extract_dotm_xex_basic import (
    ORIGINAL_BASE, ORIGINAL_ENTRY, RETAIL_KEY,
    decode_basic_xex, extract, validate_original_pe,
)


def synthetic_encrypted_xex():
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    result = bytearray(0x4000)
    result[0:4] = b"XEX2"
    struct.pack_into(">I", result, 8, 0x4000)
    struct.pack_into(">I", result, 16, 0x90)
    struct.pack_into(">I", result, 20, 3)
    struct.pack_into(">II", result, 24, 0x000003FF, 0x2AD0)
    struct.pack_into(">II", result, 32, 0x00010100, ORIGINAL_ENTRY)
    struct.pack_into(">II", result, 40, 0x00010201, ORIGINAL_BASE)
    struct.pack_into(">I", result, 0x90 + 4, 1024)
    struct.pack_into(">IHH", result, 0x2AD0, 24, 1, 1)
    struct.pack_into(">IIII", result, 0x2AD8, 256, 0, 256, 512)
    session = bytes(range(16))
    encrypted_session = Cipher(
        algorithms.AES(RETAIL_KEY), modes.ECB()).encryptor().update(session)
    result[0x90 + 0x150:0x90 + 0x160] = encrypted_session
    original = b"MZ" + bytes(254) + b"TEST" + bytes(252) + bytes(512)
    flat = original[:512]
    encryptor = Cipher(algorithms.AES(session), modes.CBC(bytes(16))).encryptor()
    result.extend(encryptor.update(flat) + encryptor.finalize())
    return bytes(result), original


class BasicXexTests(unittest.TestCase):
    def test_aes_basic_roundtrip(self):
        xex, original = synthetic_encrypted_xex()
        self.assertEqual(decode_basic_xex(xex), original)

    def test_truncated_or_modified_block_table_rejected(self):
        xex, original = synthetic_encrypted_xex()
        bad = bytearray(xex)
        struct.pack_into(">I", bad, 0x2AD8, 257)
        with self.assertRaises(ValueError):
            decode_basic_xex(bytes(bad))
        with self.assertRaises(ValueError):
            decode_basic_xex(xex[:0x4000])
        self.assertEqual(decode_basic_xex(xex)[:4], original[:4])

    def test_unsupported_compression_and_entry_rejected(self):
        xex, _ = synthetic_encrypted_xex()
        bad = bytearray(xex)
        struct.pack_into(">H", bad, 0x2AD0 + 6, 2)
        with self.assertRaises(ValueError):
            decode_basic_xex(bytes(bad))
        bad = bytearray(xex)
        struct.pack_into(">I", bad, 24 + 8 + 4, ORIGINAL_ENTRY + 4)
        with self.assertRaises(ValueError):
            decode_basic_xex(bytes(bad))

    def test_only_verified_original_xex_and_pe_accepted(self):
        from tempfile import TemporaryDirectory
        xex, original = synthetic_encrypted_xex()
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "default.xex").write_bytes(xex)
            with self.assertRaises(ValueError):
                extract(root / "default.xex", root / "original_title.pe")
            with self.assertRaises(ValueError):
                validate_original_pe(original)
            self.assertFalse((root / "original_title.pe").exists())


if __name__ == "__main__":
    unittest.main()
