#!/usr/bin/env python3
"""No-game-assets public tests for the original shader container indexer."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

path = Path(__file__).with_name("index_original_xenos_shader_containers.py")
spec = importlib.util.spec_from_file_location("dotm_xenos_index", path)
import sys
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
spec.loader.exec_module(module)


def pack(*words):
    return struct.pack(">" + "I" * len(words), *words)


def make_vertex():
    # Actual Xenos shader-container layout with synthetic 96-bit VFETCH
    # instruction words. This does NOT impersonate original DOTM microcode.
    code = [0] * 15
    code[9:12] = [(1 << 19), (38 << 16), 7]
    code[12:15] = [(1 << 19), (57 << 16), 7 | (4 << 8)]
    instructions = pack(*code)
    desc = pack(0, len(instructions), 0, 0, 0, 0) + \
        pack(0, 2, 0, 3, 4 | (10 << 12))
    virtual_size = 36 + len(desc)
    head = pack(0x102A1101, virtual_size, len(instructions), 0,
                0, 0, 36, 0, 0)
    return head + desc + instructions


def make_pixel():
    code = pack(0, 0, 0)
    desc = pack(0, len(code), 0, 0, 0, 0, 0, 1)
    virtual_size = 36 + len(desc)
    return pack(0x102A1100, virtual_size, len(code), 0, 0, 0,
                36, 0, 0) + desc + code


class IndexerTest(unittest.TestCase):
    def test_original_reflection_without_game_assets(self):
        s = module.parse_original_container(make_vertex())
        self.assertEqual(s.stage, "vertex")
        self.assertEqual(s.layout, (
            "stream 0 28 POSITION 0 0 float4",
            "stream 0 28 COLOR 0 16 float3",
        ))
        self.assertEqual(len(s.hash), 16)
        self.assertEqual(module.parse_original_container(make_pixel()).stage,
                         "pixel")

    def test_private_cache_index_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            inputs = root / "input"
            inputs.mkdir()
            (inputs / "a.bin").write_bytes(make_vertex())
            (inputs / "b.bin").write_bytes(make_pixel())
            output = root / "cache"
            result = module.index_containers(inputs, output)
            self.assertEqual(result["vertex_shaders"], 1)
            self.assertEqual(result["pixel_shaders"], 1)
            key = module.parse_original_container(make_vertex()).hash
            self.assertEqual(
                (output / "layouts" / (key + ".layout")).read_text(),
                "stream 0 28 POSITION 0 0 float4\n"
                "stream 0 28 COLOR 0 16 float3\n")
            self.assertTrue((output / "index.json").exists())
            self.assertEqual(module.index_containers(inputs, output)
                             ["vertex_shaders"], 1)
            with self.assertRaises(ValueError):
                module.index_containers(inputs, inputs / "nested")

    def test_reject_unsupported_formats_and_truncation(self):
        data = bytearray(make_vertex())
        size = struct.unpack_from(">I", data, 4)[0]
        # Exact VFETCH element location in the synthetic physical shader code.
        struct.pack_into(">I", data, size + 9 * 4 + 4, 7 << 16)
        with self.assertRaises(module.UnsupportedContainer):
            module.parse_original_container(data)
        with self.assertRaises(module.UnsupportedContainer):
            module.parse_original_container(make_vertex()[:-3])
        with self.assertRaises(module.UnsupportedContainer):
            module.parse_original_container(b"invalid")

    def test_hash_uses_original_big_endian_byte_order(self):
        raw = bytes.fromhex("12345678AABBCCDD")
        value = 14695981039346656037
        for byte in raw:
            value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
        self.assertEqual(module.fnv64(raw), f"{value:016X}")


if __name__ == "__main__":
    unittest.main()
