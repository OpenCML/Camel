#!/usr/bin/env python3
# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""same_graph: do two programs have one graph?

**The yardstick is the project's canonical form, not one invented here** (author's ruling,
`812afad5c`: "same_graph compares canonical forms, the one the block naming uses"). That form is
`canon(P)`: the canonical JSON GIR document, `std::jgir canonical=true` (version 2), whose node
order is `camel::runtime::CanonicalProgram` -- "the canonical order of the nodes of a frozen graph,
derived from the graph alone" (`include/camel/runtime/canonical.h`, design principles 9). The
document drops what a program does not have (node, region and type numbers, names, source origins
and spans) and keeps what equivalence is defined on, and `src/passes/trans/cml/cml.cpp` states the
same division: "a name is for a reader -- the canonical document compares the graphs".

So the verdict is the canonical form itself: **the two documents are one, byte for byte**. Nothing
is normalised here that the dump has not already normalised.

Measured, because the opposite is a plausible reading and it decides whether byte equality is too
strict: two writings of one body that swap two *pure* statements give the same document -- 2107
bytes on both sides, byte-identical (`test/cases/trans/same_graph_writings.py`,
`both_writings_are_one_canonical_form`). The numbering is therefore order-independent, and a
difference in the bytes is a difference in the graph and not in the order the lines were written.
That is why this tool needs no renumbering rule of its own.

What it adds to a byte comparison is the half a verdict alone does not give: **what differs, in
terms of nodes and edges.** A document that says only "not the same bytes at offset 1279" sends a
reader to a JSON line number; when the answer is no, this prints the nodes and the edges that one
side has and the other does not, with ids, spans and origins erased -- aligned by *structure*, a
node's own label together with the labelled edges around it, refined to a fixed point -- so that a
reader is told `the write is gone` and not `n2 differs from n2`. That reading is the explanation
and never the verdict: the verdict stays the canonical form, so this tool cannot be more permissive
than the project's own definition of equivalence.

Usage:

    same_graph.py --camel <path-to-camel> LEFT.cml RIGHT.cml [--swapped "9,10"]
    same_graph.py LEFT.json RIGHT.json [--swapped ...]     # canonical documents already dumped

Output:

    same_graph a_println_then_write b_write_then_println = true
    swapped: lines 9,10
    nodes=15 edges=12
    canonical: 6591 bytes, sha256=c0ffee... (both sides)

Exit status: 0 one graph, 1 not (the difference is printed), 2 a side could not be read or
compiled, 3 over the comparison budget.
"""

import argparse
import collections
import hashlib
import json
import os
import subprocess
import sys
from typing import Dict, List, NamedTuple, Optional, Tuple

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

# The structural reader, reused and not remade: it is the document-as-graph view that names what
# differs. Its verdict is not used -- the canonical form is the verdict (see the module docstring).
import cml_roundtrip as rt  # noqa: E402


# -- reading a program --------------------------------------------------------------------


class Unreadable(Exception):
    """A program could not be compiled or a document could not be read."""


def document_of(path: str, camel: Optional[str]) -> Tuple[bytes, dict]:
    """The canonical form of `path` as its exact bytes, and the same document parsed."""
    if path.endswith(".json"):
        try:
            with open(path, "rb") as handle:
                raw = handle.read()
        except OSError as error:
            raise Unreadable(f"{path}: {error.strerror}") from error
    else:
        if camel is None:
            raise Unreadable(f"{path}: a .cml program needs --camel <path-to-camel>")
        run = subprocess.run(
            [camel, path, "std::jgir", "canonical=true", "std::nop"],
            capture_output=True,
        )
        if run.returncode != 0:
            text = (run.stdout or b"").decode("utf-8", "replace")
            first = (text.strip().splitlines() or [""])[0]
            raise Unreadable(f"{path}: camel exited {run.returncode}: {first}")
        raw = run.stdout or b""
        if not raw.strip():
            raise Unreadable(f"{path}: camel wrote no document")
    try:
        return raw, json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise Unreadable(f"{path}: not JSON ({error})") from error


def first_difference(left: bytes, right: bytes) -> int:
    """The index of the first byte the two disagree on, or -1; a prefix differs at its own end."""
    for index in range(min(len(left), len(right))):
        if left[index] != right[index]:
            return index
    return -1 if len(left) == len(right) else min(len(left), len(right))


def line_and_column(raw: bytes, offset: int) -> Tuple[int, int]:
    before = raw[:offset].decode("utf-8", "replace").split("\n")
    return len(before), len(before[-1]) + 1


# -- describing one vertex, for the report ------------------------------------------------


def type_text(doc, index) -> str:
    entry = doc.pretty.get(f"T:{index}") if isinstance(index, int) else None
    if not isinstance(entry, dict):
        return "?"
    kind = entry.get("k")
    if kind == "other":
        return entry.get("spelling", "other")
    if kind == "array":
        return type_text(doc, entry.get("elem")) + "[]"
    if kind == "tuple":
        return "(" + ", ".join(type_text(doc, item) for item in entry.get("elems") or []) + ")"
    if kind == "struct":
        fields = ", ".join(
            f"{field.get('name')}: {type_text(doc, field.get('type'))}"
            for field in entry.get("fields") or []
        )
        return "{" + fields + "}"
    if kind == "func":
        return "func"
    if kind == "enum":
        return "enum"
    if kind in ("map", "set"):
        return f"{kind}<{type_text(doc, entry.get('key'))}>"
    return str(kind)


def region_text(doc, graph_id, region) -> str:
    if not region:
        return "body"
    entry = doc.pretty.get(f"R:{graph_id}:{region}")
    if not isinstance(entry, dict):
        return f"region {region}"
    return f"{entry.get('kind')} region"


def value_text(value) -> str:
    if not isinstance(value, dict):
        return json.dumps(value, sort_keys=True)
    kind = value.get("k")
    if kind in ("i32", "i64"):
        return f"{value.get('v')}"
    if kind == "string":
        text = bytes(value.get("bytes") or []).decode("utf-8", "replace")
        return json.dumps(text if len(text) <= 24 else text[:21] + "...")
    if kind == "bool":
        return str(value.get("v")).lower()
    text = json.dumps(value, sort_keys=True)
    return text if len(text) <= 40 else text[:37] + "..."


def node_desc(doc, vid) -> str:
    """What a node is, in the words of the program: kind, operator, value, type, region."""
    pretty = doc.pretty.get(vid)
    if not isinstance(pretty, dict):
        return vid
    parts = [str(pretty.get("kind"))]
    payload = pretty.get("payload") or {}
    operator = payload.get("op")
    if isinstance(operator, str):
        parts.append(operator)
    if pretty.get("kind") == "DATA" and "value" in pretty:
        parts.append(value_text(pretty["value"]))
    if "callee" in pretty:
        parts.append("callee=<graph>")
    if "param" in pretty:
        param = pretty["param"]
        parts.append(f"param {param.get('kind')}#{param.get('index')}")
    if "loop" in pretty:
        parts.append(f"loop {pretty['loop'].get('kind')}")
    graph_id = vid.split(":")[1] if vid.count(":") >= 2 else "?"
    return (
        f"{' '.join(parts)} : {type_text(doc, pretty.get('type'))} "
        f"@ {region_text(doc, graph_id, pretty.get('region'))}"
    )


def raw_ordering_kinds(doc, source_vid, target_vid) -> List[str]:
    """The edge kinds the document spells between two nodes, before they are read as one order."""
    node = doc.pretty.get(source_vid) or {}
    local = target_vid.split(":")[-1]
    kinds = {
        item.get("kind")
        for item in node.get("inputs") or []
        if item.get("source") == local and item.get("kind") in rt.ORDERING_KINDS
    }
    return sorted(kind for kind in kinds if kind)


def edge_desc(doc, source_vid, label, target_vid) -> str:
    # The comparison reads control, sync and order as one relation -- all three say "the
    # predecessor runs first" -- but the report names the kinds actually written, because that is
    # what a reader has to go and change.
    if label == "in:ordering":
        label = f"in:ordering({'+'.join(raw_ordering_kinds(doc, source_vid, target_vid))})"
    return f"{node_desc(doc, source_vid)} --{label}--> {node_desc(doc, target_vid)}"


# -- the node and edge relation, for the explanation --------------------------------------


class Relation(NamedTuple):
    nodes: int
    edges: int
    node_classes: Dict[str, int]
    edge_classes: Dict[Tuple[str, str, str], int]


def relation(doc, colors) -> Relation:
    """A document as a node multiset and an edge multiset, both keyed by structural colour."""
    node_vids = [vid for vid, label in doc.V.items() if label.get("k") == "node"]
    node_classes = collections.Counter(colors[vid] for vid in node_vids)
    edge_classes: collections.Counter = collections.Counter()
    for source in node_vids:
        for label, target in doc.out[source]:
            if target.startswith("N:"):
                edge_classes[(colors[source], label, colors[target])] += 1
    return Relation(
        nodes=len(node_vids),
        edges=sum(edge_classes.values()),
        node_classes=dict(node_classes),
        edge_classes=dict(edge_classes),
    )


def representative(vids, colors, color) -> Optional[str]:
    for vid in vids:
        if colors[vid] == color:
            return vid
    return None


def symmetric_difference(left, right, lcolors, rcolors, limit: int) -> List[str]:
    """The node classes and edge triples one side has and the other does not."""
    lrel, rrel = relation(left, lcolors), relation(right, rcolors)
    lines: List[str] = []
    left_nodes = [vid for vid, label in left.V.items() if label.get("k") == "node"]
    right_nodes = [vid for vid, label in right.V.items() if label.get("k") == "node"]

    moved = []
    for color in sorted(set(lrel.node_classes) | set(rrel.node_classes)):
        a, b = lrel.node_classes.get(color, 0), rrel.node_classes.get(color, 0)
        if a != b:
            moved.append((abs(a - b), color, a, b))
    moved.sort(reverse=True)
    lines.append(
        f"  nodes only in {os.path.basename(left.name)} "
        f"({sum(1 for _, _, a, b in moved if a > b)}) and only in "
        f"{os.path.basename(right.name)} ({sum(1 for _, _, a, b in moved if b > a)}):"
    )
    if not moved:
        lines.append("    none: the two node sets are one")
    for extra, color, a, b in moved[:limit]:
        if a > b:
            lines.append(f"    - {extra} x {node_desc(left, representative(left_nodes, lcolors, color))}")
        else:
            lines.append(
                f"    + {extra} x {node_desc(right, representative(right_nodes, rcolors, color))}"
            )
    if len(moved) > limit:
        lines.append(f"    ... {len(moved) - limit} further node class(es) moved")

    edge_moved = []
    for key in set(lrel.edge_classes) | set(rrel.edge_classes):
        a, b = lrel.edge_classes.get(key, 0), rrel.edge_classes.get(key, 0)
        if a != b:
            edge_moved.append((abs(a - b), key, a, b))
    edge_moved.sort(reverse=True)
    lines.append(
        f"  edges only in {os.path.basename(left.name)} "
        f"({sum(1 for _, _, a, b in edge_moved if a > b)}) and only in "
        f"{os.path.basename(right.name)} ({sum(1 for _, _, a, b in edge_moved if b > a)}):"
    )
    if not edge_moved:
        lines.append("    none: the two edge relations are one")
    for extra, key, a, b in edge_moved[:limit]:
        source_color, label, target_color = key
        side, colors, nodes = (left, lcolors, left_nodes) if a > b else (right, rcolors, right_nodes)
        source_vid = representative(nodes, colors, source_color)
        target_vid = representative(nodes, colors, target_color)
        sign = "-" if a > b else "+"
        if source_vid is None or target_vid is None:
            lines.append(f"    {sign} {extra} x {label} (an end is outside the node set)")
        else:
            lines.append(f"    {sign} {extra} x {edge_desc(side, source_vid, label, target_vid)}")
    if len(edge_moved) > limit:
        lines.append(f"    ... {len(edge_moved) - limit} further edge class(es) moved")
    return lines


# -- entry point ---------------------------------------------------------------------------


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description="do two programs have one canonical form, and if not what differs"
    )
    parser.add_argument("left")
    parser.add_argument("right")
    parser.add_argument("--camel", default=None, help="the compiler used for .cml inputs")
    parser.add_argument(
        "--swapped",
        default=None,
        help="what the two writings differ by, printed as given (e.g. '9,10')",
    )
    parser.add_argument("--limit", type=int, default=12, help="at most N entries per section")
    parser.add_argument("--max-nodes", type=int, default=rt.DEFAULT_MAX_NODES)
    parser.add_argument("--json", action="store_true", help="machine-readable output")
    args = parser.parse_args(argv)

    try:
        left_raw, left_json = document_of(args.left, args.camel)
        right_raw, right_json = document_of(args.right, args.camel)
    except Unreadable as error:
        print(f"same_graph {args.left} {args.right} = unreadable: {error}")
        return 2

    # The verdict: the canonical form, whole, byte for byte (module docstring).
    same = left_raw == right_raw
    digest = hashlib.sha256(left_raw).hexdigest()[:16]

    left = right = None
    lcolors = rcolors = None
    lrel = rrel = None
    try:
        left = rt.Document(left_json, args.left, args.max_nodes)
        right = rt.Document(right_json, args.right, args.max_nodes)
        lcolors, rcolors = rt.refine([left, right])
        lrel, rrel = relation(left, lcolors), relation(right, rcolors)
    except (rt.Invalid, rt.Budget):
        # The documents are not canonical GIR this reader understands; the byte verdict stands and
        # there is nothing structural to say about it.
        pass

    if args.json:
        print(
            json.dumps(
                {
                    "left": args.left,
                    "right": args.right,
                    "same": bool(same),
                    "canonical": {
                        "left_bytes": len(left_raw),
                        "right_bytes": len(right_raw),
                        "sha256": digest,
                    },
                    "nodes": {"left": getattr(lrel, "nodes", None), "right": getattr(rrel, "nodes", None)},
                    "edges": {"left": getattr(lrel, "edges", None), "right": getattr(rrel, "edges", None)},
                    "swapped": args.swapped,
                },
                indent=1,
            )
        )
        return 0 if same else 1

    print(f"same_graph {args.left} {args.right} = {'true' if same else 'false'}")
    if args.swapped:
        print(f"swapped: lines {args.swapped}")
    if lrel is not None and rrel is not None and (lrel.nodes != rrel.nodes or lrel.edges != rrel.edges):
        print(f"nodes={lrel.nodes} edges={lrel.edges}   ({os.path.basename(args.left)})")
        print(f"nodes={rrel.nodes} edges={rrel.edges}   ({os.path.basename(args.right)})")
    elif lrel is not None:
        print(f"nodes={lrel.nodes} edges={lrel.edges}")
    if same:
        print(f"canonical: {len(left_raw)} bytes, sha256={digest} (both sides)")
        return 0

    offset = first_difference(left_raw, right_raw)
    if offset < 0:
        line, column = line_and_column(left_raw, 0)
        print(f"canonical: {len(left_raw)} bytes vs {len(right_raw)} bytes")
    else:
        line, column = line_and_column(left_raw, offset)
        print(
            f"canonical: first difference at byte {offset + 1} (line {line}, column {column}); "
            f"{len(left_raw)} bytes vs {len(right_raw)} bytes"
        )

    if lrel is None or rrel is None:
        return 1
    if lrel.node_classes == rrel.node_classes and lrel.edge_classes == rrel.edge_classes:
        # The nodes and the edges are one and the canonical forms still differ: what moved is
        # elsewhere in the document (a region's ports, a type's fields, a tag, the tables).
        print("the node and edge relation is one; the difference is outside it")
        return 1
    print("the node and edge relation is not one")
    print("symmetric difference")
    for line_text in symmetric_difference(left, right, lcolors, rcolors, args.limit):
        print(line_text)
    return 1


if __name__ == "__main__":
    sys.exit(main())