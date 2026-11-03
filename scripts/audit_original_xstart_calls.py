#!/usr/bin/env python3
"""Audit actual XenonRecomp direct-call reachability from original DOTM _xstart.

Only scans user-owned ppc_recomp.*.cpp; no proprietary content is emitted.
Indirect PPC_CALL_INDIRECT_FUNC sites are counted, never guessed as edges.
"""
import argparse
import collections
import json
from pathlib import Path
import re

FUNCTION = re.compile(r"^PPC_FUNC_IMPL\((__imp__\w+)\)\s*\{", re.M)
DIRECT = re.compile(r"^\s*([A-Za-z_]\w*)\(ctx, base\);\s*$", re.M)
INDIRECT = re.compile(r"\bPPC_CALL_INDIRECT_FUNC\s*\(")
ADDRESS = re.compile(r"^sub_([0-9A-Fa-f]{8})$")


def audit(directory, entry="_xstart"):
    bodies = {}
    files = sorted(Path(directory).glob("ppc_recomp.*.cpp"))
    if not files:
        raise ValueError("No original ppc_recomp.*.cpp files")
    for file in files:
        source = file.read_text(encoding="utf-8")
        matches = list(FUNCTION.finditer(source))
        for index, match in enumerate(matches):
            name = match.group(1)
            if name in bodies:
                raise ValueError("Duplicate original PPC function: " + name)
            end = matches[index + 1].start() if index + 1 < len(matches) else len(source)
            bodies[name] = source[match.end():end]
    start = "__imp__" + entry
    if start not in bodies:
        raise ValueError("Original entrypoint not found: " + start)
    queue = collections.deque([(start, 0)])
    visited = set()
    missing = set()
    external = set()
    indirect_sites = 0
    max_depth = 0
    edges = 0
    while queue:
        name, depth = queue.popleft()
        if name in visited:
            continue
        visited.add(name)
        max_depth = max(max_depth, depth)
        body = bodies[name]
        indirect_sites += len(INDIRECT.findall(body))
        for target in DIRECT.findall(body):
            if not (target.startswith("sub_") or target.startswith("__imp__")):
                continue
            edges += 1
            canonical = "__imp__" + target if target.startswith("sub_") else target
            if canonical in bodies:
                if canonical not in visited:
                    queue.append((canonical, depth + 1))
            elif target.startswith("__imp__"):
                external.add(target)
            else:
                missing.add(target)
    return {
        "entrypoint": entry,
        "original_translation_units": len(files),
        "original_function_bodies": len(bodies),
        "reachable_direct_function_bodies": len(visited),
        "direct_call_edges": edges,
        "maximum_direct_call_depth": max_depth,
        "indirect_call_sites_in_reachable_bodies": indirect_sites,
        "unresolved_original_direct_targets": sorted(missing),
        "external_import_calls": sorted(external),
        "warning": "Static direct-call overapproximation; runtime branches and indirect calls are not resolved."
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ppc_directory", type=Path)
    parser.add_argument("--entry", default="_xstart")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        report = audit(args.ppc_directory, args.entry)
    except (OSError, ValueError) as exc:
        parser.exit(1, f"Original PPC reachability audit failed: {exc}\n")
    result = json.dumps(report, indent=2, ensure_ascii=False) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(result, encoding="utf-8")
    else:
        print(result, end="")


if __name__ == "__main__":
    main()
