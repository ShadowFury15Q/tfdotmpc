#!/usr/bin/env python3
"""Synthetic-only regression for private PPC intra-function bctr dispatch."""
from repair_ppc_jump_tables import (
    DOTM_TABLES, decode_table_targets, patch_bctr_function, read_pe_guest)


def main():
    assert len(DOTM_TABLES) == 6
    assert DOTM_TABLES[0] == (
        2, '__imp__sub_825150F8', 0x825150F8, 0x82450290,
        16, 0x82515218, 1, 4)
    assert DOTM_TABLES[4] == (
        484, '__imp__sub_8351C7F8', 0x8351C7F8, 0x824059A0,
        105, 0x8351C86C, 1, 4)
    assert DOTM_TABLES[-1] == (
        291, '__imp__sub_82F43A30', 0x82F43A30, 0x82F43A64,
        36, 0, 4, 1)
    assert decode_table_targets(bytes.fromhex('82f43af482f43b58'), 0, 4, 1) == {
        0x82F43AF4, 0x82F43B58}
    assert decode_table_targets(bytes([0, 72]), 0x8351C86C, 1, 4) == {
        0x8351C86C, 0x8351C98C}
    assert 0x825152B8 in decode_table_targets(bytes([40]), 0x82515218, 1, 4)
    assert decode_table_targets(bytes([0, 1, 0, 3]), 0x832987D8, 2, 1) == {
        0x832987D9, 0x832987DB}
    assert decode_table_targets(bytes([0, 2, 5]), 0x83296F70, 1, 4) == {
        0x83296F70, 0x83296F78, 0x83296F84}
    try:
        decode_table_targets(bytes([0]), 0x83296F70, 2, 1)
    except ValueError:
        pass
    else:
        raise AssertionError('Truncated PPC switch table accepted')
    # Synthetic disk PE and XenonRecomp expanded PE share the same section
    # headers, but the expanded image stores bytes at guest RVA, not file offset.
    def put16(buf, off, value):
        buf[off:off + 2] = value.to_bytes(2, 'little')

    def put32(buf, off, value):
        buf[off:off + 4] = value.to_bytes(4, 'little')

    disk = bytearray(0x600)
    put16(disk, 0, 0x5A4D)
    put32(disk, 0x3C, 0x100)
    disk[0x100:0x104] = b'PE\0\0'
    put16(disk, 0x104, 0x1F2)
    put16(disk, 0x106, 2)
    put16(disk, 0x114, 0xE0)
    put32(disk, 0x118 + 28, 0x82000000)
    put32(disk, 0x118 + 56, 0x01B10000)
    disk[0x1F8:0x1FD] = b'.text'
    put32(disk, 0x1F8 + 12, 0x300)
    put32(disk, 0x1F8 + 16, 0x10)
    put32(disk, 0x1F8 + 20, 0x200)
    disk[0x220:0x226] = b'.reloc'
    put32(disk, 0x220 + 12, 0x1AA0200)
    put32(disk, 0x220 + 16, 0x100)
    put32(disk, 0x220 + 20, 0x01B0FFF0)
    disk[0x200:0x204] = b'RAW!'
    disk[0x300:0x304] = b'RVA!'
    assert read_pe_guest(disk, 0x82000300, 4) == b'RAW!'
    expanded = bytearray(0x01B10000)
    expanded[:len(disk)] = disk
    assert read_pe_guest(expanded, 0x82000300, 4) == b'RVA!'

    source = '''PPC_FUNC_IMPL(__imp__sub_82500000) {
\t// li r3,1
\tctx.r3.u64 = 1;
\t// mtctr r12
\tctx.ctr.u64 = ctx.r12.u64;
\t// bctr 
\tPPC_CALL_INDIRECT_FUNC(ctx.ctr.u32);
\treturn;
\t// li r3,2
\tctx.r3.u64 = 2;
loc_82500010:
\t// blr 
\treturn;
}

PPC_FUNC_IMPL(__imp__sub_82500014) {
\t// blr 
\treturn;
}
'''
    patched, added = patch_bctr_function(source, '__imp__sub_82500000',
                                         0x82500000, {0x8250000C, 0x82500010})
    assert added == 1
    assert 'loc_8250000C:\n\t// li r3,2' in patched
    assert 'case 0x8250000CU: goto loc_8250000C;' in patched
    assert 'case 0x82500010U: goto loc_82500010;' in patched
    assert 'default: PPC_CALL_INDIRECT_FUNC(ctx.ctr.u32); return;' in patched
    assert 'PPC_FUNC_IMPL(__imp__sub_82500014)' in patched
    try:
        patch_bctr_function(source, '__imp__sub_82500000', 0x82500000,
                            {0x8250000D})
    except ValueError:
        pass
    else:
        raise AssertionError('Unaligned/nonexistent branch target was accepted')
    print('Synthetic intra-function PPC bctr regression passed')


if __name__ == '__main__':
    main()
