#!/usr/bin/env python3
"""No game content: synthetic private PPC input-discovery regression tests."""
import contextlib
import io
from pathlib import Path
import struct
import tempfile
import unittest
from private_ppc_preflight import (EXPECTED_UNITS, inspect_units, pe_entry, run)


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
            self.assertEqual(count, 695)
            self.assertEqual(missing, [400])
            self.assertFalse(extra or headers)
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(run(path), 1)
            (path / "ppc_recomp.400.cpp").touch()
            self.assertEqual(inspect_units(path), (696, [], [], []))

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


if __name__ == "__main__":
    unittest.main()
