#!/usr/bin/env python3
"""Synthetic-only regression tests for the original entrypoint reachability audit."""
import tempfile
import unittest
from pathlib import Path
from audit_entrypoint_reachability import read_direct_graph, reachable


class EntryGraphTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_alias_resolution_and_transitive_imports(self):
        (self.root / 'ppc_recomp.10.cpp').write_text('''
PPC_FUNC_IMPL(__imp___xstart) {
  sub_82500000(ctx, base);
  __imp__DbgPrint(ctx, base);
}
''')
        (self.root / 'ppc_recomp.2.cpp').write_text('''
PPC_FUNC_IMPL(__imp__sub_82500000) {
  __imp__XamInputGetState(ctx, base);
  sub_82500004(ctx, base);
}
PPC_FUNC_IMPL(__imp__sub_82500004) {
  sub_82500000(ctx, base);
}
''')
        graph, units = read_direct_graph(self.root)
        self.assertEqual(units, 2)
        result = reachable(graph, '_xstart', 3)
        self.assertEqual(result['reachable_generated_functions'], 3)
        self.assertEqual(result['direct_edges_visited'], 5)
        self.assertEqual(result['external_imports'], [
            {'symbol': '__imp__DbgPrint', 'minimum_depth': 1, 'reachable_direct_callers': 1},
            {'symbol': '__imp__XamInputGetState', 'minimum_depth': 2, 'reachable_direct_callers': 1},
        ])

    def test_depth_limit_does_not_claim_deeper_import(self):
        (self.root / 'ppc_recomp.0.cpp').write_text('''
PPC_FUNC_IMPL(__imp___xstart) {
  sub_82500000(ctx, base);
}
PPC_FUNC_IMPL(__imp__sub_82500000) {
  __imp__XamInputGetState(ctx, base);
}
''')
        graph, _ = read_direct_graph(self.root)
        self.assertEqual(reachable(graph, depth_limit=1)['external_imports'], [])
        self.assertEqual(reachable(graph, depth_limit=2)['external_imports'][0]['minimum_depth'], 2)

    def test_missing_entrypoint_fails_closed(self):
        (self.root / 'ppc_recomp.0.cpp').write_text('PPC_FUNC_IMPL(other) {\n}\n')
        graph, _ = read_direct_graph(self.root)
        with self.assertRaisesRegex(ValueError, 'entrypoint'):
            reachable(graph)
        with self.assertRaisesRegex(ValueError, 'nonnegative'):
            reachable(graph, depth_limit=-1)


if __name__ == '__main__':
    unittest.main()
