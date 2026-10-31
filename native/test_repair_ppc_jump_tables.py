#!/usr/bin/env python3
"""Synthetic-only regression for private PPC intra-function bctr dispatch."""
from repair_ppc_jump_tables import patch_bctr_function


def main():
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
