#!/usr/bin/env python3
"""Synthetic regression tests for PPC update-form repair."""
from collections import Counter
from repair_ppc_update_ops import repair

cases = {
    "lhzu r10,2(r8)": ("PPC_LOAD_U16", "ctx.r8.u32 = ppc_update_ea"),
    "lhzu r8,-2(r10)": ("ctx.r10.u32 + (-2)", "ctx.r8.u64"),
    "sthu r31,2(r9)": ("PPC_STORE_U16", "ctx.r31.u16"),
    "lwzux r9,r11,r5": ("PPC_LOAD_U32", "ctx.r11.u32 = ppc_update_ea"),
    "lhzux r10,r21,r11": ("PPC_LOAD_U16", "ctx.r21.u32 = ppc_update_ea"),
    "stbux r10,r22,r17": ("PPC_STORE_U8", "ctx.r10.u8"),
    "sthux r10,r20,r9": ("PPC_STORE_U16", "ctx.r10.u16"),
    "lbzux r11,r25,r23": ("PPC_LOAD_U8", "ctx.r25.u32 = ppc_update_ea"),
    "ldux r8,r11,r22": ("PPC_LOAD_U64", "ctx.r11.u32 = ppc_update_ea"),
}
for instruction, expected in cases.items():
    source = "\t// " + instruction + "\n\t// next instruction\n"
    counts = Counter()
    fixed = repair(source, counts)
    assert sum(counts.values()) == 1, instruction
    assert all(fragment in fixed for fragment in expected), instruction
    assert repair(fixed, Counter()) == fixed, instruction
    assert "// next instruction" in fixed, instruction
implemented = "\t// lwzu r9,4(r10)\n\tea = 4 + ctx.r10.u32;\n"
assert repair(implemented, Counter()) == implemented
for invalid in ("lhzu r8,2(r8)", "sthu r1,40000(r2)",
                "lwzux r4,r0,r5", "lhzu r10,abc(r8)"):
    try:
        repair("\t// " + invalid + "\n\t// next\n", Counter())
    except ValueError:
        continue
    raise AssertionError(f"Invalid update form accepted: {invalid}")
print("PPC update-form repair synthetic tests PASS")
