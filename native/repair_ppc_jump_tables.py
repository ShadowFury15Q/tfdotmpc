#!/usr/bin/env python3
"""Repair private PPC bctr jump tables using the user's own decrypted PE.

Only generic repair logic and table locations are public. Original PPC code,
PE section bytes, and generated patched C++ remain in the private build tree.
"""
import argparse
from pathlib import Path
import re

# Table metadata, not proprietary game bytes. Every target is read from the
# owner's private PE at build time; no jump targets are baked into this tool.
DOTM_TABLES = ((425, '__imp__sub_83294628', 0x83294628,
                0x823C0730, 56, 0x83294970),)


def read_pe_guest(pe: bytes, address: int, size: int) -> bytes:
    if len(pe) < 0x40 or pe[:2] != b'MZ':
        raise ValueError('Expected decrypted Xbox PPC PE')
    peoff = int.from_bytes(pe[0x3C:0x40], 'little')
    if peoff + 24 > len(pe) or pe[peoff:peoff + 4] != b'PE\0\0':
        raise ValueError('Invalid decrypted PE header')
    if int.from_bytes(pe[peoff + 4:peoff + 6], 'little') != 0x1F2:
        raise ValueError('Expected Xbox 360 PowerPC machine')
    count = int.from_bytes(pe[peoff + 6:peoff + 8], 'little')
    optional_size = int.from_bytes(pe[peoff + 20:peoff + 22], 'little')
    table = peoff + 24 + optional_size
    if table + count * 40 > len(pe):
        raise ValueError('Truncated PE section table')
    image_base = int.from_bytes(pe[peoff + 24 + 28:peoff + 24 + 32], 'little')
    if image_base != 0x82000000 or address < image_base:
        raise ValueError('Unexpected PPC image base or guest address')
    rva = address - image_base
    for index in range(count):
        row = table + index * 40
        virtual = int.from_bytes(pe[row + 12:row + 16], 'little')
        raw_size = int.from_bytes(pe[row + 16:row + 20], 'little')
        offset = int.from_bytes(pe[row + 20:row + 24], 'little')
        if virtual <= rva and size <= raw_size and rva - virtual <= raw_size - size:
            start = offset + rva - virtual
            if start + size > len(pe):
                raise ValueError('Truncated PPC jump-table data')
            return pe[start:start + size]
    raise ValueError('PPC jump-table data not present in PE raw sections')


def patch_bctr_function(source: str, function: str, start_address: int,
                        targets: set[int]) -> tuple[str, int]:
    declaration = f'PPC_FUNC_IMPL({function}) {{'
    if source.count(declaration) != 1:
        raise ValueError(f'Missing or duplicate private PPC function {function}')
    begin = source.index(declaration)
    end = source.find('\nPPC_FUNC_IMPL(', begin + len(declaration))
    if end < 0:
        raise ValueError('Cannot determine private PPC function boundary')
    body = source[begin:end]
    lines = body.splitlines(keepends=True)
    address = start_address
    instructions: set[int] = set()
    labels: set[int] = set()
    for line in lines:
        label = re.fullmatch(r'loc_([0-9A-Fa-f]{8}):', line.strip())
        if label:
            if int(label[1], 16) != address:
                raise ValueError('Private PPC label does not match instruction stream')
            labels.add(address)
        if line.startswith('\t// '):
            instructions.add(address)
            address += 4
    if not targets or not targets.issubset(instructions):
        raise ValueError('Jump table targets do not map to PPC instruction boundaries')
    output = []
    address = start_address
    for line in lines:
        if line.startswith('\t// '):
            if address in targets and address not in labels:
                output.append(f'loc_{address:08X}:\n')
            address += 4
        output.append(line)
    body = ''.join(output)
    branch = '\t// bctr \n\tPPC_CALL_INDIRECT_FUNC(ctx.ctr.u32);\n\treturn;'
    if body.count(branch) != 1:
        raise ValueError('Expected one original PPC bctr jump-table dispatch')
    cases = '\n'.join(
        f'\tcase 0x{target:08X}U: goto loc_{target:08X};'
        for target in sorted(targets))
    replacement = ('\t// bctr \n\tswitch (ctx.ctr.u32) {\n' + cases +
                   '\n\tdefault: PPC_CALL_INDIRECT_FUNC(ctx.ctr.u32); return;\n\t}')
    return source[:begin] + body.replace(branch, replacement) + source[end:], len(targets - labels)


def repair(directory: Path, pe_path: Path) -> tuple[int, int]:
    pe = pe_path.read_bytes()
    total_targets = total_labels = 0
    for index, function, start, table_address, count, base in DOTM_TABLES:
        raw = read_pe_guest(pe, table_address, count * 2)
        targets = {base + int.from_bytes(raw[i:i + 2], 'big')
                   for i in range(0, len(raw), 2)}
        file = directory / f'ppc_recomp.{index}.cpp'
        fixed, labels = patch_bctr_function(file.read_text(encoding='utf-8'),
                                            function, start, targets)
        file.write_text(fixed, encoding='utf-8')
        total_targets += len(targets)
        total_labels += labels
    return total_targets, total_labels


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dir', type=Path, required=True)
    parser.add_argument('--pe', type=Path, required=True)
    args = parser.parse_args()
    targets, labels = repair(args.dir, args.pe)
    print(f'Private PPC jump-table repairs: {targets} targets, {labels} labels')


if __name__ == '__main__':
    main()
