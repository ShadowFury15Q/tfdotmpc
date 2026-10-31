#!/usr/bin/env python3
"""Audit private XenonRecomp C++ call sites without publishing original game code.

The private PPC files stay outside GitHub. Addresses are derived from the
generated branch-and-link LR assignment, not inferred from a guessed ABI.
"""
import argparse
from collections import deque
import json
from pathlib import Path
import re

DEFINITION = re.compile(r'^\s*PPC_FUNC_IMPL\s*\(\s*([A-Za-z_]\w*)\s*\)\s*\{')
CALL = re.compile(r'^\s*([A-Za-z_]\w*)\s*\(\s*ctx\s*,\s*base\s*\)\s*;')
LR = re.compile(r'^\s*ctx\.lr\s*=\s*(0x[0-9A-Fa-f]+)\s*;')
PPC_INSTRUCTION = re.compile(r'^\s*//\s*([A-Za-z][A-Za-z0-9_.]*)(?:\s+(.*))?$')
SOURCE = re.compile(r'^ppc_recomp\.(\d+)\.cpp$')


def scan(directory: Path, target: str | None = None, callers_of: str | None = None):
    """Yield original direct calls; unknown call PCs remain unknown."""
    paths = sorted((p for p in directory.iterdir() if SOURCE.fullmatch(p.name)),
                   key=lambda p: int(SOURCE.fullmatch(p.name).group(1)))
    if not paths:
        raise ValueError(f'No ppc_recomp.N.cpp files in {directory}')
    for path in paths:
        function = None
        recent = deque(maxlen=20)
        previous_lr = None
        with path.open(encoding='utf-8') as stream:
            for line_no, line in enumerate(stream, 1):
                definition = DEFINITION.match(line)
                if definition:
                    function = definition.group(1)
                    recent.clear()
                    previous_lr = None
                lr = LR.match(line)
                if lr:
                    previous_lr = (int(lr.group(1), 16), line_no)
                instruction = PPC_INSTRUCTION.match(line)
                if instruction:
                    recent.append((line_no, instruction.group(1), instruction.group(2) or ''))
                call = CALL.match(line)
                if not call or not function:
                    continue
                callee = call.group(1)
                if target is not None and callee != target:
                    continue
                if callers_of is not None and callee != callers_of:
                    continue
                # PPC bl sets LR to the following 4-byte instruction.
                # Reject stale LR assignments; never fabricate call sites.
                adjacent = previous_lr and line_no - previous_lr[1] <= 2
                return_address = previous_lr[0] if adjacent else None
                yield {
                    'unit': path.name,
                    'line': line_no,
                    'caller': function,
                    'callee': callee,
                    'return_address': f'0x{return_address:08X}' if return_address is not None else None,
                    'call_address': f'0x{return_address - 4:08X}' if return_address is not None and return_address >= 4 else None,
                    'preceding_instructions': [
                        {'mnemonic': mnemonic, 'operands': operands}
                        for _, mnemonic, operands in recent
                    ],
                }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('ppc_dir', type=Path)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--target', help='Exact import or PPC callee symbol')
    group.add_argument('--callers-of', help='Find original direct callers of a PPC function')
    parser.add_argument('--json', action='store_true')
    parser.add_argument('--max-sites', type=int, default=50)
    args = parser.parse_args()
    if args.max_sites < 1:
        parser.error('--max-sites must be positive')
    sites = list(scan(args.ppc_dir, args.target, args.callers_of))
    if args.json:
        print(json.dumps({'count': len(sites), 'sites': sites[:args.max_sites]}, indent=2))
        return
    print(f'Matched {len(sites)} original PPC direct call sites')
    for site in sites[:args.max_sites]:
        print(f"{site['unit']}:{site['line']} {site['caller']} -> {site['callee']} "
              f"call={site['call_address'] or 'unknown'} "
              f"LR={site['return_address'] or 'unknown'}")
        for instruction in site['preceding_instructions'][-10:]:
            print(f"    {instruction['mnemonic']} {instruction['operands']}")
    if len(sites) > args.max_sites:
        print(f'... {len(sites) - args.max_sites} further call sites omitted')


if __name__ == '__main__':
    main()
