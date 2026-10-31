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
    # Upstream currently emits a hard debug trap for vcmpbfp(128).
    # Replace it with vector per-lane bounds checks and the CR6 record flag.
    old_bounds = '''    case PPC_INST_VCMPBFP:
    case PPC_INST_VCMPBFP128:
        println("\\t__builtin_debugtrap();");
        break;'''
    new_bounds = r'''    case PPC_INST_VCMPBFP:
    case PPC_INST_VCMPBFP128:
    {
        printSetFlushMode(true);
        // Preserve B if destination and B alias.
        println("\t{} = {};", vTemp(), v(insn.operands[2]));
        for (size_t lane = 0; lane < 4; ++lane)
        {
            println("\t{}.f64 = {}.f32[{}];", temp(), v(insn.operands[1]), lane);
            println("\t{}.u32[{}] = ({}.f64 != {}.f64 || {}.f32[{}] != {}.f32[{}]) ? 0xC0000000u : (({}.f64 > {}.f32[{}] ? 0x40000000u : 0u) | ({}.f64 < -double({}.f32[{}]) ? 0x80000000u : 0u));",
                v(insn.operands[0]), lane, temp(), temp(), vTemp(), lane, vTemp(), lane, temp(), vTemp(), lane, temp(), vTemp(), lane);
        }
        if (strchr(insn.opcode->name, '.'))
        {
            println("\t{}.lt = false;", cr(6));
            println("\t{}.gt = false;", cr(6));
            println("\t{}.eq = ({}.u32[0] | {}.u32[1] | {}.u32[2] | {}.u32[3]) == 0;",
                cr(6), v(insn.operands[0]), v(insn.operands[0]), v(insn.operands[0]), v(insn.operands[0]));
            println("\t{}.so = false;", cr(6));
        }
        break;
    }'''
    if patched.count(old_bounds) != 1:
        raise SystemExit(f"ERROR: vcmpbfp trap anchor changed; found {patched.count(old_bounds)}")
    patched = patched.replace(old_bounds, new_bounds, 1)
    # A linked branch to LR is an indirect call, not a debug trap.
    old_blrl = '''    case PPC_INST_BLRL:
        println("__builtin_debugtrap();");
        break;'''
    new_blrl = r'''    case PPC_INST_BLRL:
        println("\t{}.u32 = uint32_t(ctx.lr);", temp());
        if (!config.skipLr)
            println("\tctx.lr = 0x{:X};", base + 4);
        println("\tPPC_CALL_INDIRECT_FUNC({}.u32);", temp());
        csrState = CSRState::Unknown;
        break;'''
    if patched.count(old_blrl) != 1:
        raise SystemExit(f"ERROR: BLRL trap anchor changed; found {patched.count(old_blrl)}")
    patched = patched.replace(old_blrl, new_blrl, 1)
    source.write_text(patched, encoding="utf-8")
    shutil.copyfile(patch, header)
    shutil.copyfile(extra, upstream / "XenonRecomp" / "dotm_extra_cases.inc")
    print(f"Patched {source} with {len(patch.read_text().splitlines())} lines")
    print(f"Cases defined: {patch.read_text().count('case PPC_INST_') + extra.read_text().count('case PPC_INST_')}")

if __name__ == "__main__":
    main()
