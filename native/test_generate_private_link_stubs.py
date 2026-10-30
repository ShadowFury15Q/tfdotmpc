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
            self.assertIn("throw std::runtime_error(", source)
            self.assertNotIn("synthetic mapping", source)

    def test_xenonrecomp_alias_backed_game_functions_are_not_stubs(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "ppc_recomp_shared.h").write_text(
                "PPC_EXTERN_FUNC(__imp__XamInputGetState);\\n"
                "PPC_EXTERN_FUNC(_xstart);\\n"
                "PPC_EXTERN_FUNC(sub_82500000);\\n", encoding="utf-8")
            (root / "ppc_func_mapping.cpp").write_text(
                "{0x8277F670, _xstart},\\n"
                "{0x82500000, sub_82500000},\\n", encoding="utf-8")
            (root / "ppc_recomp.0.cpp").write_text(
                '__attribute__((alias("__imp___xstart"))) PPC_WEAK_FUNC(_xstart);\\n'
                "PPC_FUNC_IMPL(__imp___xstart) {}\\n"
                '__attribute__((alias("__imp__sub_82500000"))) PPC_WEAK_FUNC(sub_82500000);\\n'
                "PPC_FUNC_IMPL(__imp__sub_82500000) {}\\n", encoding="utf-8")
            source, units, unresolved = generate(root, 1)
            self.assertEqual((units, unresolved), (1, 0))
            self.assertNotIn("PPC_FUNC(_xstart)", source)
            self.assertNotIn("PPC_FUNC(sub_82500000)", source)

    def test_xenonrecomp_alias_requires_real_target_body(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "ppc_recomp_shared.h").write_text(
                "PPC_EXTERN_FUNC(__imp__XamInputGetState);\\n", encoding="utf-8")
            (root / "ppc_func_mapping.cpp").write_text(
                "{0x8277F670, _xstart},\\n", encoding="utf-8")
            (root / "ppc_recomp.0.cpp").write_text(
                '__attribute__((alias("__imp___xstart"))) PPC_WEAK_FUNC(_xstart);\\n',
                encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Missing XenonRecomp alias target"):
                generate(root, 1)

    def test_disabled_xenonrecomp_alias_cannot_fake_entry(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "ppc_recomp_shared.h").write_text(
                "PPC_EXTERN_FUNC(__imp__XamInputGetState);\\n", encoding="utf-8")
            (root / "ppc_func_mapping.cpp").write_text(
                "{0x8277F670, _xstart},\\n", encoding="utf-8")
            (root / "ppc_recomp.0.cpp").write_text(
                '#if 0\\n__attribute__((alias("__imp___xstart"))) PPC_WEAK_FUNC(_xstart);\\n#endif\\n'
                "PPC_FUNC_IMPL(__imp___xstart) {}\\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "_xstart mapping or implementation"):
                generate(root, 1)

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

    def test_prototype_only_entry_cannot_satisfy_xstart_mapping(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "ppc_recomp_shared.h").write_text(
                "PPC_EXTERN_FUNC(__imp__XamInputGetState);\n", encoding="utf-8")
            (root / "ppc_func_mapping.cpp").write_text(
                "{0x8277F670, _xstart},\n", encoding="utf-8")
            (root / "ppc_recomp.0.cpp").write_text(
                "PPC_FUNC_IMPL(_xstart);\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "_xstart mapping or implementation"):
                generate(root, 1)

    def test_prototype_only_game_function_cannot_satisfy_mapping(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "ppc_recomp_shared.h").write_text(
                "PPC_EXTERN_FUNC(__imp__XamInputGetState);\n", encoding="utf-8")
            (root / "ppc_func_mapping.cpp").write_text(
                "{0x8277F670, _xstart},\n"
                "{0x82500000, sub_82500000},\n", encoding="utf-8")
            (root / "ppc_recomp.0.cpp").write_text(
                "PPC_FUNC_IMPL(_xstart) {}\n"
                "PPC_FUNC(sub_82500000);\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Missing recompiled game functions"):
                generate(root, 1)

    def test_duplicate_game_function_bodies_are_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "ppc_recomp_shared.h").write_text(
                "PPC_EXTERN_FUNC(__imp__XamInputGetState);\n", encoding="utf-8")
            (root / "ppc_func_mapping.cpp").write_text(
                "{0x8277F670, _xstart},\n", encoding="utf-8")
            for index in range(2):
                (root / f"ppc_recomp.{index}.cpp").write_text(
                    "PPC_FUNC_IMPL(_xstart) {}\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Duplicate recompiled PPC function definitions"):
                generate(root, 2)

    def test_platform_import_prototype_still_requires_stub(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "ppc_recomp_shared.h").write_text(
                "PPC_EXTERN_FUNC(__imp__XamInputGetState);\n"
                "PPC_EXTERN_FUNC(__imp__KeWaitForSingleObject);\n",
                encoding="utf-8")
            (root / "ppc_func_mapping.cpp").write_text(
                "{0x8277F670, _xstart},\n", encoding="utf-8")
            (root / "ppc_recomp.0.cpp").write_text(
                "PPC_FUNC_IMPL(_xstart) {}\n"
                "PPC_FUNC(__imp__KeWaitForSingleObject);\n", encoding="utf-8")
            source, count, unresolved = generate(root, 1)
            self.assertEqual((count, unresolved), (1, 1))
            self.assertIn("PPC_FUNC(__imp__KeWaitForSingleObject)", source)

    def test_undeclared_mapped_platform_import_is_rejected(self):
        for name in ("__imp__KeWaitForSingleObject",
                     "__imp__XamInputGetCapabilities"):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                (root / "ppc_recomp_shared.h").write_text(
                    "PPC_EXTERN_FUNC(__imp__XamInputGetState);\n",
                    encoding="utf-8")
                (root / "ppc_func_mapping.cpp").write_text(
                    "{0x8277F670, _xstart},\n"
                    f"{{0x82500000, {name}}},\n", encoding="utf-8")
                (root / "ppc_recomp.0.cpp").write_text(
                    "PPC_FUNC_IMPL(_xstart) {}\n", encoding="utf-8")
                with self.assertRaisesRegex(
                    ValueError, "Mapped PPC imports lack declarations or implementations"
                ):
                    generate(root, 1)

    def test_mapped_import_defined_in_private_code_needs_no_declaration(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "ppc_recomp_shared.h").write_text(
                "PPC_EXTERN_FUNC(__imp__XamInputGetState);\n",
                encoding="utf-8")
            (root / "ppc_func_mapping.cpp").write_text(
                "{0x8277F670, _xstart},\n"
                "{0x82500000, __imp__SyntheticPlatform},\n",
                encoding="utf-8")
            (root / "ppc_recomp.0.cpp").write_text(
                "PPC_FUNC_IMPL(_xstart) {}\n"
                "PPC_FUNC(__imp__SyntheticPlatform) {}\n",
                encoding="utf-8")
            source, units, unresolved = generate(root, 1)
            self.assertEqual((units, unresolved), (1, 0))
            self.assertNotIn("PPC_FUNC(__imp__SyntheticPlatform)", source)

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

    def test_disabled_preprocessor_definition_cannot_satisfy_mapping(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "ppc_recomp_shared.h").write_text(
                "PPC_EXTERN_FUNC(__imp__XamInputGetState);\n", encoding="utf-8")
            (root / "ppc_func_mapping.cpp").write_text(
                "{0x8277F670, _xstart},\n", encoding="utf-8")
            (root / "ppc_recomp.0.cpp").write_text(
                "#if 0\nPPC_FUNC_IMPL(_xstart) {}\n#endif\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "_xstart mapping or implementation"):
                generate(root, 1)

    def test_disabled_preprocessor_mapping_is_ignored(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "ppc_recomp_shared.h").write_text(
                "PPC_EXTERN_FUNC(__imp__XamInputGetState);\n", encoding="utf-8")
            (root / "ppc_func_mapping.cpp").write_text(
                "#if 0\n{0x8277F670, _xstart},\n#endif\n", encoding="utf-8")
            (root / "ppc_recomp.0.cpp").write_text(
                "PPC_FUNC_IMPL(_xstart) {}\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "No original PPC function mappings"):
                generate(root, 1)

    def test_preprocessor_else_keeps_real_definition(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "ppc_recomp_shared.h").write_text(
                "PPC_EXTERN_FUNC(__imp__XamInputGetState);\n", encoding="utf-8")
            (root / "ppc_func_mapping.cpp").write_text(
                "{0x8277F670, _xstart},\n", encoding="utf-8")
            (root / "ppc_recomp.0.cpp").write_text(
                "#if 0\nPPC_FUNC_IMPL(_xstart) {}\n"
                "#else\nPPC_FUNC_IMPL(_xstart) {}\n#endif\n", encoding="utf-8")
            _, count, unresolved = generate(root, 1)
            self.assertEqual((count, unresolved), (1, 0))

    def test_nested_disabled_preprocessor_definition_with_crlf(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "ppc_recomp_shared.h").write_text(
                "PPC_EXTERN_FUNC(__imp__XamInputGetState);\n", encoding="utf-8")
            (root / "ppc_func_mapping.cpp").write_text(
                "{0x8277F670, _xstart},\n", encoding="utf-8")
            (root / "ppc_recomp.0.cpp").write_text(
                "#if 0\r\n#if 1\r\nPPC_FUNC_IMPL(_xstart) {}\r\n"
                "#endif\r\n#endif\r\n", encoding="utf-8", newline="")
            with self.assertRaisesRegex(ValueError, "_xstart mapping or implementation"):
                generate(root, 1)

    def test_macro_definition_is_not_a_function_body(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "ppc_recomp_shared.h").write_text(
                "PPC_EXTERN_FUNC(__imp__XamInputGetState);\n", encoding="utf-8")
            (root / "ppc_func_mapping.cpp").write_text(
                "{0x8277F670, _xstart},\n", encoding="utf-8")
            (root / "ppc_recomp.0.cpp").write_text(
                "#define FAKE_ENTRY PPC_FUNC_IMPL(_xstart) {}\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "_xstart mapping or implementation"):
                generate(root, 1)

    def test_line_spliced_if_zero_cannot_fake_game_definition(self):
        for newline in ("\n", "\r\n"):
            with self.subTest(newline=repr(newline)), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                (root / "ppc_recomp_shared.h").write_text(
                    "PPC_EXTERN_FUNC(__imp__XamInputGetState);\n", encoding="utf-8")
                (root / "ppc_func_mapping.cpp").write_text(
                    "{0x8277F670, _xstart},\n", encoding="utf-8")
                (root / "ppc_recomp.0.cpp").write_text(
                    "#if \\" + newline + "0" + newline +
                    "PPC_FUNC_IMPL(_xstart) {}" + newline +
                    "#endif" + newline, encoding="utf-8", newline="")
                with self.assertRaisesRegex(ValueError, "_xstart mapping or implementation"):
                    generate(root, 1)

    def test_line_spliced_elif_one_keeps_active_definition(self):
        for newline in ("\n", "\r\n"):
            with self.subTest(newline=repr(newline)), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                (root / "ppc_recomp_shared.h").write_text(
                    "PPC_EXTERN_FUNC(__imp__XamInputGetState);\n", encoding="utf-8")
                (root / "ppc_func_mapping.cpp").write_text(
                    "{0x8277F670, _xstart},\n", encoding="utf-8")
                (root / "ppc_recomp.0.cpp").write_text(
                    "#if 0" + newline +
                    "PPC_FUNC_IMPL(_xstart) {}" + newline +
                    "#elif \\" + newline + "1" + newline +
                    "PPC_FUNC_IMPL(_xstart) {}" + newline +
                    "#endif" + newline, encoding="utf-8", newline="")
                _, units, unresolved = generate(root, 1)
                self.assertEqual((units, unresolved), (1, 0))

    def test_line_spliced_define_cannot_fake_game_definition(self):
        for newline in ("\n", "\r\n"):
            with self.subTest(newline=repr(newline)), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                (root / "ppc_recomp_shared.h").write_text(
                    "PPC_EXTERN_FUNC(__imp__XamInputGetState);\n", encoding="utf-8")
                (root / "ppc_func_mapping.cpp").write_text(
                    "{0x8277F670, _xstart},\n", encoding="utf-8")
                (root / "ppc_recomp.0.cpp").write_text(
                    "#define FAKE \\" + newline +
                    "PPC_FUNC_IMPL(_xstart) {}" + newline, encoding="utf-8", newline="")
                with self.assertRaisesRegex(ValueError, "_xstart mapping or implementation"):
                    generate(root, 1)

    def test_spliced_function_macro_identifier_is_detected(self):
        for newline in ("\n", "\r\n"):
            with self.subTest(newline=repr(newline)), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                (root / "ppc_recomp_shared.h").write_text(
                    "PPC_EXTERN_FUNC(__imp__XamInputGetState);\n", encoding="utf-8")
                (root / "ppc_func_mapping.cpp").write_text(
                    "{0x8277F670, _xstart},\n", encoding="utf-8")
                (root / "ppc_recomp.0.cpp").write_text(
                    "PPC_FUNC_" + "\\" + newline + "IMPL(_xstart) {}" + newline,
                    encoding="utf-8", newline="")
                _, units, unresolved = generate(root, 1)
                self.assertEqual((units, unresolved), (1, 0))

    def test_spliced_mapping_address_is_detected(self):
        for newline in ("\n", "\r\n"):
            with self.subTest(newline=repr(newline)), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                (root / "ppc_recomp_shared.h").write_text(
                    "PPC_EXTERN_FUNC(__imp__XamInputGetState);\n", encoding="utf-8")
                (root / "ppc_func_mapping.cpp").write_text(
                    "{0x8277F" + "\\" + newline + "670, _xstart}," + newline,
                    encoding="utf-8", newline="")
                (root / "ppc_recomp.0.cpp").write_text(
                    "PPC_FUNC_IMPL(_xstart) {}\n", encoding="utf-8")
                _, units, unresolved = generate(root, 1)
                self.assertEqual((units, unresolved), (1, 0))

    def test_spliced_header_declaration_is_detected(self):
        for newline in ("\n", "\r\n"):
            with self.subTest(newline=repr(newline)), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                (root / "ppc_recomp_shared.h").write_text(
                    "PPC_EX" + "\\" + newline +
                    "TERN_FUNC(__imp__XamInputGetState);" + newline,
                    encoding="utf-8", newline="")
                (root / "ppc_func_mapping.cpp").write_text(
                    "{0x8277F670, _xstart},\n", encoding="utf-8")
                (root / "ppc_recomp.0.cpp").write_text(
                    "PPC_FUNC_IMPL(_xstart) {}\n", encoding="utf-8")
                _, units, unresolved = generate(root, 1)
                self.assertEqual((units, unresolved), (1, 0))

    def test_spliced_cpp_comment_delimiter_cannot_fake_definition(self):
        for newline in ("\n", "\r\n"):
            with self.subTest(newline=repr(newline)), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                (root / "ppc_recomp_shared.h").write_text(
                    "PPC_EXTERN_FUNC(__imp__XamInputGetState);\n", encoding="utf-8")
                (root / "ppc_func_mapping.cpp").write_text(
                    "{0x8277F670, _xstart},\n", encoding="utf-8")
                (root / "ppc_recomp.0.cpp").write_text(
                    "/" + "\\" + newline +
                    "/ PPC_FUNC_IMPL(_xstart) {}" + newline,
                    encoding="utf-8", newline="")
                with self.assertRaisesRegex(ValueError, "_xstart mapping or implementation"):
                    generate(root, 1)

    def test_negated_integer_guards_on_ppc_definitions(self):
        for condition, active in (
            ("!1", False), ("!!0", False), ("!(0x1UL)", False),
            ("!0", True), ("!(0x0UL)", True), ("(!(!1))", True),
        ):
            for newline in ("\n", "\r\n"):
                with self.subTest(condition=condition, newline=repr(newline)), tempfile.TemporaryDirectory() as tmp:
                    root = Path(tmp)
                    (root / "ppc_recomp_shared.h").write_text(
                        "PPC_EXTERN_FUNC(__imp__XamInputGetState);\n", encoding="utf-8")
                    (root / "ppc_func_mapping.cpp").write_text(
                        "{0x8277F670, _xstart},\n", encoding="utf-8")
                    (root / "ppc_recomp.0.cpp").write_text(
                        "#if " + condition + newline +
                        "PPC_FUNC_IMPL(_xstart) {}" + newline +
                        "#endif" + newline, encoding="utf-8", newline="")
                    if active:
                        _, units, unresolved = generate(root, 1)
                        self.assertEqual((units, unresolved), (1, 0))
                    else:
                        with self.assertRaisesRegex(ValueError, "_xstart mapping or implementation"):
                            generate(root, 1)

    def test_negated_false_guards_cannot_fabricate_imports_or_mappings(self):
        for condition in ("!1", "!!0", "!(1U)"):
            for newline in ("\n", "\r\n"):
                with self.subTest(condition=condition, newline=repr(newline)), tempfile.TemporaryDirectory() as tmp:
                    root = Path(tmp)
                    (root / "ppc_recomp_shared.h").write_text(
                        "#if " + condition + newline +
                        "PPC_EXTERN_FUNC(__imp__KeWaitForSingleObject);" + newline +
                        "#endif" + newline +
                        "PPC_EXTERN_FUNC(__imp__XamInputGetState);" + newline,
                        encoding="utf-8", newline="")
                    (root / "ppc_func_mapping.cpp").write_text(
                        "#if " + condition + newline +
                        "{0x82500000, sub_82500000}," + newline +
                        "#endif" + newline +
                        "{0x8277F670, _xstart}," + newline,
                        encoding="utf-8", newline="")
                    (root / "ppc_recomp.0.cpp").write_text(
                        "PPC_FUNC_IMPL(_xstart) {}\n", encoding="utf-8")
                    source, units, unresolved = generate(root, 1)
                    self.assertEqual((units, unresolved), (1, 0))
                    self.assertNotIn("KeWaitForSingleObject", source)

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


    def test_nonconditional_preprocessor_directives_do_not_create_ppc_symbols(self):
        for newline in ("\n", "\r\n"):
            with self.subTest(newline=repr(newline)), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                (root / "ppc_recomp_shared.h").write_text(
                    "PPC_EXTERN_FUNC(__imp__XamInputGetState);" + newline +
                    "#pragma vendor PPC_EXTERN_FUNC(__imp__KeWaitForSingleObject);" + newline,
                    encoding="utf-8", newline="")
                (root / "ppc_func_mapping.cpp").write_text(
                    "#pragma vendor {0x82500000, sub_82500000}," + newline +
                    "{0x8277F670, _xstart}," + newline,
                    encoding="utf-8", newline="")
                (root / "ppc_recomp.0.cpp").write_text(
                    "#pragma vendor " + chr(92) + newline +
                    "PPC_FUNC_IMPL(sub_82500000) {}" + newline +
                    "PPC_FUNC_IMPL(_xstart) {}" + newline,
                    encoding="utf-8", newline="")
                source, units, unresolved = generate(root, 1)
                self.assertEqual((units, unresolved), (1, 0))
                self.assertNotIn("KeWaitForSingleObject", source)

    def test_pragma_body_cannot_satisfy_xstart_mapping(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "ppc_recomp_shared.h").write_text(
                "PPC_EXTERN_FUNC(__imp__XamInputGetState);\n", encoding="utf-8")
            (root / "ppc_func_mapping.cpp").write_text(
                "{0x8277F670, _xstart},\n", encoding="utf-8")
            (root / "ppc_recomp.0.cpp").write_text(
                "#pragma vendor PPC_FUNC_IMPL(_xstart) {}\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "_xstart mapping or implementation"):
                generate(root, 1)


    def test_integer_suffix_and_nondecimal_if_conditions(self):
        # An inactive #if 0U/0x0 must not fabricate a PPC implementation.
        for condition, active in (
            ("0U", False), ("0x0UL", False), ("000", False),
            ("((0b0))", False), ("1U", True), ("0x1LL", True),
        ):
            for newline in ("\n", "\r\n"):
                with self.subTest(condition=condition, newline=repr(newline)), tempfile.TemporaryDirectory() as tmp:
                    root = Path(tmp)
                    (root / "ppc_recomp_shared.h").write_text(
                        "PPC_EXTERN_FUNC(__imp__XamInputGetState);\n", encoding="utf-8")
                    (root / "ppc_func_mapping.cpp").write_text(
                        "{0x8277F670, _xstart},\n", encoding="utf-8")
                    (root / "ppc_recomp.0.cpp").write_text(
                        "#if " + condition + newline +
                        "PPC_FUNC_IMPL(_xstart) {}" + newline +
                        "#endif" + newline, encoding="utf-8", newline="")
                    if active:
                        _, units, unresolved = generate(root, 1)
                        self.assertEqual((units, unresolved), (1, 0))
                    else:
                        with self.assertRaisesRegex(ValueError, "_xstart mapping or implementation"):
                            generate(root, 1)

    def test_zero_integer_guards_cannot_fabricate_mapping_or_import(self):
        for condition in ("0U", "(0x0UL)", "000"):
            for newline in ("\n", "\r\n"):
                with self.subTest(condition=condition, newline=repr(newline)), tempfile.TemporaryDirectory() as tmp:
                    root = Path(tmp)
                    (root / "ppc_recomp_shared.h").write_text(
                        "#if " + condition + newline +
                        "PPC_EXTERN_FUNC(__imp__KeWaitForSingleObject);" + newline +
                        "#endif" + newline +
                        "PPC_EXTERN_FUNC(__imp__XamInputGetState);" + newline,
                        encoding="utf-8", newline="")
                    (root / "ppc_func_mapping.cpp").write_text(
                        "#if " + condition + newline +
                        "{0x82500000, sub_82500000}," + newline +
                        "#endif" + newline +
                        "{0x8277F670, _xstart}," + newline,
                        encoding="utf-8", newline="")
                    (root / "ppc_recomp.0.cpp").write_text(
                        "PPC_FUNC_IMPL(_xstart) {}\n", encoding="utf-8")
                    source, units, unresolved = generate(root, 1)
                    self.assertEqual((units, unresolved), (1, 0))
                    self.assertNotIn("KeWaitForSingleObject", source)



if __name__ == "__main__":
    unittest.main()
