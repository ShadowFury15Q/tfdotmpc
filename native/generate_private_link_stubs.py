#!/usr/bin/env python3
"""Generate FAIL-FAST unresolved PPC imports for a PRIVATE XenonRecomp link probe.

This tool never copies original game code to the public repository. The generated
source is written to the local CMake build directory and is not game runtime.
"""
import argparse
from collections import Counter
from pathlib import Path
import re

DECL = re.compile(r"\bPPC_EXTERN_FUNC\s*\(\s*([A-Za-z_]\w*)\s*\)\s*;")
# Only a macro followed by a function body is a definition; prototypes do not link.
DEF = re.compile(r"\bPPC_FUNC(?:_IMPL)?\s*\(\s*([A-Za-z_]\w*)\s*\)\s*\{")
MAPPING = re.compile(r"\{\s*(0x[0-9A-Fa-f]+)\s*,\s*([A-Za-z_]\w*)\s*\}")
HELPER = re.compile(r"__?(?:save|rest)(?:gprlr|fpr|vmx)_\d+")
NATIVE = {"__imp__XamInputGetState", "__imp__XamInputGetCapabilities",
          "__imp__NtAllocateVirtualMemory", "__imp__NtFreeVirtualMemory"}


# Strip C++ comments and literals before interpreting generated macro calls.
# A commented-out PPC_FUNC_IMPL must never satisfy a private game mapping.
# Preserve line breaks to avoid joining tokens across removed regions.
CPP_NONCODE = re.compile(
    r'R"(?P<raw_delimiter>[^ ()\\\t\r\n]{0,16})\([\s\S]*?\)(?P=raw_delimiter)"'
    r'|"(?:\\[\s\S]|[^"\\])*"'
    r"|'(?:\\[\s\S]|[^'\\])*'"
    r'|//(?:\\\r?\n|[^\n])*'
    r'|/\*[\s\S]*?\*/'
)


def strip_inactive_preprocessor(code: str) -> str:
    """Exclude literal #if 0 branches and preprocessor directives from link checks.

    Unknown conditional expressions remain visible rather than guessing which
    platform branch is active. Keep line breaks intact for stable diagnostics.
    """
    def mask(line: str) -> str:
        return re.sub(r'[^\r\n]', ' ', line)

    def literal_bool(expr: str):
        expr = expr.strip()
        if re.fullmatch(r'0|\(\s*0\s*\)', expr):
            return False
        if re.fullmatch(r'1|\(\s*1\s*\)', expr):
            return True
        return None

    stack = []
    inactive = False
    output = []
    continuation = False
    for line in code.splitlines(keepends=True):
        if continuation:
            output.append(mask(line))
            continuation = line.rstrip('\r\n').endswith('\\')
            continue
        match = re.match(r'^\s*#\s*(if|ifdef|ifndef|elif|else|endif|define)\b(.*)', line)
        if match:
            directive, expr = match.groups()
            if directive in ('if', 'ifdef', 'ifndef'):
                value = literal_bool(expr) if directive == 'if' else None
                stack.append({
                    'outer': inactive, 'known': value is not None,
                    'taken': value is True, 'active': value is not False
                })
                inactive = stack[-1]['outer'] or not stack[-1]['active']
            elif directive in ('elif', 'else') and stack:
                frame = stack[-1]
                if frame['known']:
                    if directive == 'else':
                        frame['active'] = not frame['taken']
                        frame['taken'] = True
                    elif frame['taken']:
                        frame['active'] = False
                    else:
                        value = literal_bool(expr)
                        if value is None:
                            frame['known'] = False
                            frame['active'] = True
                        else:
                            frame['active'] = value
                            frame['taken'] = value
                else:
                    frame['active'] = True
                inactive = frame['outer'] or not frame['active']
            elif directive == 'endif' and stack:
                inactive = stack.pop()['outer']
            output.append(mask(line))
            continuation = line.rstrip('\r\n').endswith('\\')
        else:
            output.append(mask(line) if inactive else line)
    return ''.join(output)


def cpp_code_only(source: str) -> str:
    code = CPP_NONCODE.sub(
        lambda match: re.sub(r'[^\r\n]', ' ', match.group(0)), source
    )
    return strip_inactive_preprocessor(code)


def generate(directory: Path, minimum_units: int = 696) -> tuple[str, int, int]:
    header = directory / "ppc_recomp_shared.h"
    mapping = directory / "ppc_func_mapping.cpp"
    units = sorted(directory.glob("ppc_recomp.*.cpp"))
    if not header.is_file() or not mapping.is_file():
        raise ValueError("Missing private ppc_recomp_shared.h or ppc_func_mapping.cpp")
    if len(units) < minimum_units:
        raise ValueError(f"Incomplete private recompilation: {len(units)} translation units; expected >= {minimum_units}")
    # A count alone can hide a missing XenonRecomp object if another file
    # with an unexpected index is present. Require the complete 0..N-1 set.
    indices = []
    for path in units:
        match = re.fullmatch(r"ppc_recomp\.(\d+)\.cpp", path.name)
        if not match:
            raise ValueError(f"Invalid private PPC unit filename: {path.name}")
        indices.append(int(match.group(1)))
    if sorted(indices) != list(range(len(units))):
        raise ValueError("Private PPC translation unit indices must be contiguous from 0")
    declarations = set(DECL.findall(cpp_code_only(header.read_text(encoding="utf-8"))))
    if not declarations:
        raise ValueError("No PPC_EXTERN_FUNC declarations found in private header")
    definition_counts = Counter()
    for path in [mapping, *units]:
        definition_counts.update(DEF.findall(cpp_code_only(path.read_text(encoding="utf-8"))))
    duplicates = sorted(name for name, count in definition_counts.items() if count > 1)
    if duplicates:
        raise ValueError(
            "Duplicate recompiled PPC function definitions: "
            + ", ".join(duplicates[:8])
        )
    definitions = set(definition_counts)
    if "__imp__XamInputGetState" not in declarations:
        raise ValueError("Private game header lacks expected XamInputGetState import")
    # Fail closed: abort-on-call stubs may stand in for unimplemented Xbox
    # imports and PPC save/restore helpers, NEVER for missing game functions.
    # Otherwise a truncated XenonRecomp output can falsely "link" successfully.
    mapping_entries = [
        (int(address, 16), name)
        for address, name in MAPPING.findall(cpp_code_only(mapping.read_text(encoding="utf-8")))
    ]
    if not mapping_entries:
        raise ValueError("No original PPC function mappings found")
    addresses = [address for address, _ in mapping_entries]
    if len(addresses) != len(set(addresses)):
        raise ValueError("Duplicate guest addresses in original PPC mapping")
    # Every mapped platform import must be visible to the linker. An import
    # omitted from the private header and all PPC bodies cannot be resolved
    # by either the native bridge or generated fail-fast stubs.
    missing_imports = sorted({
        name for _, name in mapping_entries
        if name.startswith("__imp__")
        and name not in declarations and name not in definitions
    })
    if missing_imports:
        raise ValueError(
            "Mapped PPC imports lack declarations or implementations: "
            + ", ".join(missing_imports[:8])
        )
    # The native dispatcher only accepts aligned addresses inside the original
    # DOTM executable range. Reject corrupt mappings before a private link can
    # appear successful and fail later during dispatch installation.
    code_begin = 0x82500000
    code_end = code_begin + 0x0132235C
    invalid = [(address, name) for address, name in mapping_entries
               if address < code_begin or address >= code_end or address % 4]
    if invalid:
        address, name = invalid[0]
        raise ValueError(
            f"Invalid original PPC mapping address 0x{address:08X} ({name}): "
            "outside executable range or not 4-byte aligned"
        )
    if dict(mapping_entries).get(0x8277F670) != "_xstart" or "_xstart" not in definitions:
        raise ValueError("Original DOTM _xstart mapping or implementation missing")
    missing_game = sorted({
        name for _, name in mapping_entries
        if not name.startswith("__imp__") and name not in definitions
    })
    if missing_game:
        raise ValueError(
            "Missing recompiled game functions (cannot stub): "
            + ", ".join(missing_game[:8])
        )
    unresolved = sorted(declarations - definitions - NATIVE)
    nonplatform = [
        name for name in unresolved
        if not name.startswith("__imp__") and not HELPER.fullmatch(name)
    ]
    if nonplatform:
        raise ValueError(
            "Unimplemented non-platform PPC symbols (cannot stub): "
            + ", ".join(nonplatform[:8])
        )
    lines = [
        '#include "ppc_recomp_shared.h"',
        '#include <cstdio>',
        '#include <cstdlib>',
        '// PRIVATE LINK PROBE ONLY: unresolved PPC imports deliberately abort.',
        '// These are not implemented Xbox kernel, XAM or register-save semantics.',
    ]
    for name in unresolved:
        lines.extend([
            f"PPC_FUNC({name}) {{",
            f'    std::fputs("Unimplemented PPC external: {name}\\n", stderr);',
            "    std::abort();",
            "}",
        ])
    return "\n".join(lines) + "\n", len(units), len(unresolved)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("private_ppc_dir", type=Path)
    parser.add_argument("output_cpp", type=Path)
    parser.add_argument("--minimum-units", type=int, default=696)
    args = parser.parse_args()
    if args.minimum_units < 1:
        parser.error("--minimum-units must be positive")
    try:
        content, count, unresolved = generate(args.private_ppc_dir, args.minimum_units)
    except ValueError as exc:
        parser.error(str(exc))
    args.output_cpp.parent.mkdir(parents=True, exist_ok=True)
    args.output_cpp.write_text(content, encoding="utf-8")
    print(f"Private link probe: {count} recompiled units; {unresolved} fail-fast unresolved PPC externals")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
