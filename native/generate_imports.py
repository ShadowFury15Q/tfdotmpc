#!/usr/bin/env python3
"""Build explicitly failing C++ handlers for all unresolved Xbox 360 API imports.
Usage: python native/generate_imports.py <ppc_recomp_shared.h> <out.cpp>
Generated handlers intentionally abort; this is NOT an Xbox 360 API implementation.
"""
from pathlib import Path
import re
import sys

def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    source = Path(sys.argv[1]).read_text(encoding="utf-8")
    names = sorted(set(re.findall(r"PPC_EXTERN_FUNC\((__imp__(?!sub_)[A-Za-z0-9_]+)\);", source)))
    if len(names) < 200:
        raise SystemExit(f"Unexpected number of platform imports: {len(names)}")
    lines = ['#include "ppc_recomp_shared.h"', '#include <cstdio>', '#include <cstdlib>',
             '// Unsupported API imports must abort rather than silently fake success.']
    for name in names:
        lines += [f'PPC_FUNC({name}) {{',
                  f'    std::fprintf(stderr, "Xbox 360 API NOT IMPLEMENTED: {name}\\n");',
                  '    std::abort();',
                  '}']
    Path(sys.argv[2]).write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Generated {len(names)} native import handlers (fail-fast diagnostics)")
    return 0

if __name__ == "__main__":
    sys.exit(main())
