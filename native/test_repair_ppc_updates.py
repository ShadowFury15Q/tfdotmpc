#!/usr/bin/env python3
"""Synthetic regression tests for PPC update-form and EQV repair."""
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
    "lhau r9,2(r30)": ("int16_t(PPC_LOAD_U16", "ctx.r30.u32 = ppc_update_ea"),
    "lfsu f13,4(r8)": ("ctx.f13.f64 = double(ppc_update_temp.f32)", "ctx.r8.u32 = ppc_update_ea"),
    "stfsu f0,12(r11)": ("ppc_update_temp.f32 = float(ctx.f0.f64)", "PPC_STORE_U32"),
    "lfsux f25,r11,r30": ("PPC_LOAD_U32", "ctx.r11.u32 = ppc_update_ea"),
    "stfsux f10,r11,r8": ("PPC_STORE_U32", "ctx.r11.u32 = ppc_update_ea"),
    "lfdu f0,8(r10)": ("ctx.f0.u64 = PPC_LOAD_U64", "ctx.r10.u32 = ppc_update_ea"),
    "stfdu f13,8(r9)": ("PPC_STORE_U64(ppc_update_ea, ctx.f13.u64)", "ctx.r9.u32 = ppc_update_ea"),
    "lfdux f2,r3,r4": ("ctx.f2.u64 = PPC_LOAD_U64", "ctx.r3.u32 = ppc_update_ea"),
    "eqv r9,r11,r30": ("ctx.r9.u64 = ~(ctx.r11.u64 ^ ctx.r30.u64)", "// next instruction"),
}
for instruction, expected in cases.items():
    source = "\t// " + instruction + "\n\t// next instruction\n"
    counts = Counter()
    fixed = repair(source, counts)
    assert sum(counts.values()) == 1, instruction
    assert all(fragment in fixed for fragment in expected), instruction
    assert repair(fixed, Counter()) == fixed, instruction
    assert "// next instruction" in fixed, instruction
local_branch = (
    "PPC_FUNC_IMPL(__imp__sub_82500000) {\n"
    "  // bdzf 4*cr6+eq,0x82500020\n"
    "  // next instruction\n"
    "loc_82500020:\n"
    "  return;\n"
    "}\n"
)
counts = Counter()
fixed = repair(local_branch, counts)
assert counts["bdzf"] == 1
assert "ctx.ctr.u64 -= 1U;" in fixed
assert "if (ctx.ctr.u64 == 0U && !ctx.cr6.eq) { goto loc_82500020; }" in fixed
assert repair(fixed, Counter()) == fixed
# Earlier update-form expansions shift source offsets. The bdzf repair must
# search the already-patched source, not the original pre-expansion string.
shifted_branch = (
    "PPC_FUNC_IMPL(__imp__sub_82500000) {\n"
    "  // lhzu r10,2(r8)\n"
    "  // next instruction\n"
    "  // bdzf 4*cr6+eq,0x82500020\n"
    "  // next instruction\n"
    "loc_82500020:\n"
    "  return;\n"
    "}\n"
    "PPC_FUNC_IMPL(__imp__sub_82500030) { return; }\n"
)
fixed = repair(shifted_branch, Counter())
assert "if (ctx.ctr.u64 == 0U && !ctx.cr6.eq) { goto loc_82500020; }" in fixed
assert "sub_82500020(ctx, base)" not in fixed
tail_branch = (
    "PPC_FUNC_IMPL(__imp__sub_82500000) {\n"
    "  // bdzf 4*cr6+eq,0x82500030\n"
    "  // next instruction\n"
    "}\n"
    "PPC_FUNC_IMPL(__imp__sub_82500030) { return; }\n"
)
counts = Counter()
fixed = repair(tail_branch, counts)
assert counts["bdzf"] == 1
assert "if (ctx.ctr.u64 == 0U && !ctx.cr6.eq) { sub_82500030(ctx, base); return; }" in fixed
assert repair(fixed, Counter()) == fixed
implemented = "\t// lwzu r9,4(r10)\n\tea = 4 + ctx.r10.u32;\n"
assert repair(implemented, Counter()) == implemented
for invalid in ("lhzu r8,2(r8)", "sthu r1,40000(r2)",
                "lwzux r4,r0,r5", "lhzu r10,abc(r8)",
                "lfsu r13,4(r8)", "stfsu f32,4(r8)",
                "lfdu f0,4(r0)", "lhau r8,2(r8)",
                "eqv r32,r1,r2", "eqv r1,r2", "eqv r0,r1,r33"):
    try:
        repair("\t// " + invalid + "\n\t// next\n", Counter())
    except ValueError:
        continue
    raise AssertionError(f"Invalid update form accepted: {invalid}")

for invalid_branch in ("4*cr6+lt,0x82500020", "4*cr6+eq,not_an_address"):
    try:
        repair("PPC_FUNC_IMPL(test) {\n  // bdzf " + invalid_branch +
               "\n  // next\n}\n", Counter())
    except ValueError:
        continue
    raise AssertionError(f"Invalid PPC bdzf accepted: {invalid_branch}")
print("PPC update-form, EQV and bdzf repair synthetic tests PASS")
