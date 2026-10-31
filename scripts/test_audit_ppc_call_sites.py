#!/usr/bin/env python3
"""Synthetic fixtures only; no original DOTM files are published."""
import tempfile
import unittest
from pathlib import Path
from audit_ppc_call_sites import scan


class PpcCallSiteAuditTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)

    def write(self, name, content):
        (self.directory / name).write_text(content, encoding='utf-8')

    def test_direct_call_address_and_caller(self):
        self.write('ppc_recomp.3.cpp', '''
PPC_FUNC_IMPL(__imp__sub_82525808) {
  // li r3,0
  ctx.r3.s64 = 0;
  // bl 0x8380f574
  ctx.lr = 0x82525840;
  __imp__VdRetrainEDRAMWorker(ctx, base);
  // bl 0x8380f564
  ctx.lr = 0x82525868;
  __imp__VdRetrainEDRAM(ctx, base);
}
''')
        sites = list(scan(self.directory, '__imp__VdRetrainEDRAMWorker'))
        self.assertEqual(len(sites), 1)
        self.assertEqual(sites[0]['caller'], '__imp__sub_82525808')
        self.assertEqual(sites[0]['call_address'], '0x8252583C')
        self.assertEqual(sites[0]['return_address'], '0x82525840')
        self.assertEqual(sites[0]['preceding_instructions'][-2]['mnemonic'], 'li')
        self.assertEqual(sites[0]['preceding_instructions'][-1]['mnemonic'], 'bl')
        self.assertEqual(list(scan(self.directory, '__imp__VdRetrainEDRAM'))[0]
                         ['call_address'], '0x82525864')

    def test_no_stale_return_address(self):
        self.write('ppc_recomp.0.cpp', '''
PPC_FUNC_IMPL(__imp__sub_82500000) {
  ctx.lr = 0x82500014;
  ctx.r3.u64 = 7;
  ctx.r4.u64 = 8;
  ctx.r5.u64 = 9;
  __imp__Something(ctx, base);
}
''')
        site = list(scan(self.directory, '__imp__Something'))[0]
        self.assertIsNone(site['call_address'])
        self.assertIsNone(site['return_address'])

    def test_sorted_units_and_callers(self):
        self.write('ppc_recomp.10.cpp', '''PPC_FUNC_IMPL(__imp__sub_82501000) {
  ctx.lr = 0x82501010;
  sub_82525808(ctx, base);
}
''')
        self.write('ppc_recomp.2.cpp', '''PPC_FUNC_IMPL(__imp__sub_82500200) {
  ctx.lr = 0x82500210;
  sub_82525808(ctx, base);
}
''')
        sites = list(scan(self.directory, callers_of='sub_82525808'))
        self.assertEqual([s['unit'] for s in sites],
                         ['ppc_recomp.2.cpp', 'ppc_recomp.10.cpp'])

    def test_missing_units_is_error(self):
        with self.assertRaisesRegex(ValueError, 'No ppc_recomp'):
            list(scan(self.directory, '__imp__Missing'))


if __name__ == '__main__':
    unittest.main()
