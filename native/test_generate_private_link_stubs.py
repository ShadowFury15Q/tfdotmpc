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
            (root / "ppc_func_mapping.cpp").write_text(
                "{0x8277F670, _xstart},\n"
                "{0x82500000, __imp__KeWaitForSingleObject},\n",
                encoding="utf-8")
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

    def test_missing_recompiled_game_function_is_not_stubbed(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "ppc_recomp_shared.h").write_text(
                "PPC_EXTERN_FUNC(__imp__XamInputGetState);\n"
                "PPC_EXTERN_FUNC(_xstart);\n"
                "PPC_EXTERN_FUNC(sub_82500000);\n", encoding="utf-8")
            (root / "ppc_func_mapping.cpp").write_text(
                "{0x8277F670, _xstart},\n{0x82500000, sub_82500000},\n",
                encoding="utf-8")
            (root / "ppc_recomp.0.cpp").write_text(
                "PPC_FUNC_IMPL(_xstart) {}\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Missing recompiled game functions"):
                generate(root, 1)

    def test_duplicate_guest_addresses_are_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "ppc_recomp_shared.h").write_text(
                "PPC_EXTERN_FUNC(__imp__XamInputGetState);\n", encoding="utf-8")
            (root / "ppc_func_mapping.cpp").write_text(
                "{0x8277F670, _xstart},\n{0x8277F670, _xstart},\n",
                encoding="utf-8")
            (root / "ppc_recomp.0.cpp").write_text(
                "PPC_FUNC_IMPL(_xstart) {}\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Duplicate guest addresses"):
                generate(root, 1)

    def test_missing_original_entry_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "ppc_recomp_shared.h").write_text(
                "PPC_EXTERN_FUNC(__imp__XamInputGetState);\n", encoding="utf-8")
            (root / "ppc_func_mapping.cpp").write_text(
                "{0x82500000, sub_82500000},\n", encoding="utf-8")
            (root / "ppc_recomp.0.cpp").write_text(
                "PPC_FUNC_IMPL(sub_82500000) {}\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "_xstart mapping"):
                generate(root, 1)

    def test_unmapped_nonplatform_symbol_is_not_stubbed(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "ppc_recomp_shared.h").write_text(
                "PPC_EXTERN_FUNC(__imp__XamInputGetState);\n"
                "PPC_EXTERN_FUNC(sub_82500004);\n", encoding="utf-8")
            (root / "ppc_func_mapping.cpp").write_text(
                "{0x8277F670, _xstart},\n", encoding="utf-8")
            (root / "ppc_recomp.0.cpp").write_text(
                "PPC_FUNC_IMPL(_xstart) {}\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Unimplemented non-platform PPC symbols"):
                generate(root, 1)

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
