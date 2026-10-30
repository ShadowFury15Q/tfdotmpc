#!/usr/bin/env python3
"""No game content: synthetic private PPC input-discovery regression tests."""
import contextlib
import hashlib
import io
from pathlib import Path
import struct
import tempfile
import unittest
import zipfile
from unittest.mock import patch
from private_ppc_preflight import (EXPECTED_UNITS, inspect_archive, inspect_units, pe_entry, run)


class PrivatePreflightTests(unittest.TestCase):
    def test_empty_directory_is_blocked(self):
        with tempfile.TemporaryDirectory() as tmp, contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(run(Path(tmp)), 1)

    def test_missing_middle_unit_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            for name in ("ppc_recomp_shared.h", "ppc_context.h", "ppc_func_mapping.cpp"):
                (path / name).touch()
            for i in range(EXPECTED_UNITS):
                if i != 400:
                    (path / f"ppc_recomp.{i}.cpp").touch()
            count, missing, extra, headers = inspect_units(path)
            self.assertEqual(count, EXPECTED_UNITS - 1)
            self.assertEqual(missing, [400])
            self.assertFalse(extra or headers)
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(run(path), 1)
            (path / "ppc_recomp.400.cpp").touch()
            self.assertEqual(inspect_units(path), (EXPECTED_UNITS, [], [], []))

    def test_pe_machine_base_and_entry(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "game.pe"
            data = bytearray(512)
            data[:2] = b"MZ"
            struct.pack_into("<I", data, 0x3c, 0x80)
            data[0x80:0x84] = b"PE\0\0"
            struct.pack_into("<H", data, 0x84, 0x01F2)
            struct.pack_into("<H", data, 0x98, 0x010B)
            struct.pack_into("<I", data, 0x98 + 16, 0x0077F670)
            struct.pack_into("<I", data, 0x98 + 28, 0x82000000)
            path.write_bytes(data)
            self.assertEqual(pe_entry(path), (0x82000000, 0x8277F670))
            data[0x84] = 0x4c
            path.write_bytes(data)
            self.assertIsNone(pe_entry(path))


    def test_original_ppc_zip_layout_without_extraction(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "ppc.zip"
            with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
                archive.writestr("ppc/ppc_config.h", "#define PPC_IMAGE_BASE 0x82000000")
                archive.writestr("ppc/ppc_context.h", "struct PPCContext {};")
                archive.writestr("ppc/ppc_recomp_shared.h", "PPC_EXTERN_FUNC(_xstart);")
                archive.writestr("ppc/ppc_func_mapping.cpp",
                                 "PPCFuncMapping m[] = {{ 0x8277F670, _xstart }};")
                for index in range(EXPECTED_UNITS):
                    archive.writestr(f"ppc/ppc_recomp.{index}.cpp", "")
            self.assertEqual(inspect_archive(path), (EXPECTED_UNITS, [], [], []))
            with contextlib.redirect_stdout(io.StringIO()) as output:
                self.assertEqual(run(path), 1)
            self.assertIn("READY: 697 contiguous units", output.getvalue())
            self.assertIn("no decrypted Xbox PPC PE", output.getvalue())
            with contextlib.redirect_stdout(io.StringIO()) as output:
                self.assertEqual(run(Path(tmp)), 1)
            self.assertIn("PPC archive:", output.getvalue())

    def test_ready_ppc_and_pe_still_require_matching_original_xex(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            with zipfile.ZipFile(root / "ppc.zip", "w") as archive:
                for name in ("ppc_config.h", "ppc_context.h"):
                    archive.writestr(f"ppc/{name}", "")
                archive.writestr("ppc/ppc_recomp_shared.h", "PPC_EXTERN_FUNC(_xstart);")
                archive.writestr("ppc/ppc_func_mapping.cpp",
                                 "PPCFuncMapping m[] = {{ 0x8277F670, _xstart }};")
                for index in range(EXPECTED_UNITS):
                    archive.writestr(f"ppc/ppc_recomp.{index}.cpp", "")
            pe = bytearray(512)
            pe[:2] = b"MZ"
            struct.pack_into("<I", pe, 0x3c, 0x80)
            pe[0x80:0x84] = b"PE\\0\\0"
            struct.pack_into("<H", pe, 0x84, 0x01F2)
            struct.pack_into("<H", pe, 0x98, 0x010B)
            struct.pack_into("<I", pe, 0x98 + 16, 0x0077F670)
            struct.pack_into("<I", pe, 0x98 + 28, 0x82000000)
            (root / "private-game.pe").write_bytes(pe)
            with contextlib.redirect_stdout(io.StringIO()) as output:
                self.assertEqual(run(root), 1)
            self.assertIn("original default.xex", output.getvalue())
            original = root / "default.xex"
            original.write_bytes(b"synthetic-only-test")
            with contextlib.redirect_stdout(io.StringIO()) as output:
                self.assertEqual(run(root), 1)
            self.assertIn("MISMATCH", output.getvalue())
            expected_hash = hashlib.sha256(original.read_bytes()).hexdigest()
            with patch("private_ppc_preflight.EXPECTED_XEX_SHA256", expected_hash):
                with contextlib.redirect_stdout(io.StringIO()) as output:
                    self.assertEqual(run(root), 0)
            self.assertIn("--xex-header", output.getvalue())

    def test_zip_missing_unit_and_invalid_entry(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "ppc.zip"
            with zipfile.ZipFile(path, "w") as archive:
                archive.writestr("ppc/ppc_config.h", "")
                archive.writestr("ppc/ppc_context.h", "")
                archive.writestr("ppc/ppc_recomp_shared.h", "PPC_EXTERN_FUNC(_xstart);")
                archive.writestr("ppc/ppc_func_mapping.cpp", "not a mapping")
                for index in range(EXPECTED_UNITS):
                    if index != 400:
                        archive.writestr(f"ppc/ppc_recomp.{index}.cpp", "")
            count, missing, extra, headers = inspect_archive(path)
            self.assertEqual(count, EXPECTED_UNITS - 1)
            self.assertEqual(missing, [400])
            self.assertFalse(extra)
            self.assertIn("ppc_func_mapping.cpp (_xstart entry)", headers)

    def test_zip_duplicate_or_traversal_member_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "ppc.zip"
            with zipfile.ZipFile(path, "w") as archive:
                archive.writestr("../ppc/ppc_recomp.0.cpp", "")
            self.assertIsNone(inspect_archive(path))

if __name__ == "__main__":
    unittest.main()
