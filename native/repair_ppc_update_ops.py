#!/usr/bin/env python3
"""Repair omitted PPC update-form, EQV and conditional CTR branches in private XenonRecomp C++.

Patched copies remain in the private build directory; game code is never
modified in-place or copied to the public repository.
"""
import argparse
from collections import Counter
from pathlib import Path
import re
import shutil

D_FORMS = {
    "lhzu": ("load", 16), "sthu": ("store", 16),
    "lhau": ("load_signed", 16),
    "lfsu": ("float_load", 32), "stfsu": ("float_store", 32),
    "lfdu": ("float_load", 64), "stfdu": ("float_store", 64),
}
X_FORMS = {
    "lwzux": ("load", 32), "lhzux": ("load", 16),
    "lbzux": ("load", 8), "ldux": ("load", 64),
    "stbux": ("store", 8), "sthux": ("store", 16),
    "lfsux": ("float_load", 32), "stfsux": ("float_store", 32),
    "lfdux": ("float_load", 64),
}
OPCODES = D_FORMS | X_FORMS
MISSING = re.compile(
    r"(?m)^(?P<indent>[ \t]*)// (?P<op>" + "|".join(OPCODES) +
    r") (?P<args>[^\r\n]+)\r?\n(?=[ \t]*(?:// |loc_|\}))"
)
D_ARGS = re.compile(r"([rf])(\d+),(-?\d+)\(r(\d+)\)")
X_ARGS = re.compile(r"([rf])(\d+),r(\d+),r(\d+)")

# XenonRecomp also silently omits PPC EQV (64-bit bitwise XNOR).
# Restore the register operation without changing XER or CR.
EQV_MISSING = re.compile(
    r"(?m)^(?P<indent>[ \t]*)// eqv (?P<args>[^\r\n]+)\r?\n(?=[ \t]*(?:// |loc_|\}))"
)
EQV_ARGS = re.compile(r"r(\d+),r(\d+),r(\d+)")
# bdzf is CTR-- followed by a branch if CTR == 0 and the CR bit is false.
# The supplied title uses CR6.EQ for switch dispatch, with both local labels
# and tail branches to another generated function entrypoint.
BDZF_MISSING = re.compile(
    r"(?m)^(?P<indent>[ \t]*)// bdzf (?P<args>[^\r\n]+)\r?\n(?=[ \t]*(?:// |loc_|\}))"
)
BDZF_ARGS = re.compile(r"4\*cr6\+eq,0x([0-9a-fA-F]{8})")



def repair(text: str, counts: Counter) -> str:
    def replace(match: re.Match) -> str:
        op, args = match["op"], match["args"]
        d_form = op in D_FORMS
        parsed = (D_ARGS if d_form else X_ARGS).fullmatch(args)
        if parsed is None:
            raise ValueError(f"Unrecognized {op} operands: {args}")
        prefix, dest = parsed[1], int(parsed[2])
        base = int(parsed[4] if d_form else parsed[3])
        kind, width = OPCODES[op]
        expected_prefix = "f" if kind.startswith("float") else "r"
        if prefix != expected_prefix:
            raise ValueError(f"Invalid {op} register type: {args}")
        if not (0 <= dest <= 31 and 1 <= base <= 31):
            raise ValueError(f"Invalid PPC update register in {op} {args}")
        if kind in ("load", "load_signed") and dest == base:
            raise ValueError(f"Undefined PPC update-load destination in {op} {args}")
        if d_form:
            offset = int(parsed[3])
            if not (-32768 <= offset <= 32767):
                raise ValueError(f"Out-of-range D-form displacement: {args}")
            address = f"ctx.r{base}.u32 + ({offset})"
        else:
            index = int(parsed[4])
            if not (0 <= index <= 31):
                raise ValueError(f"Invalid PPC index register: {args}")
            address = f"ctx.r{base}.u32 + ctx.r{index}.u32"
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
        elif kind == "load_signed":
            lines.append(
                f"{indent}    ctx.r{dest}.s64 = int16_t(PPC_LOAD_U16(ppc_update_ea));"
            )
        elif kind == "store":
            lines.append(
                f"{indent}    PPC_STORE_U{width}(ppc_update_ea, ctx.r{dest}.u{width});"
            )
        elif kind == "float_load":
            lines.append(f"{indent}    ctx.fpscr.disableFlushMode();")
            if width == 32:
                lines.extend([
                    f"{indent}    PPCRegister ppc_update_temp{{}};",
                    f"{indent}    ppc_update_temp.u32 = PPC_LOAD_U32(ppc_update_ea);",
                    f"{indent}    ctx.f{dest}.f64 = double(ppc_update_temp.f32);",
                ])
            else:
                lines.append(
                    f"{indent}    ctx.f{dest}.u64 = PPC_LOAD_U64(ppc_update_ea);"
                )
        else:
            lines.append(f"{indent}    ctx.fpscr.disableFlushMode();")
            if width == 32:
                lines.extend([
                    f"{indent}    PPCRegister ppc_update_temp{{}};",
                    f"{indent}    ppc_update_temp.f32 = float(ctx.f{dest}.f64);",
                    f"{indent}    PPC_STORE_U32(ppc_update_ea, ppc_update_temp.u32);",
                ])
            else:
                lines.append(
                    f"{indent}    PPC_STORE_U64(ppc_update_ea, ctx.f{dest}.u64);"
                )
        lines.extend([
            f"{indent}    ctx.r{base}.u32 = ppc_update_ea;",
            f"{indent}}}",
        ])
        counts[op] += 1
        return "\n".join(lines) + "\n"
    fixed = MISSING.sub(replace, text)

    def replace_eqv(match: re.Match) -> str:
        parsed = EQV_ARGS.fullmatch(match["args"])
        if parsed is None or any(int(reg) > 31 for reg in parsed.groups()):
            raise ValueError(f"Invalid PPC eqv operands: {match['args']}")
        dest, source_a, source_b = map(int, parsed.groups())
        indent = match["indent"]
        counts["eqv"] += 1
        return (
            f"{indent}// eqv {match['args']}\n"
            f"{indent}ctx.r{dest}.u64 = ~(ctx.r{source_a}.u64 ^ ctx.r{source_b}.u64);\n"
        )

    fixed = EQV_MISSING.sub(replace_eqv, fixed)

    def replace_bdzf(match: re.Match) -> str:
        parsed = BDZF_ARGS.fullmatch(match["args"])
        if parsed is None:
            raise ValueError(f"Unsupported PPC bdzf condition or target: {match['args']}")
        target = int(parsed.group(1), 16)
        function_start = fixed.rfind("PPC_FUNC_IMPL(", 0, match.start())
        if function_start < 0:
            raise ValueError("PPC bdzf occurs outside a generated function")
        function_end = fixed.find("PPC_FUNC_IMPL(", match.end())
        if function_end < 0:
            function_end = len(fixed)
        label = f"loc_{target:08X}"
        if f"{label}:" in fixed[function_start:function_end]:
            branch = f"goto {label};"
        else:
            # A branch to a different generated function is a tail branch:
            # never fall through to the next instruction after the call.
            branch = f"sub_{target:08X}(ctx, base); return;"
        indent = match["indent"]
        counts["bdzf"] += 1
        return (
            f"{indent}// bdzf {match['args']}\n"
            f"{indent}ctx.ctr.u64 -= 1U;\n"
            f"{indent}if (ctx.ctr.u64 == 0U && !ctx.cr6.eq) {{ {branch} }}\n"
        )

    fixed = BDZF_MISSING.sub(replace_bdzf, fixed)
    if MISSING.search(fixed) or EQV_MISSING.search(fixed) or BDZF_MISSING.search(fixed):
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
