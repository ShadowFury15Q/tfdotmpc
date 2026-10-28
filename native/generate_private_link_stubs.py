#!/usr/bin/env python3
"""Generate FAIL-FAST unresolved PPC imports for a PRIVATE XenonRecomp link probe.

This tool never copies original game code to the public repository. The generated
source is written to the local CMake build directory and is not game runtime.
"""
import argparse
from pathlib import Path
import re

DECL = re.compile(r"\bPPC_EXTERN_FUNC\s*\(\s*([A-Za-z_]\w*)\s*\)\s*;")
DEF = re.compile(r"\bPPC_FUNC(?:_IMPL)?\s*\(\s*([A-Za-z_]\w*)\s*\)")
NATIVE = {"__imp__XamInputGetState", "__imp__XamInputGetCapabilities"}


def generate(directory: Path, minimum_units: int = 600) -> tuple[str, int, int]:
    header = directory / "ppc_recomp_shared.h"
    mapping = directory / "ppc_func_mapping.cpp"
    units = sorted(directory.glob("ppc_recomp.*.cpp"))
    if not header.is_file() or not mapping.is_file():
        raise ValueError("Missing private ppc_recomp_shared.h or ppc_func_mapping.cpp")
    if len(units) < minimum_units:
        raise ValueError(f"Incomplete private recompilation: {len(units)} translation units; expected >= {minimum_units}")
    declarations = set(DECL.findall(header.read_text(encoding="utf-8")))
    if not declarations:
        raise ValueError("No PPC_EXTERN_FUNC declarations found in private header")
    definitions = set()
    for path in [mapping, *units]:
        definitions.update(DEF.findall(path.read_text(encoding="utf-8")))
    if "__imp__XamInputGetState" not in declarations:
        raise ValueError("Private game header lacks expected XamInputGetState import")
    unresolved = sorted(declarations - definitions - NATIVE)
    lines = [
        '#include "ppc_recomp_shared.h"',
        '#include <cstdio>',
        '#include <cstdlib>',
        '// PRIVATE LINK PROBE ONLY: unresolved PPC imports deliberately abort.',
        '// These are not implemented Xbox kernel, XAM or register-save semantics.',
    ]
    for name in unresolved:
        lines.extend([
            f"PPC_FUNC({name}) {{",
            f'    std::fputs("Unimplemented PPC external: {name}\\n", stderr);',
            "    std::abort();",
            "}",
        ])
    return "\n".join(lines) + "\n", len(units), len(unresolved)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("private_ppc_dir", type=Path)
    parser.add_argument("output_cpp", type=Path)
    parser.add_argument("--minimum-units", type=int, default=600)
    args = parser.parse_args()
    if args.minimum_units < 1:
        parser.error("--minimum-units must be positive")
    try:
        content, count, unresolved = generate(args.private_ppc_dir, args.minimum_units)
    except ValueError as exc:
        parser.error(str(exc))
    args.output_cpp.parent.mkdir(parents=True, exist_ok=True)
    args.output_cpp.write_text(content, encoding="utf-8")
    print(f"Private link probe: {count} recompiled units; {unresolved} fail-fast unresolved PPC externals")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
