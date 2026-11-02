#!/usr/bin/env python3
"""Synthetic-only tests: no proprietary ISO or game data."""
import hashlib
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from verify_iso_parts import find_parts, verify


class MultipartIsoTests(unittest.TestCase):
    def make_parts(self, root):
        parts = []
        for n, data in enumerate((b'abcd', b'efgh', b'ij'), 1):
            path = root / f'private.7z.{n:03}'
            path.write_bytes(data)
            parts.append(path)
        return parts

    def manifest(self, root, parts):
        path = root / 'dotm_iso_parts_SHA256SUMS.txt'
        path.write_text(''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n'
                                for p in parts), encoding='utf-8')
        return path

    def test_verified_parts_and_manifest(self):
        with TemporaryDirectory() as d:
            root = Path(d)
            parts = self.make_parts(root)
            manifest = self.manifest(root, parts)
            self.assertEqual(verify(root, manifest), (3, 10))
            self.assertEqual([p.name for p in find_parts(root)], [p.name for p in parts])

    def test_missing_part_fails(self):
        with TemporaryDirectory() as d:
            root = Path(d)
            parts = self.make_parts(root)
            parts[1].unlink()
            with self.assertRaisesRegex(ValueError, 'part numbers'):
                find_parts(root)

    def test_tampered_part_fails(self):
        with TemporaryDirectory() as d:
            root = Path(d)
            parts = self.make_parts(root)
            manifest = self.manifest(root, parts)
            parts[0].write_bytes(b'zzzz')
            with self.assertRaisesRegex(ValueError, 'SHA-256 mismatch'):
                verify(root, manifest)

    def test_extra_manifest_entry_fails(self):
        with TemporaryDirectory() as d:
            root = Path(d)
            parts = self.make_parts(root)
            manifest = self.manifest(root, parts)
            with manifest.open('a') as out:
                out.write('0' * 64 + '  unexpected.7z.004\n')
            with self.assertRaisesRegex(ValueError, 'filenames differ'):
                verify(root, manifest)

    def test_inconsistent_sizes_fail(self):
        with TemporaryDirectory() as d:
            root = Path(d)
            parts = self.make_parts(root)
            parts[1].write_bytes(b'xx')
            with self.assertRaisesRegex(ValueError, 'inconsistent'):
                find_parts(root)

    def test_multiple_archives_fail(self):
        with TemporaryDirectory() as d:
            root = Path(d)
            self.make_parts(root)
            (root / 'other.zip.001').write_bytes(b'other')
            with self.assertRaisesRegex(ValueError, 'one complete'):
                find_parts(root)

    def test_empty_and_duplicate_manifest_fail(self):
        with TemporaryDirectory() as d:
            root = Path(d)
            parts = self.make_parts(root)
            parts[2].write_bytes(b'')
            with self.assertRaisesRegex(ValueError, 'Empty'):
                find_parts(root)
            parts[2].write_bytes(b'ij')
            manifest = self.manifest(root, parts)
            with manifest.open('a') as out:
                out.write(f'{"0"*64}  {parts[0].name}\n')
            with self.assertRaisesRegex(ValueError, 'duplicate'):
                verify(root, manifest)


if __name__ == '__main__':
    unittest.main()
