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
                "PPC_EXTERN_FUNC(__imp__NtAllocateVirtualMemory);\n"
                "PPC_EXTERN_FUNC(__imp__NtFreeVirtualMemory);\n"
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
            self.assertNotIn("PPC_FUNC(__imp__NtAllocateVirtualMemory)", source)
            self.assertNotIn("PPC_FUNC(__imp__NtFreeVirtualMemory)", source)
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

    def test_out_of_range_or_unaligned_mapping_is_rejected(self):
        # These entries would be rejected by the runtime dispatch table;
        # catch them before compiling hundreds of private PPC units.
        for address in ("0x824FFFFC", "0x8382235C", "0x82500002"):
            with self.subTest(address=address), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                (root / "ppc_recomp_shared.h").write_text(
                    "PPC_EXTERN_FUNC(__imp__XamInputGetState);\n"
                    "PPC_EXTERN_FUNC(_xstart);\n", encoding="utf-8")
                (root / "ppc_func_mapping.cpp").write_text(
                    "{0x8277F670, _xstart},\n"
                    f"{{{address}, _xstart}},\n", encoding="utf-8")
                (root / "ppc_recomp.0.cpp").write_text(
                    "PPC_FUNC_IMPL(_xstart) {}\n", encoding="utf-8")
                with self.assertRaisesRegex(ValueError, "Invalid original PPC mapping address"):
                    generate(root, 1)

    def test_commented_out_game_definition_cannot_satisfy_mapping(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "ppc_recomp_shared.h").write_text(
                "PPC_EXTERN_FUNC(__imp__XamInputGetState);\n", encoding="utf-8")
            (root / "ppc_func_mapping.cpp").write_text(
                "{0x8277F670, _xstart},\n", encoding="utf-8")
            (root / "ppc_recomp.0.cpp").write_text(
                "/* PPC_FUNC_IMPL(_xstart) {} */\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "_xstart mapping or implementation"):
                generate(root, 1)

    def test_commented_out_mapping_is_not_accepted(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "ppc_recomp_shared.h").write_text(
                "PPC_EXTERN_FUNC(__imp__XamInputGetState);\n", encoding="utf-8")
            (root / "ppc_func_mapping.cpp").write_text(
                "// {0x8277F670, _xstart},\n", encoding="utf-8")
            (root / "ppc_recomp.0.cpp").write_text(
                "PPC_FUNC_IMPL(_xstart) {}\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "No original PPC function mappings"):
                generate(root, 1)

    def test_string_literal_cannot_create_fake_import(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "ppc_recomp_shared.h").write_text(
                'PPC_EXTERN_FUNC(__imp__XamInputGetState);\n'
                'const char* sample = "PPC_EXTERN_FUNC(__imp__KeWaitForSingleObject);";\n',
                encoding="utf-8")
            (root / "ppc_func_mapping.cpp").write_text(
                "{0x8277F670, _xstart},\n", encoding="utf-8")
            (root / "ppc_recomp.0.cpp").write_text(
                "PPC_FUNC_IMPL(_xstart) {}\n", encoding="utf-8")
            source, count, unresolved = generate(root, 1)
            self.assertEqual((count, unresolved), (1, 0))
            self.assertNotIn("PPC_FUNC(__imp__KeWaitForSingleObject)", source)

    def test_raw_string_mapping_does_not_create_invalid_address(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "ppc_recomp_shared.h").write_text(
                "PPC_EXTERN_FUNC(__imp__XamInputGetState);\n", encoding="utf-8")
            (root / "ppc_func_mapping.cpp").write_text(
                '{0x8277F670, _xstart},\n'
                'const char* sample = R"TAG({0x824FFFFC, _xstart})TAG";\n',
                encoding="utf-8")
            (root / "ppc_recomp.0.cpp").write_text(
                "PPC_FUNC_IMPL(_xstart) {}\n", encoding="utf-8")
            _, count, unresolved = generate(root, 1)
            self.assertEqual((count, unresolved), (1, 0))

    def test_line_spliced_comment_cannot_fake_game_definition(self):
        for newline in ("\n", "\r\n"):
            with self.subTest(newline=repr(newline)), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                (root / "ppc_recomp_shared.h").write_text(
                    "PPC_EXTERN_FUNC(__imp__XamInputGetState);\n", encoding="utf-8", newline="")
                (root / "ppc_func_mapping.cpp").write_text(
                    "{0x8277F670, _xstart},\n", encoding="utf-8", newline="")
                (root / "ppc_recomp.0.cpp").write_text(
                    "// commented out \\" + newline +
                    "PPC_FUNC_IMPL(_xstart) {}" + newline, encoding="utf-8", newline="")
                with self.assertRaisesRegex(ValueError, "_xstart mapping or implementation"):
                    generate(root, 1)

    def test_line_spliced_comment_cannot_fake_mapping(self):
        for newline in ("\n", "\r\n"):
            with self.subTest(newline=repr(newline)), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                (root / "ppc_recomp_shared.h").write_text(
                    "PPC_EXTERN_FUNC(__imp__XamInputGetState);\n", encoding="utf-8", newline="")
                (root / "ppc_func_mapping.cpp").write_text(
                    "// commented out \\" + newline +
                    "{0x8277F670, _xstart}," + newline, encoding="utf-8", newline="")
                (root / "ppc_recomp.0.cpp").write_text(
                    "PPC_FUNC_IMPL(_xstart) {}\n", encoding="utf-8", newline="")
                with self.assertRaisesRegex(ValueError, "No original PPC function mappings"):
                    generate(root, 1)

    def test_line_spliced_comment_cannot_create_import(self):
        for newline in ("\n", "\r\n"):
            with self.subTest(newline=repr(newline)), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                (root / "ppc_recomp_shared.h").write_text(
                    "PPC_EXTERN_FUNC(__imp__XamInputGetState);" + newline +
                    "// commented out \\" + newline +
                    "PPC_EXTERN_FUNC(__imp__KeWaitForSingleObject);" + newline,
                    encoding="utf-8", newline="")
                (root / "ppc_func_mapping.cpp").write_text(
                    "{0x8277F670, _xstart},\n", encoding="utf-8", newline="")
                (root / "ppc_recomp.0.cpp").write_text(
                    "PPC_FUNC_IMPL(_xstart) {}\n", encoding="utf-8", newline="")
                source, count, unresolved = generate(root, 1)
                self.assertEqual((count, unresolved), (1, 0))
                self.assertNotIn("PPC_FUNC(__imp__KeWaitForSingleObject)", source)

    def test_default_requires_all_696_private_units(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "ppc_recomp_shared.h").write_text(
                "PPC_EXTERN_FUNC(__imp__XamInputGetState);\n",
                encoding="utf-8")
            (root / "ppc_func_mapping.cpp").write_text(
                "{0x8277F670, _xstart},\n", encoding="utf-8")
            (root / "ppc_recomp.0.cpp").write_text(
                "PPC_FUNC_IMPL(_xstart) {}\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "expected >= 696"):
                generate(root)

    def test_missing_ppc_unit_index_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "ppc_recomp_shared.h").write_text(
                "PPC_EXTERN_FUNC(__imp__XamInputGetState);\n",
                encoding="utf-8")
            (root / "ppc_func_mapping.cpp").write_text(
                "{0x8277F670, _xstart},\n", encoding="utf-8")
            (root / "ppc_recomp.0.cpp").write_text(
                "PPC_FUNC_IMPL(_xstart) {}\n", encoding="utf-8")
            (root / "ppc_recomp.2.cpp").write_text(
                "PPC_FUNC_IMPL(sub_82500000) {}\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "indices must be contiguous"):
                generate(root, 2)

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
