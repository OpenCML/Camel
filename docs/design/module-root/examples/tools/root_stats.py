#!/usr/bin/env python3
"""Count the nodes of the entry graph (__root__) of every program under the given directories.

    python3 docs/design/module-root/examples/tools/root_stats.py examples exemplars benchmarks test/cases

For each .cml file that has a `main`, runs `camel <file> std::rgir` and counts the nodes printed
at the top level of the DOT output (the nodes of __root__; EXIT/ARGS anchors are not nodes).
Prints the distribution and the programs with the largest roots. Files that do not compile
(library modules without `main`, negative tests) are skipped and counted.
"""
import collections
import concurrent.futures
import os
import re
import subprocess
import sys

NODE = re.compile(r'^    (N[0-9A-F]+) \[label="(.*?)", type="([A-Z_]+)"')
TOP_DECL = re.compile(r'^(export\s+)?(let|var)\s', re.M)


def measure(path):
    try:
        text = open(path, encoding='utf-8', errors='replace').read()
    except OSError:
        return None
    if not re.search(r'^\s*(export\s+)?(sync\s+)?func\s+main\s*\(', text, re.M):
        return None
    try:
        out = subprocess.run(['camel', path, 'std::rgir'], capture_output=True, text=True,
                             timeout=60, cwd=os.getcwd())
    except subprocess.TimeoutExpired:
        return (path, None, None, 0)
    if out.returncode != 0 or 'digraph' not in out.stdout:
        return (path, None, None, 0)
    kinds = collections.Counter()
    for line in out.stdout.splitlines():
        m = NODE.match(line)
        if m:
            kinds[m.group(3)] += 1
    return (path, sum(kinds.values()), dict(kinds), len(TOP_DECL.findall(text)))


def main(roots):
    files = []
    for root in roots:
        for d, _, names in os.walk(root):
            files += [os.path.join(d, n) for n in names if n.endswith('.cml')]
    files.sort()
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        results = [r for r in pool.map(measure, files) if r is not None]
    ok = [r for r in results if r[1] is not None]
    print(f'files: {len(files)}  with main: {len(results)}  compiled: {len(ok)}')
    counts = sorted(r[1] for r in ok)
    if not counts:
        return
    hist = collections.Counter()
    for c in counts:
        hist['2-3' if c <= 3 else '4-10' if c <= 10 else '11-50' if c <= 50 else '51-200' if c <= 200 else '>200'] += 1
    print('root nodes: min', counts[0], 'median', counts[len(counts) // 2], 'max', counts[-1],
          'sum over all programs', sum(counts))
    for k in ['2-3', '4-10', '11-50', '51-200', '>200']:
        print(f'  {k:>7} nodes: {hist[k]} programs')
    withdecl = [r for r in ok if r[3] > 0]
    print(f'programs with a top-level let/var: {len(withdecl)}')
    print('largest roots:')
    for path, n, kinds, decls in sorted(ok, key=lambda r: -r[1])[:12]:
        print(f'  {n:5d} nodes  {decls:3d} top-level let/var  {path}  {kinds}')


if __name__ == '__main__':
    main(sys.argv[1:] or ['examples'])
