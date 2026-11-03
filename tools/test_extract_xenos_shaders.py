#!/usr/bin/env python3
"""Integration tests for the real upstream XenosRecomp invocation."""
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

SCRIPT = pathlib.Path(__file__).with_name("extract_xenos_shaders.py")


class XenosPipelineTests(unittest.TestCase):
    def test_successful_upstream_cache(self):
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            source = root / "assets"
            source.mkdir()
            output = root / "out"
            header = root / "shader_common.h"
            header.write_text("// test fixture", encoding="utf-8")
            runner = root / "fake_recomp.py"
            runner.write_text(
                "#!/usr/bin/env python3\n"
                "import pathlib, sys\n"
                "pathlib.Path(sys.argv[2]).write_text('// cache')\n",
                encoding="utf-8")
            # On Windows, a .py fixture cannot execute directly. The real
            # translator is a native executable; use a launcher on POSIX.
            if sys.platform == "win32":
                self.skipTest("POSIX fixture launcher")
            runner.chmod(0o755)
            result = subprocess.run(
                [sys.executable, str(SCRIPT), str(source), str(output),
                 "--xenosrecomp", str(runner), "--header", str(header)],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            manifest = json.loads((output / "xenosrecomp_result.json").read_text())
            self.assertTrue(manifest["output_exists"])
            self.assertTrue((output / "dotm_xenos_shader_cache.cpp").is_file())

    def test_output_cannot_be_inside_assets(self):
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            source = root / "assets"
            source.mkdir()
            exe = root / "recomp"
            exe.write_text("", encoding="utf-8")
            header = root / "shader_common.h"
            header.write_text("", encoding="utf-8")
            result = subprocess.run(
                [sys.executable, str(SCRIPT), str(source), str(source / "cache"),
                 "--xenosrecomp", str(exe), "--header", str(header)],
                capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("outside the input tree", result.stderr)


if __name__ == "__main__":
    unittest.main()
