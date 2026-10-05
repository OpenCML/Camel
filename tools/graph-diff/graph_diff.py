#!/usr/bin/env python3
"""graph-diff: compare two per-pass graph snapshots.

The snapshots are the readable DOT files written by `camel --dump-after-each-pass <dir>`
(the `std::rgir` format). Nodes are compared as a multiset keyed by
(node type, label, source file, source span): node identities change from pass to pass, the
operator and where it came from do not. The output lists what a pass removed and what it
added, each with its source location, and how many nodes carry a source location at all.

Usage:
    graph_diff.py BEFORE.rgir.dot AFTER.rgir.dot [--json] [--limit N]
    graph_diff.py --coverage SNAPSHOT.rgir.dot [--json]

Exit code: 0 when the two multisets are equal, 1 when they differ, 2 on usage errors.
"""
from __future__ import annotations

import argparse
import collections
import json
import re
import sys
from typing import Dict, Iterable, List, NamedTuple, Optional, Tuple

# A node statement: `N0A [label="...", type="OPER", origin=12, span="8:1-8:9", srcfile="..."];`
# Edge statements contain `->` before the attribute list and are skipped.
_NODE_RE = re.compile(r'^\s*([A-Za-z_][A-Za-z0-9_]*)\s*\[(.*)\];\s*$')
_ATTR_RE = re.compile(r'([A-Za-z_]+)=("((?:[^"\\]|\\.)*)"|[^,\s\]]+)')

# Node types that are dump structure (graph ports, argument packs, exits), not program
# operations; they never carry a source location
# and are left out of the location coverage ratio (they still take part in the diff).
STRUCTURAL_TYPES = frozenset({"EXIT", "PORT", "CLOSURE_PORT", "ARGS"})


class NodeKey(NamedTuple):
    type: str
    label: str
    srcfile: str
    span: str

    def location(self) -> str:
        if not self.span:
            return "<no source location>"
        # Spans are 0-based `line:col-line:col`; print the start 1-based like diagnostics do.
        m = re.match(r'(\d+):(\d+)-(\d+):(\d+)$', self.span)
        if not m:
            return f"{self.srcfile}:{self.span}"
        line, col = int(m.group(1)) + 1, int(m.group(2)) + 1
        return f"{self.srcfile}:{line}:{col}"


def _unescape(text: str) -> str:
    return re.sub(r'\\(.)', lambda m: m.group(1), text)


def parse_nodes(path: str) -> List[NodeKey]:
    nodes: List[NodeKey] = []
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            if "->" in line.split("[", 1)[0]:
                continue
            m = _NODE_RE.match(line)
            if not m:
                continue
            attrs: Dict[str, str] = {}
            for am in _ATTR_RE.finditer(m.group(2)):
                value = am.group(3) if am.group(3) is not None else am.group(2)
                attrs[am.group(1)] = _unescape(value)
            if "type" not in attrs:
                continue
            nodes.append(
                NodeKey(
                    type=attrs["type"],
                    label=attrs.get("label", ""),
                    srcfile=attrs.get("srcfile", ""),
                    span=attrs.get("span", ""),
                )
            )
    return nodes


def coverage(nodes: Iterable[NodeKey]) -> dict:
    by_type: Dict[str, List[int]] = collections.defaultdict(lambda: [0, 0])
    for n in nodes:
        by_type[n.type][1] += 1
        if n.span:
            by_type[n.type][0] += 1
    total = sum(v[1] for v in by_type.values())
    located = sum(v[0] for v in by_type.values())
    op_total = sum(v[1] for t, v in by_type.items() if t not in STRUCTURAL_TYPES)
    op_located = sum(v[0] for t, v in by_type.items() if t not in STRUCTURAL_TYPES)
    return {
        "nodes": total,
        "nodes_with_location": located,
        "operation_nodes": op_total,
        "operation_nodes_with_location": op_located,
        "operation_location_ratio": (op_located / op_total) if op_total else None,
        "by_type": {t: {"with_location": v[0], "total": v[1]} for t, v in sorted(by_type.items())},
    }


def diff(before: List[NodeKey], after: List[NodeKey]) -> Tuple[List[Tuple[NodeKey, int]], List[Tuple[NodeKey, int]]]:
    cb, ca = collections.Counter(before), collections.Counter(after)
    removed = sorted(((k, n) for k, n in (cb - ca).items()), key=lambda kv: (kv[0].srcfile, kv[0].span, kv[0].type, kv[0].label))
    added = sorted(((k, n) for k, n in (ca - cb).items()), key=lambda kv: (kv[0].srcfile, kv[0].span, kv[0].type, kv[0].label))
    return removed, added


def _entry(key: NodeKey, count: int) -> dict:
    return {"type": key.type, "label": key.label, "srcfile": key.srcfile, "span": key.span,
            "location": key.location(), "count": count}


def main(argv: Optional[List[str]] = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    ap.add_argument("snapshots", nargs="+", help="BEFORE AFTER, or one snapshot with --coverage")
    ap.add_argument("--coverage", action="store_true", help="only report source-location coverage")
    ap.add_argument("--json", action="store_true", help="machine-readable output")
    ap.add_argument("--limit", type=int, default=0, help="print at most N entries per section (text mode)")
    args = ap.parse_args(argv)

    if args.coverage:
        if len(args.snapshots) != 1:
            ap.error("--coverage takes exactly one snapshot")
        cov = coverage(parse_nodes(args.snapshots[0]))
        if args.json:
            print(json.dumps(cov, indent=1))
        else:
            ratio = cov["operation_location_ratio"]
            print(f"nodes: {cov['nodes']}  with location: {cov['nodes_with_location']}")
            print(f"operation nodes (excluding {', '.join(sorted(STRUCTURAL_TYPES))}): {cov['operation_nodes']}  "
                  f"with location: {cov['operation_nodes_with_location']}"
                  + (f"  ({ratio:.1%})" if ratio is not None else ""))
            for t, v in cov["by_type"].items():
                print(f"  {t:6} {v['with_location']:6} / {v['total']}")
        return 0

    if len(args.snapshots) != 2:
        ap.error("expected BEFORE and AFTER snapshots")
    before, after = parse_nodes(args.snapshots[0]), parse_nodes(args.snapshots[1])
    removed, added = diff(before, after)
    if args.json:
        print(json.dumps({
            "before": args.snapshots[0], "after": args.snapshots[1],
            "removed": [_entry(k, n) for k, n in removed],
            "added": [_entry(k, n) for k, n in added],
            "removed_nodes": sum(n for _, n in removed),
            "added_nodes": sum(n for _, n in added),
            "coverage_before": coverage(before), "coverage_after": coverage(after),
        }, indent=1))
    else:
        for title, sign, entries in (("removed", "-", removed), ("added", "+", added)):
            print(f"{title}: {sum(n for _, n in entries)} node(s), {len(entries)} distinct")
            shown = entries[: args.limit] if args.limit else entries
            for key, count in shown:
                times = f" x{count}" if count > 1 else ""
                print(f"  {sign} {key.type:5} {key.label}{times}  @ {key.location()}")
            if len(shown) < len(entries):
                print(f"  ... {len(entries) - len(shown)} more")
    return 1 if (removed or added) else 0


if __name__ == "__main__":
    sys.exit(main())
