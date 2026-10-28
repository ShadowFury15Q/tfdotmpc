#!/usr/bin/env python3
"""Tests for the private PPC link stub generator; synthetic fixtures only."""
import tempfile
import unittest
from pathlib import Path
from generate_private_link_stubs import generate


class PrivateLinkStubsTests(unittest.TestCase):
    def test_missing_private_files_are_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(ValueError, "Missing private"):
                generate(Path(tmp), 1)

    def test_unresolved_symbols_only_and_no_game_source_leak(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "ppc_recomp_shared.h").write_text(
                "PPC_EXTERN_FUNC(__imp__XamInputGetState);\n"
                "PPC_EXTERN_FUNC(__imp__XamInputGetCapabilities);\n"
                "PPC_EXTERN_FUNC(__imp__KeWaitForSingleObject);\n"
                "PPC_EXTERN_FUNC(__savegprlr_14);\n"
                "PPC_EXTERN_FUNC(_xstart);\n", encoding="utf-8")
            (root / "ppc_func_mapping.cpp").write_text("// synthetic mapping\n", encoding="utf-8")
            (root / "ppc_recomp.0.cpp").write_text(
                "PPC_FUNC_IMPL(_xstart) { /* synthetic */ }\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Incomplete private"):
                generate(root, 2)
            source, units, unresolved = generate(root, 1)
            self.assertEqual((units, unresolved), (1, 2))
            self.assertIn("PPC_FUNC(__imp__KeWaitForSingleObject)", source)
            self.assertIn("PPC_FUNC(__savegprlr_14)", source)
            self.assertNotIn("PPC_FUNC(_xstart)", source)
            self.assertNotIn("PPC_FUNC(__imp__XamInputGetState)", source)
            self.assertNotIn("PPC_FUNC(__imp__XamInputGetCapabilities)", source)
            self.assertIn("std::abort()", source)
            self.assertNotIn("synthetic mapping", source)

    def test_missing_expected_xam_import_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "ppc_recomp_shared.h").write_text(
                "PPC_EXTERN_FUNC(__imp__KeWaitForSingleObject);", encoding="utf-8")
            (root / "ppc_func_mapping.cpp").write_text("", encoding="utf-8")
            (root / "ppc_recomp.0.cpp").write_text("", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "lacks expected"):
                generate(root, 1)


if __name__ == "__main__":
    unittest.main()
