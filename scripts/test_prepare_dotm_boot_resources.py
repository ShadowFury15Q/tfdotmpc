#!/usr/bin/env python3
"""Public synthetic coverage for private original DOTM boot-resource staging."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
from prepare_dotm_boot_resources import prepare


class PrepareOriginalBootResources(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.archive = self.root / "boot.zip"
        self.headers = self.root / "headers.json"
        self.output = self.root / "game"
        self.toc = b"3 0 0 ..\\TransGame\\CookedXenon\\Startup_int.xxx abcdef\r\n"
        self.entries = {
            "TransGame/Xbox360TOC.txt": self.toc,
            "TransGame/CookedXenon/Startup_int.xxx": b"\x9e\x2a\x83\xc1" + bytes(range(64)),
        }
        self.toc_digest = hashlib.sha256(self.toc).hexdigest()
        self.make_fixture()

    def make_fixture(self, override=None, extras=None):
        data = dict(self.entries if override is None else override)
        manifest = {
            "iso": "synthetic-only.xiso",
            "files": [
                {"path": name, "bytes": len(payload),
                 "sha256": hashlib.sha256(payload).hexdigest(),
                 "first_256_bytes_hex": payload[:256].hex()}
                for name, payload in self.entries.items()
            ],
        }
        self.headers.write_text(json.dumps(manifest), encoding="utf-8")
        with zipfile.ZipFile(self.archive, "w") as archive:
            for name, payload in data.items():
                archive.writestr(name, payload)
            for name, payload in (extras or {}).items():
                archive.writestr(name, payload)

    def test_exact_original_structure_and_digest(self):
        count, size = prepare(
            self.archive, self.headers, self.output, self.toc_digest)
        self.assertEqual(count, 2)
        self.assertEqual(size, sum(len(x) for x in self.entries.values()))
        for name, data in self.entries.items():
            self.assertEqual((self.output / name).read_bytes(), data)

    def test_refuses_overwrite(self):
        self.output.mkdir()
        (self.output / "keep.txt").write_text("preserve", encoding="utf-8")
        with self.assertRaises(FileExistsError):
            prepare(self.archive, self.headers, self.output, self.toc_digest)
        self.assertEqual((self.output / "keep.txt").read_text(), "preserve")

    def test_rejects_corrupted_zip_without_mounted_content(self):
        edited = dict(self.entries)
        edited["TransGame/Xbox360TOC.txt"] = b"corrupted"
        self.make_fixture(override=edited)
        with self.assertRaises(ValueError):
            prepare(self.archive, self.headers, self.output, self.toc_digest)
        self.assertFalse(self.output.exists())

    def test_rejects_unexpected_zip_path(self):
        self.make_fixture(extras={"../outside.txt": b"invalid"})
        with self.assertRaises(ValueError):
            prepare(self.archive, self.headers, self.output, self.toc_digest)
        self.assertFalse((self.root / "outside.txt").exists())

    def test_requires_known_original_toc(self):
        with self.assertRaises(ValueError):
            prepare(self.archive, self.headers, self.output)


if __name__ == "__main__":
    unittest.main()
