#!/usr/bin/env python3
"""Insert DOTM-specific PowerPC translation handlers into upstream XenonRecomp."""
from pathlib import Path
import shutil
import sys

def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("Usage: python apply_xenon_patch.py <XenonRecomp directory>")
    upstream = Path(sys.argv[1])
    source = upstream / "XenonRecomp" / "recompiler.cpp"
    header = upstream / "XenonRecomp" / "dotm_instruction_cases.inc"
    root = Path(__file__).resolve().parents[1]
    patch = root / "patches" / "dotm_instruction_cases.inc"
    extra = root / "patches" / "dotm_extra_cases.inc"
    original = source.read_text(encoding="utf-8")
    anchor = "    default:\n        return false;\n    }\n"
    insert = '#include "dotm_instruction_cases.inc"\n#include "dotm_extra_cases.inc"\n'
    if original.count(anchor) != 1:
        raise SystemExit(f"ERROR: Upstream switch changed; found {original.count(anchor)} anchors, expected 1")
    patched = original.replace(anchor, insert + anchor, 1)
    if patched.count(insert) != 1:
        raise SystemExit("ERROR: Patch insertion failed")
    source.write_text(patched, encoding="utf-8")
    shutil.copyfile(patch, header)
    shutil.copyfile(extra, upstream / "XenonRecomp" / "dotm_extra_cases.inc")
    print(f"Patched {source} with {len(patch.read_text().splitlines())} lines")
    print(f"Cases defined: {patch.read_text().count('case PPC_INST_') + extra.read_text().count('case PPC_INST_')}")

if __name__ == "__main__":
    main()
