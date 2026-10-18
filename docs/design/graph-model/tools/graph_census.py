#!/usr/bin/env python3
"""Census of the graphs the compiler builds for the programs under the given directories.

    python3 docs/design/graph-model/tools/graph_census.py [--passes "std::opt::inline"] [--json out.json] DIR...

For every .cml file that declares `main`, runs `camel <file> [passes] std::rgir` with
CAMEL_GC_STATS=1 and reads

  * the DOT output: graphs (clusters), nodes with their kind and label, edges with their kind;
  * the `[camel] spaces:` line on stderr: bytes and allocations of the graph, permanent (types)
    and meta (compile-time constants) spaces.

It prints, over all programs that compile: the distribution of node and edge kinds, nodes and
edges per graph, graphs per program, how deep graphs are nested in the dump, how many inputs
and users a node has, what share of the graphs are branch arms and how small they are, how
often the same operator name / port name / constant label is repeated, and bytes per node.

Nothing here changes the compiler; the numbers are read from its existing dumps.
"""
import argparse
import collections
import concurrent.futures
import json
import os
import re
import subprocess
import sys

NODE = re.compile(r'^\s*(N[0-9A-F]+) \[label="(.*?)", type="([A-Z_]+)"')
EDGE = re.compile(r'^\s*([A-Z][0-9A-F]+) -> ([A-Z][0-9A-F]+) \[label="(.*?)", type="([A-Z_]+)"')
CLUSTER = re.compile(r'^\s*subgraph cluster_(F[0-9A-F]+) \{')
LABEL = re.compile(r'^\s*label="(.*)";')
SPACES = re.compile(r'(\w+)=([0-9.]+)')
MAIN = re.compile(r'^\s*(export\s+)?(sync\s+)?func\s+main\s*\(', re.M)


def parse_dot(text):
    """Returns the list of graphs: dict(name, depth, nodes{id: (kind, label)}, edges[(src, dst, kind)])."""
    graphs = []
    stack = []
    root = dict(name='__root__', depth=0, nodes={}, edges=[])
    graphs.append(root)
    owner = {}
    for line in text.splitlines():
        m = CLUSTER.match(line)
        if m:
            g = dict(name=None, depth=len(stack) + 1, nodes={}, edges=[])
            graphs.append(g)
            stack.append(g)
            continue
        if line.strip() == '}' and stack:
            stack.pop()
            continue
        cur = stack[-1] if stack else root
        m = LABEL.match(line)
        if m and stack and cur['name'] is None:
            cur['name'] = m.group(1)
            continue
        m = NODE.match(line)
        if m:
            kind = m.group(3)
            cur['nodes'][m.group(1)] = (kind, m.group(2))
            owner[m.group(1)] = cur
            continue
        m = EDGE.match(line)
        if m and m.group(4) in ('NORM', 'WITH', 'CTRL'):
            cur['edges'].append((m.group(1), m.group(2), m.group(4)))
    return graphs


def measure(args):
    path, passes = args
    try:
        text = open(path, encoding='utf-8', errors='replace').read()
    except OSError:
        return None
    if not MAIN.search(text):
        return None
    env = dict(os.environ, CAMEL_GC_STATS='1')
    try:
        out = subprocess.run(['camel', path] + passes + ['std::rgir'], capture_output=True,
                             text=True, timeout=120, env=env, stdin=subprocess.DEVNULL)
    except subprocess.TimeoutExpired:
        return dict(path=path, ok=False)
    if out.returncode != 0 or 'digraph' not in out.stdout:
        return dict(path=path, ok=False)
    spaces = {}
    for line in out.stderr.splitlines():
        if line.startswith('[camel] spaces:'):
            spaces = {k: float(v) for k, v in SPACES.findall(line)}
    return dict(path=path, ok=True, graphs=parse_dot(out.stdout), spaces=spaces,
                source_bytes=len(text.encode('utf-8')))


def pct(values, p):
    if not values:
        return 0
    s = sorted(values)
    return s[min(len(s) - 1, int(round(p / 100.0 * (len(s) - 1))))]


def dist(values):
    if not values:
        return 'n=0'
    return (f'n={len(values)} min={min(values)} p50={pct(values, 50)} p90={pct(values, 90)} '
            f'p99={pct(values, 99)} max={max(values)} mean={sum(values) / len(values):.1f}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--passes', default='')
    ap.add_argument('--json', default='')
    ap.add_argument('--jobs', type=int, default=4)
    ap.add_argument('dirs', nargs='+')
    a = ap.parse_args()
    passes = a.passes.split()

    files = []
    for root in a.dirs:
        if os.path.isfile(root):
            files.append(root)
            continue
        for d, _, names in os.walk(root):
            files += [os.path.join(d, n) for n in names if n.endswith('.cml')]
    files.sort()
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:
        results = [r for r in pool.map(measure, [(f, passes) for f in files]) if r is not None]
    ok = [r for r in results if r['ok']]
    print(f'files: {len(files)}  with main: {len(results)}  compiled: {len(ok)}  passes: {passes or "none"}')
    if not ok:
        return

    kinds = collections.Counter()
    edge_kinds = collections.Counter()
    nodes_per_graph, edges_per_graph, graphs_per_program, nodes_per_program = [], [], [], []
    depths = []
    fan_in, fan_out = [], []
    arm_graphs = 0
    arm_sizes, non_arm_sizes = [], []
    oper_total, oper_distinct_per_program = 0, 0
    oper_global = collections.Counter()
    port_names = collections.Counter()
    const_dups_total, const_total = 0, 0
    bytes_graph = bytes_perm = bytes_meta = 0.0
    allocs_graph = allocs_perm = allocs_meta = 0.0
    total_nodes = total_edges = total_graphs = 0
    exec_nodes = 0
    biggest = []

    for r in ok:
        gs = r['graphs']
        graphs_per_program.append(len(gs))
        prog_nodes = 0
        prog_opers = set()
        # Branch arms: graphs called by a FUNC node that a BRCH node selects (CTRL edge BRCH -> FUNC).
        arm_names = set()
        for g in gs:
            for src, dst, kind in g['edges']:
                if kind == 'CTRL' and g['nodes'].get(src, ('', ''))[0] == 'BRCH' and \
                        g['nodes'].get(dst, ('', ''))[0] == 'FUNC':
                    arm_names.add(g['nodes'][dst][1])
        for g in gs:
            n = len(g['nodes'])
            e = len(g['edges'])
            nodes_per_graph.append(n)
            edges_per_graph.append(e)
            depths.append(g['depth'])
            prog_nodes += n
            total_edges += e
            biggest.append((n, e, g['name'], r['path']))
            ins = collections.Counter()
            outs = collections.Counter()
            for src, dst, kind in g['edges']:
                edge_kinds[kind] += 1
                ins[dst] += 1
                outs[src] += 1
            consts = collections.Counter()
            for nid, (kind, label) in g['nodes'].items():
                kinds[kind] += 1
                fan_in.append(ins.get(nid, 0))
                fan_out.append(outs.get(nid, 0))
                if kind == 'OPER':
                    oper_total += 1
                    prog_opers.add(label)
                    oper_global[label] += 1
                elif kind in ('PORT', 'CLOSURE_PORT'):
                    port_names[label] += 1
                elif kind == 'DATA':
                    consts[label] += 1
                if kind not in ('DATA', 'PORT', 'CLOSURE_PORT'):
                    exec_nodes += 1
            const_total += sum(consts.values())
            const_dups_total += sum(c - 1 for c in consts.values())
            if g['name'] in arm_names:
                arm_graphs += 1
                arm_sizes.append(n)
            elif g['name'] != '__root__':
                non_arm_sizes.append(n)
        nodes_per_program.append(prog_nodes)
        oper_distinct_per_program += len(prog_opers)
        total_nodes += prog_nodes
        total_graphs += len(gs)
        s = r['spaces']
        bytes_graph += s.get('graph_used_kb', 0) * 1024
        bytes_perm += s.get('perm_used_kb', 0) * 1024
        bytes_meta += s.get('meta_peak_kb', 0) * 1024
        allocs_graph += s.get('graph_allocs', 0)
        allocs_perm += s.get('perm_allocs', 0)
        allocs_meta += s.get('meta_allocs', 0)

    print(f'totals: programs={len(ok)} graphs={total_graphs} nodes={total_nodes} edges={total_edges}')
    # What the compiler allocates in its spaces before it has compiled anything of the program
    # (the types of the built-in operators): measured on the smallest program and taken off.
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        empty = os.path.join(tmp, 'empty.cml')
        with open(empty, 'w') as f:
            f.write('func main(): int {\n    return 0\n}\n')
        base = measure((empty, passes))
    b = base['spaces'] if base and base['ok'] else {}
    base_graphs = len(base['graphs']) if base and base['ok'] else 0
    print(f'baseline (empty main): graphs={base_graphs} graph_allocs={b.get("graph_allocs", 0):.0f} '
          f'perm_used_kb={b.get("perm_used_kb", 0):.0f} perm_allocs={b.get("perm_allocs", 0):.0f} '
          f'meta_peak_kb={b.get("meta_peak_kb", 0):.0f}')
    net_graphs = max(1, total_graphs - base_graphs * len(ok))
    net_perm = bytes_perm - b.get('perm_used_kb', 0) * 1024 * len(ok)
    net_perm_allocs = allocs_perm - b.get('perm_allocs', 0) * len(ok)
    print('node kinds:')
    for k, c in kinds.most_common():
        print(f'  {k:13s} {c:8d}  {100.0 * c / total_nodes:5.1f}%')
    print('edge kinds:')
    for k, c in edge_kinds.most_common():
        print(f'  {k:13s} {c:8d}  {100.0 * c / total_edges:5.1f}%')
    print(f'executable nodes (not DATA/PORT): {exec_nodes} ({100.0 * exec_nodes / total_nodes:.1f}%)')
    print('nodes per graph:      ', dist(nodes_per_graph))
    print('edges per graph:      ', dist(edges_per_graph))
    print('graphs per program:   ', dist(graphs_per_program))
    print('nodes per program:    ', dist(nodes_per_program))
    print('nesting depth in dump:', dist(depths))
    print('inputs per node:      ', dist(fan_in))
    print('users per node:       ', dist(fan_out))
    for limit in (0, 1, 2, 3, 4, 8):
        share = 100.0 * sum(1 for v in fan_in if v <= limit) / len(fan_in)
        print(f'  nodes with <= {limit} inputs: {share:5.1f}%')
    for limit in (0, 1, 2, 4):
        share = 100.0 * sum(1 for v in fan_out if v <= limit) / len(fan_out)
        print(f'  nodes with <= {limit} users:  {share:5.1f}%')
    small = [1, 2, 4, 8, 16, 32, 64, 256, 1024, 65535]
    lo = 0
    for hi in small:
        c = sum(1 for v in nodes_per_graph if lo < v <= hi)
        if c:
            print(f'  graphs with {lo + 1:>5}..{hi:<5} nodes: {c:7d}  {100.0 * c / len(nodes_per_graph):5.1f}%')
        lo = hi
    print(f'branch-arm graphs: {arm_graphs} of {total_graphs} graphs ({100.0 * arm_graphs / total_graphs:.1f}%)')
    print('  nodes per arm graph:     ', dist(arm_sizes))
    print('  nodes per other function:', dist(non_arm_sizes))
    print(f'  arm graphs with <= 2 nodes: {100.0 * sum(1 for v in arm_sizes if v <= 2) / max(1, len(arm_sizes)):.1f}%')
    print(f'OPER nodes: {oper_total}; distinct operator names per program summed: {oper_distinct_per_program} '
          f'({oper_total / max(1, oper_distinct_per_program):.1f} uses per name); distinct over the corpus: {len(oper_global)}')
    print('  most used operators:', ', '.join(f'{k} {v}' for k, v in oper_global.most_common(12)))
    print(f'port nodes: {sum(port_names.values())}; distinct labels: {len(port_names)}; unnamed (label PORT): {port_names.get("PORT", 0)}')
    print(f'DATA nodes: {const_total}; repeats of a label already present in the same graph: {const_dups_total} '
          f'({100.0 * const_dups_total / max(1, const_total):.1f}%)')
    print('memory (sum over programs, as reported by CAMEL_GC_STATS):')
    print(f'  graph space: {bytes_graph / 1024:.0f} KB, {allocs_graph:.0f} allocations; '
          f'{bytes_graph / total_nodes:.1f} bytes per node, {bytes_graph / total_graphs:.0f} bytes per graph')
    print(f'  perm space (types): {bytes_perm / 1024:.0f} KB, {allocs_perm:.0f} allocations; '
          f'above the baseline: {net_perm / 1024:.0f} KB, {net_perm_allocs:.0f} allocations, '
          f'{net_perm / net_graphs:.0f} bytes and {net_perm_allocs / net_graphs:.1f} allocations per graph, '
          f'{net_perm / total_nodes:.1f} bytes per node')
    print(f'  meta space (peak): {bytes_meta / 1024:.0f} KB, {allocs_meta:.0f} allocations; '
          f'{bytes_meta / total_graphs:.0f} bytes per graph')
    print('largest graphs:')
    for n, e, name, path in sorted(biggest, reverse=True)[:10]:
        print(f'  {n:6d} nodes {e:6d} edges  {name}  {path}')
    if a.json:
        with open(a.json, 'w') as f:
            json.dump(dict(programs=len(ok), graphs=total_graphs, nodes=total_nodes, edges=total_edges,
                           kinds=dict(kinds), edge_kinds=dict(edge_kinds)), f, indent=1)


if __name__ == '__main__':
    main()
