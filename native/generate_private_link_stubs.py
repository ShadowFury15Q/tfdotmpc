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
# XenonRecomp emits a guest-visible alias for each __imp__ implementation.
ALIAS = re.compile(
    r'__attribute__\s*\(\(\s*alias\s*\(\s*"([A-Za-z_]\w*)"\s*\)\s*\)\)'
    r'\s*PPC_WEAK_FUNC\s*\(\s*([A-Za-z_]\w*)\s*\)\s*;'
)
MAPPING = re.compile(r"\{\s*(0x[0-9A-Fa-f]+)\s*,\s*([A-Za-z_]\w*)\s*\}")
HELPER = re.compile(r"__?(?:save|rest)(?:gprlr|fpr|vmx)_\d+")
NATIVE = {"__imp__XGetAVPack", "__imp__KeGetCurrentProcessType",
          "__imp__RtlFillMemoryUlong", "__imp__RtlCompareMemoryUlong",
          "__imp__XexCheckExecutablePrivilege",
          "__imp__RtlImageXexHeaderField",
          "__imp__RtlInitializeCriticalSection",
          "__imp__RtlInitializeCriticalSectionAndSpinCount",
          "__imp__RtlEnterCriticalSection",
          "__imp__RtlTryEnterCriticalSection",
          "__imp__RtlLeaveCriticalSection",
          "__imp__KeTlsAlloc", "__imp__KeTlsFree", "__imp__KeTlsGetValue", "__imp__KeTlsSetValue",
          "__imp__XamInputGetState", "__imp__XamInputGetCapabilities",
          "__imp__XamLoaderSetLaunchData",
          "__imp__XamLoaderGetLaunchDataSize",
          "__imp__XamLoaderGetLaunchData",
          "__imp__NtAllocateVirtualMemory", "__imp__NtFreeVirtualMemory",
          "__imp__NtQueryVirtualMemory",
          "__imp__XexLoadImage", "__imp__XexGetModuleHandle",
          "__imp__XexGetProcedureAddress", "__imp__XexUnloadImage"} | {
              f"__{action}gprlr_{reg}"
              for action in ("save", "rest") for reg in range(14, 32)
          } | {
              f"__{action}fpr_{reg}"
              for action in ("save", "rest") for reg in range(14, 32)
          }


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
        # Integer constant expressions are common in generated C++ guards.
        # Recognize only self-contained integer literals: never guess the
        # value of a macro, operator expression, or platform-specific #if.
        # A leading logical negation of an integer literal is still a
        # compile-time constant. Unwrap only complete parenthesized expressions;
        # do not evaluate macros or compound expressions speculatively.
        negated = False
        while True:
            expr = expr.strip()
            if expr.startswith('!'):
                negated = not negated
                expr = expr[1:]
                continue
            if expr.startswith('(') and expr.endswith(')'):
                depth = 0
                wrapped = True
                for index, char in enumerate(expr):
                    if char == '(':
                        depth += 1
                    elif char == ')':
                        depth -= 1
                        if depth < 0 or (depth == 0 and index != len(expr) - 1):
                            wrapped = False
                            break
                if wrapped and depth == 0:
                    expr = expr[1:-1]
                    continue
            break
        match = re.fullmatch(
            r'(0[xX][0-9a-fA-F]+|0[bB][01]+|0[0-7]*|[1-9][0-9]*)[uUlL]*',
            expr,
        )
        if match is None:
            return None
        number = match.group(1)
        if number.lower().startswith('0x'):
            value = int(number[2:], 16)
        elif number.lower().startswith('0b'):
            value = int(number[2:], 2)
        elif len(number) > 1 and number.startswith('0'):
            value = int(number, 8)
        else:
            value = int(number, 10)
        return bool(value) != negated

    stack = []
    inactive = False
    output = []
    lines = code.splitlines(keepends=True)
    index = 0
    while index < len(lines):
        line = lines[index]
        # Mask every directive, including #pragma/#undef and continuations:
        # nonconditional directives are not C++ function definitions.
        match = re.match(r'^\s*#\s*([A-Za-z_]\w*)\b(.*)', line)
        if match:
            # C/C++ phase 2 splices backslash-newline before parsing #if.
            # A continued literal 0 must not expose disabled PPC definitions.
            directive_lines = [line]
            while (directive_lines[-1].rstrip('\r\n').endswith('\\')
                   and index + 1 < len(lines)):
                index += 1
                directive_lines.append(lines[index])
            directive_code = re.sub(r'\\\r?\n', ' ', ''.join(directive_lines))
            match = re.match(
                r'^\s*#\s*([A-Za-z_]\w*)\b(.*)',
                directive_code
            )
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
            output.extend(mask(part) for part in directive_lines)
        else:
            output.append(mask(line) if inactive else line)
        index += 1
    return ''.join(output)


def cpp_code_only(source: str, preserve_alias_targets: bool = False) -> str:
    # C++ translation phase 2 removes backslash-newline before tokenization.
    # Splice before comment stripping: even // or PPC_FUNC_IMPL can be split.
    source = re.sub(r'\\\r?\n', '', source)

    def mask_noncode(match):
        token = match.group(0)
        # Preserve only the quoted target of a GCC alias attribute. Ordinary
        # strings, raw strings and comments must not manufacture definitions.
        if (preserve_alias_targets and token.startswith('"') and
                re.search(r'\balias\s*\(\s*$', source[max(0, match.start() - 80):match.start()])):
            return token
        return re.sub(r'[^\r\n]', ' ', token)

    code = CPP_NONCODE.sub(mask_noncode, source)
    return strip_inactive_preprocessor(code)


def generate(directory: Path, minimum_units: int = 697) -> tuple[str, int, int]:
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
        source = path.read_text(encoding="utf-8")
        bodies = DEF.findall(cpp_code_only(source))
        definition_counts.update(bodies)
        if path == mapping:
            continue
        # GCC aliases require their implementation body in the same C++ unit.
        local_bodies = set(bodies)
        for target, guest_symbol in ALIAS.findall(cpp_code_only(source, preserve_alias_targets=True)):
            if target not in local_bodies:
                raise ValueError(f"Missing XenonRecomp alias target for {guest_symbol}: {target}")
            definition_counts[guest_symbol] += 1
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
        '#include <stdexcept>',
        '// PRIVATE LINK PROBE ONLY: unresolved PPC imports throw diagnostic errors.',
        '// These are not implemented Xbox kernel, XAM or register-save semantics.',
    ]
    for name in unresolved:
        lines.extend([
            f"PPC_FUNC({name}) {{",
            f'    throw std::runtime_error("Unimplemented PPC external: {name}");',
            "}",
        ])
    return "\n".join(lines) + "\n", len(units), len(unresolved)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("private_ppc_dir", type=Path)
    parser.add_argument("output_cpp", type=Path)
    parser.add_argument("--minimum-units", type=int, default=697)
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
