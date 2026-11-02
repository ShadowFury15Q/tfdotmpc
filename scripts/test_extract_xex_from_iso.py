#!/usr/bin/env python3
"""Synthetic XDVDFS fixtures only; no original game files."""
import hashlib
from pathlib import Path
import struct
from tempfile import TemporaryDirectory
import unittest

from extract_xex_from_iso import extract, find_root_xex, MAGIC, SECTOR


def make_iso(path, base=0, xex=b'XEX2\0\x01synthetic'):
    # Sparse full-disc layout; writes a few KB even when base is 266 MB.
    descriptor = bytearray(SECTOR)
    descriptor[:20] = MAGIC
    descriptor[0x7EC:] = MAGIC
    struct.pack_into('<II', descriptor, 0x14, 0x40, SECTOR)
    root = bytearray(SECTOR)
    # Root node is a.txt; default.xex is in its right subtree at byte 32.
    struct.pack_into('<HHIIBB', root, 0, 0, 8, 0x50, 3, 0x20, 5)
    root[14:19] = b'a.txt'
    struct.pack_into('<HHIIBB', root, 32, 0, 0, 0x60, len(xex), 0x20, 11)
    root[46:57] = b'default.xex'
    with path.open('wb') as out:
        out.seek(base + 32 * SECTOR)
        out.write(descriptor)
        out.seek(base + 0x40 * SECTOR)
        out.write(root)
        out.seek(base + 0x60 * SECTOR)
        out.write(xex)
    return xex


class XdvdfsXexTests(unittest.TestCase):
    def test_plain_xiso_root_tree(self):
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            xex = make_iso(root / 'disc.iso')
            size, digest, base = extract(root / 'disc.iso', root / 'default.xex', hashlib.sha256(xex).hexdigest())
            self.assertEqual((size, digest, base), (len(xex), hashlib.sha256(xex).hexdigest(), 0))
            self.assertEqual((root / 'default.xex').read_bytes(), xex)

    def test_xgd2_full_disc_partition(self):
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            xex = make_iso(root / 'disc.iso', 0x0FD90000)
            self.assertEqual(extract(root / 'disc.iso', root / 'default.xex')[2], 0x0FD90000)
            self.assertEqual((root / 'default.xex').read_bytes(), xex)

    def test_refuses_overwrite(self):
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_iso(root / 'disc.iso')
            (root / 'default.xex').write_bytes(b'original')
            with self.assertRaises(FileExistsError):
                extract(root / 'disc.iso', root / 'default.xex')
            self.assertEqual((root / 'default.xex').read_bytes(), b'original')

    def test_wrong_digest_does_not_publish(self):
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_iso(root / 'disc.iso')
            with self.assertRaisesRegex(ValueError, 'SHA-256 mismatch'):
                extract(root / 'disc.iso', root / 'default.xex', '0' * 64)
            self.assertFalse((root / 'default.xex').exists())

    def test_rejects_bad_descriptor(self):
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_iso(root / 'disc.iso')
            with (root / 'disc.iso').open('r+b') as out:
                out.seek(0x10000 + 0x7EC)
                out.write(b'BROKEN')
            with self.assertRaisesRegex(ValueError, 'Corrupt'):
                extract(root / 'disc.iso', root / 'default.xex')

    def test_rejects_directory_cycle(self):
        table = bytearray(SECTOR)
        struct.pack_into('<HHIIBB', table, 0, 0, 8, 0, 3, 0x20, 5)
        table[14:19] = b'a.txt'
        struct.pack_into('<HHIIBB', table, 32, 0, 8, 0, 3, 0x20, 5)
        table[46:51] = b'b.txt'
        with self.assertRaisesRegex(ValueError, 'Cyclic'):
            find_root_xex(table)

    def test_rejects_out_of_bounds_file(self):
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_iso(root / 'disc.iso')
            with (root / 'disc.iso').open('r+b') as out:
                out.seek(0x40 * SECTOR + 32 + 4)
                out.write(struct.pack('<I', 0xFFFFFFFF))
            with self.assertRaisesRegex(ValueError, 'outside the ISO'):
                extract(root / 'disc.iso', root / 'default.xex')


if __name__ == '__main__':
    unittest.main()
