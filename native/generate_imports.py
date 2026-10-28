#!/usr/bin/env python3
"""Build explicitly failing C++ handlers for all unresolved Xbox 360 API imports.
Usage: python native/generate_imports.py <ppc_recomp_shared.h> <out.cpp> [--native-xam-input]
Generated handlers intentionally abort; this is NOT an Xbox 360 API implementation.
"""
from pathlib import Path
import re
import sys

def main() -> int:
    if len(sys.argv) not in (3, 4) or (len(sys.argv) == 4 and sys.argv[3] != "--native-xam-input"):
        print(__doc__)
        return 2
    source = Path(sys.argv[1]).read_text(encoding="utf-8")
    names = sorted(set(re.findall(r"PPC_EXTERN_FUNC\((__imp__(?!sub_)[A-Za-z0-9_]+)\);", source)))
    if len(names) < 200:
        raise SystemExit(f"Unexpected number of platform imports: {len(names)}")
    native_xam = len(sys.argv) == 4
    if native_xam:
        if "__imp__XamInputGetState" not in names:
            raise SystemExit("XamInputGetState import is not declared in private generated header")
        names.remove("__imp__XamInputGetState")
        # If the game imports capability discovery, it is implemented by
        # the same native XAM module and must not receive a duplicate stub.
        if "__imp__XamInputGetCapabilities" in names:
            names.remove("__imp__XamInputGetCapabilities")
    lines = ['#include "ppc_recomp_shared.h"', '#include <cstdio>', '#include <cstdlib>',
             '// Unsupported API imports must abort rather than silently fake success.']
    for name in names:
        lines += [f'PPC_FUNC({name}) {{',
                  f'    std::fprintf(stderr, "Xbox 360 API NOT IMPLEMENTED: {name}\\n");',
                  '    std::abort();',
                  '}']
    Path(sys.argv[2]).write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Generated {len(names)} native import handlers (fail-fast diagnostics)")
    if native_xam:
        print("XamInputGetState and supported XAM capabilities are native in xam_ppc_import.cpp")
    return 0

if __name__ == "__main__":
    sys.exit(main())
