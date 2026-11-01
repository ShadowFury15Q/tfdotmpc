#!/usr/bin/env python3
"""Read-only direct-call reachability from a privately recompiled Xbox title entrypoint.

This is a conservative static graph: indirect calls, runtime conditions,
function-pointer dispatch and Xbox kernel callbacks are not inferred. It never
copies or publishes game source, image bytes or executable contents.
"""
import argparse
from collections import defaultdict, deque
import json
from pathlib import Path
import re

UNIT = re.compile(r'^ppc_recomp\.(\d+)\.cpp$')
DEF = re.compile(r'^\s*PPC_FUNC_IMPL\s*\(\s*([A-Za-z_]\w*)\s*\)\s*\{')
CALL = re.compile(r'^\s*([A-Za-z_]\w*)\s*\(\s*ctx\s*,\s*base\s*\)\s*;')


def read_direct_graph(directory: Path):
    """Return function -> direct callees without exposing original source."""
    graph = defaultdict(set)
    units = sorted((p for p in directory.iterdir() if UNIT.fullmatch(p.name)),
                   key=lambda p: int(UNIT.fullmatch(p.name).group(1)))
    if not units:
        raise ValueError('No private PPC translation units found')
    for unit in units:
        current = None
        with unit.open(encoding='utf-8') as source:
            for line in source:
                match = DEF.match(line)
                if match:
                    current = match.group(1)
                    graph[current]
                elif current and (match := CALL.match(line)):
                    graph[current].add(match.group(1))
    return graph, len(units)


def reachable(graph, entry='_xstart', depth_limit=8):
    """Resolve generated guest aliases; classify external direct imports."""
    if depth_limit < 0:
        raise ValueError('Depth limit must be nonnegative')

    def resolve(name):
        if name in graph:
            return name
        wrapped = '__imp__' + name
        return wrapped if wrapped in graph else None

    root = resolve(entry)
    if root is None:
        raise ValueError('Original entrypoint has no generated implementation: ' + entry)
    queue = deque([(root, 0)])
    seen = {root}
    external = defaultdict(lambda: {'minimum_depth': None, 'callers': set()})
    edges = 0
    while queue:
        function, depth = queue.popleft()
        if depth >= depth_limit:
            continue
        for callee in graph[function]:
            edges += 1
            resolved = resolve(callee)
            if resolved:
                if resolved not in seen:
                    seen.add(resolved)
                    queue.append((resolved, depth + 1))
            elif callee.startswith('__imp__'):
                item = external[callee]
                item['minimum_depth'] = min(item['minimum_depth'], depth + 1) if item['minimum_depth'] is not None else depth + 1
                item['callers'].add(function)
    imports = [
        {'symbol': name, 'minimum_depth': data['minimum_depth'],
         'reachable_direct_callers': len(data['callers'])}
        for name, data in external.items()
    ]
    imports.sort(key=lambda item: (item['minimum_depth'], item['symbol']))
    return {
        'entrypoint': entry,
        'resolved_entrypoint': root,
        'max_direct_call_depth': depth_limit,
        'reachable_generated_functions': len(seen),
        'direct_edges_visited': edges,
        'external_imports': imports,
        'limitations': 'Static direct calls only; indirect branches, runtime conditions and callbacks are not resolved.',
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('private_ppc_dir', type=Path)
    parser.add_argument('--entry', default='_xstart')
    parser.add_argument('--depth', type=int, default=8)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    graph, count = read_direct_graph(args.private_ppc_dir)
    result = reachable(graph, args.entry, args.depth)
    result['original_translation_units'] = count
    output = json.dumps(result, indent=2)
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(output + '\n', encoding='utf-8')
    print(output)


if __name__ == '__main__':
    main()
