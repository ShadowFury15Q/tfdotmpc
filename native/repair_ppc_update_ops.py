#!/usr/bin/env python3
"""Repair omitted PPC update-form operations in private XenonRecomp C++.

Patched copies remain in the private build directory; game code is never
modified in-place or copied to the public repository.
"""
import argparse
from collections import Counter
from pathlib import Path
import re
import shutil

D_FORMS = {"lhzu": ("load", 16), "sthu": ("store", 16)}
X_FORMS = {
    "lwzux": ("load", 32), "lhzux": ("load", 16),
    "lbzux": ("load", 8), "ldux": ("load", 64),
    "stbux": ("store", 8), "sthux": ("store", 16),
}
OPCODES = D_FORMS | X_FORMS
MISSING = re.compile(
    r"(?m)^(?P<indent>[ \t]*)// (?P<op>" + "|".join(OPCODES) +
    r") (?P<args>[^\r\n]+)\r?\n(?=[ \t]*(?:// |loc_|\}))"
)
D_ARGS = re.compile(r"r(\d+),(-?\d+)\(r(\d+)\)")
X_ARGS = re.compile(r"r(\d+),r(\d+),r(\d+)")

def repair(text: str, counts: Counter) -> str:
    def replace(match: re.Match) -> str:
        op, args = match["op"], match["args"]
        d_form = op in D_FORMS
        parsed = (D_ARGS if d_form else X_ARGS).fullmatch(args)
        if parsed is None:
            raise ValueError(f"Unrecognized {op} operands: {args}")
        dest = int(parsed[1])
        base = int(parsed[3] if d_form else parsed[2])
        if not (0 <= dest <= 31 and 1 <= base <= 31):
            raise ValueError(f"Invalid PPC update register in {op} {args}")
        if op in ("lhzu", "lwzux", "lhzux", "lbzux", "ldux") and dest == base:
            raise ValueError(f"Undefined PPC update-load destination in {op} {args}")
        if d_form:
            offset = int(parsed[2])
            if not (-32768 <= offset <= 32767):
                raise ValueError(f"Out-of-range D-form displacement: {args}")
            address = f"ctx.r{base}.u32 + ({offset})"
        else:
            index = int(parsed[3])
            if not (0 <= index <= 31):
                raise ValueError(f"Invalid PPC index register: {args}")
            address = f"ctx.r{base}.u32 + ctx.r{index}.u32"
        kind, width = OPCODES[op]
        indent = match["indent"]
        lines = [
            f"{indent}// {op} {args}",
            f"{indent}{{",
            f"{indent}    const uint32_t ppc_update_ea = {address};",
        ]
        if kind == "load":
            lines.append(
                f"{indent}    ctx.r{dest}.u64 = PPC_LOAD_U{width}(ppc_update_ea);"
            )
        else:
            lines.append(
                f"{indent}    PPC_STORE_U{width}(ppc_update_ea, ctx.r{dest}.u{width});"
            )
        lines.extend([
            f"{indent}    ctx.r{base}.u32 = ppc_update_ea;",
            f"{indent}}}",
        ])
        counts[op] += 1
        return "\n".join(lines) + "\n"
    fixed = MISSING.sub(replace, text)
    if MISSING.search(fixed):
        raise ValueError("PPC update-form instruction remains untranslated")
    return fixed

def stage(source: Path, dest: Path) -> Counter:
    source, dest = source.resolve(), dest.resolve()
    if source == dest or source in dest.parents or dest in source.parents:
        raise ValueError("Private source and staging directories must be distinct")
    names = [f"ppc_recomp.{i}.cpp" for i in range(697)]
    names += ["ppc_recomp_shared.h", "ppc_context.h", "ppc_config.h",
              "ppc_func_mapping.cpp"]
    for name in names:
        if not (source / name).is_file():
            raise FileNotFoundError(f"Missing private XenonRecomp file: {name}")
    dest.mkdir(parents=True, exist_ok=True)
    counts = Counter()
    for name in names:
        src, out = source / name, dest / name
        if name.startswith("ppc_recomp.") and name.endswith(".cpp"):
            original = src.read_text(encoding="utf-8")
            fixed = repair(original, counts)
            if fixed != original:
                out.write_text(fixed, encoding="utf-8")
            else:
                shutil.copyfile(src, out)
        else:
            shutil.copyfile(src, out)
    return counts

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--dest", required=True, type=Path)
    args = parser.parse_args()
    counts = stage(args.source, args.dest)
    print("Private PPC update-form repairs:", sum(counts.values()),
          ", ".join(f"{name}={counts[name]}" for name in sorted(counts)))

if __name__ == "__main__":
    main()
