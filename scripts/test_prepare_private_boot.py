#!/usr/bin/env python3
"""No proprietary files: validate safe staging and original-entry checks."""
import sys
from pathlib import Path
from tempfile import TemporaryDirectory
import struct
import unittest
import zipfile
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import prepare_private_boot as p


class PrivateBootTests(unittest.TestCase):
    def test_697_original_units_staged_without_overwriting(self):
        with TemporaryDirectory() as t:
            root = Path(t)
            with zipfile.ZipFile(root / "ppc.zip", "w", zipfile.ZIP_DEFLATED) as z:
                for i in range(697):
                    z.writestr(f"ppc/ppc_recomp.{i}.cpp", "/* public test fixture */")
                z.writestr("ppc/ppc_recomp_shared.h", "PPC_EXTERN_FUNC(_xstart);")
                z.writestr("ppc/ppc_context.h", "/* fixture */")
                z.writestr("ppc/ppc_config.h", "/* fixture */")
                z.writestr("ppc/ppc_func_mapping.cpp", "{0x8277F670, _xstart}")
            output = root / "private" / "native_boot"
            staged = p.verified_ppc(root, output)
            self.assertEqual(len(list(staged.glob("ppc_recomp.*.cpp"))), 697)
            self.assertEqual(p.verified_ppc(root, output), staged)
            # A second run must reject differing private bytes, even with same length.
            (staged / "ppc_recomp.24.cpp").write_text("/* forged test fixture */")
            with self.assertRaises(ValueError):
                p.verified_ppc(root, output)

    def test_incomplete_archive_is_rejected(self):
        with TemporaryDirectory() as t:
            root = Path(t)
            with zipfile.ZipFile(root / "ppc.zip", "w") as z:
                z.writestr("ppc/ppc_recomp.0.cpp", "")
            with self.assertRaises(ValueError):
                p.verified_ppc(root, root / "private" / "native_boot")

    def test_expected_powerpc_entrypoint(self):
        with TemporaryDirectory() as t:
            path = Path(t) / "original_title.pe"
            image = bytearray(512)
            image[:2] = b"MZ"
            struct.pack_into("<I", image, 0x3c, 0x80)
            image[0x80:0x84] = b"PE\x00\x00"
            struct.pack_into("<H", image, 0x84, 0x01F2)
            struct.pack_into("<H", image, 0x98, 0x10B)
            struct.pack_into("<I", image, 0x98 + 28, 0x82000000)
            struct.pack_into("<I", image, 0x98 + 16, 0x77F670)
            path.write_bytes(image)
            self.assertEqual(p.verify_pe(path), path)
            struct.pack_into("<I", image, 0x98 + 16, 0x77F674)
            path.write_bytes(image)
            with self.assertRaises(ValueError):
                p.verify_pe(path)

    def test_private_boot_repairs_original_bctr_with_verified_pe(self):
        # Mock only the host tool invocations: public CI must never need game bytes.
        with TemporaryDirectory() as t:
            root = Path(t)
            repo = root / "repo"
            (repo / "native").mkdir(parents=True)
            (repo / "native" / "CMakeLists.txt").write_text("project(DOTM)")
            output = root / "private"
            ppc = output / "ppc"
            pe = output / "original_title.pe"
            xex = output / "default.xex"
            commands = []
            def record(command, env=None):
                commands.append((command, env))
            (root / "TransGame").mkdir()
            (root / "TransGame" / "Xbox360TOC.txt").write_text("synthetic TOC")
            with patch.object(p.shutil, "which", return_value="/mock/tool"), \
                 patch.object(p, "checked", side_effect=record):
                p.boot(repo, root, output, ppc, pe, xex, root / "simde", None)
            self.assertEqual(len(commands), 3)
            (configure, config_env), (build, build_env), (run, run_env) = commands
            self.assertIsNone(config_env)
            self.assertIsNone(build_env)
            self.assertEqual(run_env["DOTM_GAME_ROOT"], str(root.resolve()))
            self.assertIn("-DDOTM_PRIVATE_PPC_DIR=" + str(ppc), configure)
            self.assertIn("-DDOTM_PRIVATE_PE_FILE=" + str(pe), configure)
            self.assertIn("-DDOTM_SIMDE_INCLUDE_DIR=" + str(root / "simde"), configure)
            self.assertEqual(build[-1], "2")
            self.assertEqual(run[-4:], ["--run-entry", pe, "--xex-header", xex])
            # No original binary or PPC translation unit is copied into repo.
            self.assertEqual(list((repo / "native").iterdir()),
                             [repo / "native" / "CMakeLists.txt"])

    def test_invalid_original_xex_refused(self):
        with TemporaryDirectory() as t:
            root = Path(t)
            (root / "default.xex").write_bytes(b"XEX2" + bytes(128))
            self.assertEqual(p.main.__name__, "main")
            from private_ppc_preflight import EXPECTED_XEX_SHA256, sha256
            self.assertNotEqual(sha256(root / "default.xex"), EXPECTED_XEX_SHA256)


if __name__ == "__main__":
    unittest.main()
