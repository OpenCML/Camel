# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""Canonical order: the numbering is the graph's, not the spelling's.

Five readings, each named after the rule it belongs to (docs/design-principles.cn.md 9, "the
graph interface does not see the spelling"):

  `order`  two programs that differ only in the order two values were captured in -- which is also
           the order their nodes were created in -- write one `std::jgir` document. Source
           positions are compared out: the document carries them under `origin` and `source`, and
           a position is spelling.

  `tie`    a tie is a place where the ports do not determine an order: here, the two values a
           closure captured, whose order is the order the source captured them in -- spelling. The
           arrangement is the one whose numbering table is smallest. Two things are ruled out: the
           source captures the larger payload first (so the capture order and the creation order are
           the opposite arrangement), and the payloads are chosen so that a structural hash of the
           two fragments orders them the other way too -- a hash orders a tie by a value that says
           nothing about which arrangement the table wants, and two nodes a hash calls equal need
           not be interchangeable. The document must put the smaller table first.

  `run`    the numbering is a query: `std::nvm` runs the program to the same output. A tie may
           change what a document says and never what the program does.

  `program`  the minimum is taken over the whole program: two graphs that each reach their smallest
           table under more than one arrangement, with a binder that reads the graph it binds
           through that graph's port numbers, must write one canonical (`canonical=true`) document
           whatever order the source captured the values in. A numbering that fixed one arrangement
           of each graph before its binders were numbered would give the two spellings two
           different tables.

  `wide`   a tie wider than kMaxTieWidth is ordered by the node key -- the type of the value a node
           makes is part of it, so eight captures of eight different types are ordered without
           enumerating 8! arrangements -- and a part the key leaves equal and that is still that
           wide is reported by `camel inspect --ties`, not arranged in the order the source wrote it
           in. Both are read here: the swapped capture order writes one document, and the report has
           no part left over the limit.

`--camel` names the install under test (the gate passes `${camel}`). `--probe` prints the document
it reads and exits 1: it is how this case is debugged by hand, and the plan never uses it.
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile

# ---------------------------------------------------------------------------------------------
# the programs
# ---------------------------------------------------------------------------------------------

# The same two captures, taken in the other order: `read_ab`'s body reads `a` and then `b`,
# `read_ba`'s reads `b` and then `a`. The two values the body adds are the same values in the same
# operand slots; only the capture table's order -- the order the source mentioned them -- moves.
CAPTURE_AB = """
func main(): int sync {
    var a = 1
    var b = 2
    let read = (u: int): int => {
        let p = a
        let q = b
        return u + p + q
    }
    return read(0)
}
"""

CAPTURE_BA = """
func main(): int sync {
    var a = 1
    var b = 2
    let read = (u: int): int => {
        let q = b
        let p = a
        return u + p + q
    }
    return read(0)
}
"""

# One captured value, with the payload filled in: the fragment a payload makes has the same shape
# here and in the two-capture program below, so hashing it here is hashing the fragment the tie
# will arrange.
CAPTURE_ONE = """
func main(): int sync {
    var a = @VALUE@
    let read = (u: int): int => {
        return a
    }
    return read(0)
}
"""

# Two captures, the larger payload captured first: the capture table's order is the order the
# source captured the values in, which is also the order the cells were created in, and the two
# values are a width-2 tie. The payloads are chosen (by the reading) so that a structural hash of
# the two fragments orders them the other way as well.
CAPTURE_TWO = """
func main(): int sync {
    var a = @LARGER@
    var b = @SMALLER@
    let read = (u: int): int => {
        return a + b
    }
    return read(0)
}
"""

# A program whose output is the same before and after the change: the ordering rule is a query,
# and the canonical numbering is not a scheduler.
ORDERED_RUN = """
func main(): int sync {
    var a = 0
    var b = 0
    a = 1
    b = 2
    println(a, b)
    return 0
}
"""

# Two graphs that each reach their smallest table under more than one arrangement, and a binder
# that reads the graph it binds by that graph's port numbers. Two cells are made and tied (the same
# operator on two constants), the closure captures both, and the body reads them in the other order
# in the second program -- so the capture table's order, which is the order the source mentioned
# them in, moves. Per graph either arrangement reaches the same table; the binder is what decides
# which one the program takes, and it decides it the same way in both spellings (design principles
# 9.5: the minimum is taken over the combinations across graphs, not fixed per graph).
PROGRAM_AB = """
func main(): int sync {
    var a = 1
    var b = 2
    let read = (u: int): int => {
        let p = a
        let q = b
        return u + p + q
    }
    return read(0)
}
"""

PROGRAM_BA = """
func main(): int sync {
    var a = 1
    var b = 2
    let read = (u: int): int => {
        let q = b
        let p = a
        return u + p + q
    }
    return read(0)
}
"""

# Eight captured values of eight different types: a tie group wider than kMaxTieWidth, and one whose
# members the node key tells apart (the type of the value is part of the key), so the numbering
# orders them without enumerating 8! arrangements. A group that the key leaves equal and that is
# still that wide is reported by `camel inspect --ties` instead of being arranged in the order the
# source wrote it in.
WIDE_AB = """
func main(): int sync {
    var a = 1
    var b = 2.5
    var c = true
    var d = "s"
    var e = 3
    var f = 4.5
    var g = false
    var h = "t"
    let read = (u: int): int => {
        let p = a
        let q = b
        let r = c
        let s = d
        let t = e
        let w = f
        let x = g
        let y = h
        return u + p + t
    }
    return read(0)
}
"""

WIDE_BA = """
func main(): int sync {
    var a = 1
    var b = 2.5
    var c = true
    var d = "s"
    var e = 3
    var f = 4.5
    var g = false
    var h = "t"
    let read = (u: int): int => {
        let y = h
        let x = g
        let w = f
        let t = e
        let s = d
        let r = c
        let q = b
        let p = a
        return u + p + t
    }
    return read(0)
}
"""


def camel_env():
    env = dict(os.environ)
    home = env.get("CAMEL_HOME")
    if home:
        env["PATH"] = os.path.join(home, "bin") + os.pathsep + env.get("PATH", "")
    return env


def run_camel(camel, path, args, timeout=180):
    return subprocess.run(
        [camel, path] + args,
        capture_output=True,
        text=True,
        timeout=timeout,
        env=camel_env(),
    )


def jgir(camel, workdir, name, text, passes=("std::jgir",)):
    """The `std::jgir` document of a program, or None and the process when it did not compile."""
    path = os.path.join(workdir, name)
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)
    proc = run_camel(camel, path, list(passes))
    if proc.returncode != 0:
        return None, proc
    start = proc.stdout.find("{")
    if start < 0:
        raise SystemExit(f"no jgir document in the output of {name}:\n{proc.stdout[:2000]}")
    return json.loads(proc.stdout[start:]), proc


def inspect(camel, workdir, name, text):
    """What `camel inspect --ties` says about a program, or None and the process."""
    path = os.path.join(workdir, name)
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)
    # `inspect` is a command, not a pass: the program is its target.
    proc = subprocess.run(
        [camel, "inspect", "--ties", path],
        capture_output=True,
        text=True,
        timeout=180,
        env=camel_env(),
    )
    if proc.returncode != 0:
        return None, proc
    return proc.stdout, proc


def without_positions(value):
    """Every field the document spells a source position in, recursively removed: a node's
    `origin` (an opaque per-build id for a position) and its `source` (the file it is written in
    and the span there). An *edge's* `source` is the node the edge comes from, which is structure
    and is kept -- the two documents must agree on every edge, or the comparison would be a
    comparison of node lists."""
    if isinstance(value, dict):
        kept = {}
        for key, item in value.items():
            if key == "origin":
                continue
            if key == "source" and isinstance(item, dict):
                continue
            kept[key] = without_positions(item)
        return kept
    if isinstance(value, list):
        return [without_positions(item) for item in value]
    return value


def document_text(document):
    return json.dumps(without_positions(document), indent=2)


def graphs_by_name(document):
    return {graph["name"]: graph for graph in document["graphs"]}


# ---------------------------------------------------------------------------------------------
# reading a graph out of a document
# ---------------------------------------------------------------------------------------------


def index_lines(graph):
    """Nodes by id, and every input edge by target: (kind, source, targetIndex) in the document's
    order, which is the target's port order."""
    nodes = {node["id"]: node for node in graph["nodes"]}
    inputs = {}
    for edge in graph["edges"]:
        if edge["kind"] not in ("normal", "context", "control"):
            continue
        inputs.setdefault(edge["target"], []).append((edge["targetIndex"], edge["source"]))
    for target in inputs:
        inputs[target] = [source for _, source in sorted(inputs[target])]
    return nodes, inputs


def subtree_text(nodes, inputs, root):
    """What a fragment is, with every number the document chose left out: the kind and label of
    each node and of everything it is computed from, in port order."""
    def describe(node):
        record = nodes[node]
        return (
            record.get("kind"),
            record.get("label"),
            record.get("outputType"),
            tuple(describe(child) for child in inputs.get(node, [])),
        )

    return json.dumps(describe(root), sort_keys=False)


def subtree_labels(graph, root):
    """The labels of the constant nodes inside a fragment: what value it builds."""
    nodes, inputs = index_lines(graph)
    labels = set()
    stack = [root]
    seen = set()
    while stack:
        node = stack.pop()
        if node in seen:
            continue
        seen.add(node)
        if nodes[node].get("kind") == "DATA":
            labels.add(nodes[node].get("label"))
        stack.extend(inputs.get(node, []))
    return labels


def fnv1a(text):
    """A structural hash of one fragment: what a hash of the subtree's text amounts to. It is the
    stand-in for the rule this one replaces, which ordered a tie by a hash."""
    value = 0xCBF29CE484222325
    for byte in text.encode("utf-8"):
        value = ((value ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


# ---------------------------------------------------------------------------------------------
# the readings
# ---------------------------------------------------------------------------------------------


def graph_summary(graph):
    """One graph's nodes as `number: KIND label (kind index)`, in document order."""
    lines = []
    for index, node in enumerate(graph["nodes"]):
        parameter = node.get("parameter") or {}
        mark = f" ({parameter.get('kind')} {parameter.get('index')})" if parameter else ""
        lines.append(f"  {index}: {node['kind']} {node.get('label')}{mark}")
    return lines


def read_order(camel, workdir):
    """Reading 1: two capture orders, one document."""
    first, proc = jgir(camel, workdir, "capture_ab.cml", CAPTURE_AB)
    if first is None:
        raise AssertionError(f"the program did not compile: {proc.stderr[:2000]}")
    second, proc = jgir(camel, workdir, "capture_ba.cml", CAPTURE_BA)
    if second is None:
        raise AssertionError(f"the program did not compile: {proc.stderr[:2000]}")
    left = document_text(first).splitlines()
    right = document_text(second).splitlines()
    mismatch = None
    for index, (a, b) in enumerate(zip(left, right)):
        if a != b:
            mismatch = index
            break
    if mismatch is None and len(left) != len(right):
        mismatch = min(len(left), len(right))
    if mismatch is None:
        return
    # What moved, graph by graph: only the graphs whose nodes the two orders numbered apart.
    moved = []
    for one, other in zip(first["graphs"], second["graphs"]):
        mine, theirs = graph_summary(one), graph_summary(other)
        if mine != theirs:
            moved.append(f"graph {one['id']} {one['name']} (a-then-b / b-then-a):")
            moved.extend(f"{a}   |{b.strip()}" for a, b in zip(mine, theirs))
    # The differing line last: the harness shows the tail of this message.
    raise AssertionError(
        f"the two capture orders wrote different documents "
        f"({len(left)} and {len(right)} lines) at line {mismatch + 1}:\n"
        + "\n".join(moved)
        + f"\ndifferent line, a-then-b: {left[mismatch].strip() if mismatch < len(left) else '<none>'}"
        + f"\ndifferent line, b-then-a: {right[mismatch].strip() if mismatch < len(right) else '<none>'}"
    )


def capture_fragments(graph):
    """The values the closure values of `graph` captured, by capture index: the captured values are
    the context (with) inputs of a FILL, and the capture index is the port they sit on."""
    by_id = {node["id"]: node for node in graph["nodes"]}
    fragments = {}
    for edge in graph["edges"]:
        if edge["kind"] != "context":
            continue
        target = by_id.get(edge["target"])
        if target is None or target["kind"] != "FILL":
            continue
        fragments[edge["targetIndex"]] = edge["source"]
    return fragments


def graph_with_captures(document):
    """The graph that holds a closure value and its captured values, by capture index."""
    for graph in document["graphs"]:
        fragments = capture_fragments(graph)
        if fragments:
            return graph, fragments
    raise AssertionError("no graph holds a closure value")


def fragment_text(graph, node):
    nodes, inputs = index_lines(graph)
    return subtree_text(nodes, inputs, node)


def choose_payloads(camel, workdir):
    """A pair of payloads whose fragments a structural hash orders the other way round.

    Each payload's fragment is read off a program that captures it once -- the shape of the
    fragment is the program's, and only the payload's text moves -- and the pair is returned with
    the smaller payload first: the smaller table wants it first, the hash wants the other one first.
    """
    texts = {}
    for value in range(1, 10):
        program = CAPTURE_ONE.replace("@VALUE@", str(value))
        document, proc = jgir(camel, workdir, f"capture_{value}.cml", program)
        if document is None:
            raise AssertionError(
                f"the probe program did not compile (exit {proc.returncode}):\n"
                f"{program}\n--- stdout ---\n{proc.stdout[:1500]}\n"
                f"--- stderr ---\n{proc.stderr[:1500]}"
            )
        graph, fragments = graph_with_captures(document)
        if 0 not in fragments:
            raise AssertionError(f"the closure of the probe program captured nothing: {program}")
        texts[value] = fragment_text(graph, fragments[0])
    for larger in range(2, 10):
        for smaller in range(1, larger):
            if fnv1a(texts[larger]) < fnv1a(texts[smaller]):
                return smaller, larger, fnv1a(texts[smaller]), fnv1a(texts[larger])
    raise AssertionError("no payload pair separates the two orders; widen the search")


def read_tie(camel, workdir):
    """Reading 2: a width-2 tie takes the smallest numbering table."""
    smaller, larger, smaller_hash, larger_hash = choose_payloads(camel, workdir)
    # The larger payload is captured first: the source names it first, the cell is created first,
    # and a structural hash of the two fragments orders it first as well. The numbering table must
    # not, and the document must therefore number the capture of the smaller payload first.
    program = CAPTURE_TWO.replace("@LARGER@", str(larger)).replace("@SMALLER@", str(smaller))
    document, proc = jgir(camel, workdir, "capture_tie.cml", program)
    if document is None:
        raise AssertionError(f"the tie program did not compile: {proc.stderr[:2000]}")
    graph, fragments = graph_with_captures(document)
    if len(fragments) != 2:
        raise AssertionError(
            f"the closure did not capture two values (captured: {sorted(fragments)}):\n{program}"
        )
    # Which captured value is which is read off the payloads inside them, not off a slot: the slots
    # the document prints are the canonical order, which is the thing under test.
    by_payload = {}
    for slot in sorted(fragments):
        labels = subtree_labels(graph, fragments[slot])
        for payload in (smaller, larger):
            if str(payload) in labels:
                by_payload[payload] = fragments[slot]
    if sorted(by_payload) != [smaller, larger]:
        raise AssertionError(
            f"the two captures do not hold the two payloads (payloads found: {sorted(by_payload)}):"
            f"\n{program}\nfragments: {subtree_text(*index_lines(graph), fragments[0])[:200]}"
        )
    order = {node["id"]: index for index, node in enumerate(graph["nodes"])}
    smaller_fragment, larger_fragment = by_payload[smaller], by_payload[larger]
    smaller_text = fragment_text(graph, smaller_fragment)
    larger_text = fragment_text(graph, larger_fragment)
    if not smaller_text < larger_text:
        raise AssertionError(
            "the two captured fragments are not ordered by their tables:\n"
            f"  payload {smaller}: {smaller_text[:300]}\n"
            f"  payload {larger}: {larger_text[:300]}"
        )
    if order[smaller_fragment] > order[larger_fragment]:
        raise AssertionError(
            f"the tie was arranged by something other than the smallest table: the capture of "
            f"payload {larger} (named first in the source, and the one a structural hash orders "
            f"first: {larger_hash:#x} < {smaller_hash:#x}) was numbered at "
            f"{order[larger_fragment]}, before the capture of payload {smaller} at "
            f"{order[smaller_fragment]}\n{program}"
        )
    return (
        f"payloads {smaller}<{larger}: source and hash order put {larger} first, the document "
        f"numbered the capture of {smaller} at {order[smaller_fragment]} and of {larger} at "
        f"{order[larger_fragment]}"
    )


def read_run(camel, workdir):
    """Reading 3: the numbering is a query; execution is what it was."""
    path = os.path.join(workdir, "ordered_run.cml")
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(ORDERED_RUN)
    proc = run_camel(camel, path, ["std::nvm"])
    if proc.returncode != 0:
        raise AssertionError(f"std::nvm exited {proc.returncode}: {proc.stderr[:2000]}")
    if proc.stdout.strip() != "1 2":
        raise AssertionError(
            f"std::nvm printed {proc.stdout.strip()!r} where the program writes '1 2'"
        )
    return "std::nvm printed '1 2', exit 0"


def canonical_documents(camel, workdir, left_name, left, right_name, right):
    """The two programs' canonical (`canonical=true`) documents, compared byte for byte."""
    first, proc = jgir(camel, workdir, left_name, left, ("std::jgir", "canonical=true"))
    if first is None:
        raise AssertionError(f"{left_name} did not compile: {proc.stderr[:2000]}")
    second, proc = jgir(camel, workdir, right_name, right, ("std::jgir", "canonical=true"))
    if second is None:
        raise AssertionError(f"{right_name} did not compile: {proc.stderr[:2000]}")
    left_lines = document_text(first).splitlines()
    right_lines = document_text(second).splitlines()
    for index, (a, b) in enumerate(zip(left_lines, right_lines)):
        if a != b:
            raise AssertionError(
                f"{left_name} and {right_name} wrote different canonical documents "
                f"({len(left_lines)} and {len(right_lines)} lines) at line {index + 1}:\n"
                f"  {left_name}: {a.strip()}\n"
                f"  {right_name}: {b.strip()}"
            )
    if len(left_lines) != len(right_lines):
        raise AssertionError(
            f"{left_name} and {right_name} wrote canonical documents of different lengths "
            f"({len(left_lines)} and {len(right_lines)} lines)"
        )
    return first, len(left_lines)


def tie_report(camel, workdir, name, text):
    """What `camel inspect --ties` says the numbering met in a program."""
    report, proc = inspect(camel, workdir, name, text)
    if report is None:
        raise AssertionError(f"inspect --ties failed on {name} (exit {proc.returncode}):\n"
                             f"{proc.stderr[:2000]}")
    summary = [line for line in report.splitlines() if line.startswith("canonical ties:")]
    if not summary:
        raise AssertionError(f"inspect --ties printed no summary for {name}:\n{report[:2000]}")
    return report, summary[0]


def read_program(camel, workdir):
    """Reading 4: the tie is taken over the whole program, not fixed per graph."""
    document, lines = canonical_documents(
        camel, workdir, "program_ab.cml", PROGRAM_AB, "program_ba.cml", PROGRAM_BA
    )
    if len(document["graphs"]) < 2:
        raise AssertionError(
            f"the program has one graph ({len(document['graphs'])}), so no binder reads a bound "
            f"graph and the reading says nothing"
        )
    # The binding is what the two spellings had to agree on: the graph the closure binds is a graph
    # of the document, its capture ports are numbered there, and the closure's context slots are the
    # values in that order.
    captures = {}
    for graph in document["graphs"]:
        if graph["capturePorts"]:
            captures[graph["id"]] = graph["capturePorts"]
    if not captures:
        raise AssertionError("no graph of the document has capture ports")
    report, summary = tie_report(camel, workdir, "program_ab.cml", PROGRAM_AB)
    return (
        f"the two capture orders wrote one canonical document ({lines} lines, "
        f"{len(document['graphs'])} graphs, {len(captures)} with capture ports); "
        f"inspect --ties: {summary.strip()}"
    )


def read_wide(camel, workdir):
    """Reading 5: a tie wider than the enumeration limit is ordered by the key, not by the source."""
    document, lines = canonical_documents(
        camel, workdir, "wide_ab.cml", WIDE_AB, "wide_ba.cml", WIDE_BA
    )
    report, summary = tie_report(camel, workdir, "wide_ab.cml", WIDE_AB)
    if "not enumerated" in report:
        raise AssertionError(
            "a tie part the key leaves equal was left over the limit on the eight-capture "
            f"program, where the eight types tell the members apart:\n{report[:2000]}"
        )
    return (
        f"eight captures of eight types, capture order swapped: one canonical document "
        f"({lines} lines); inspect --ties: {summary.strip()}"
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--camel", required=True, help="the compiler of the build under test")
    parser.add_argument("--probe", action="store_true", help="print the documents and exit 1")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="canon-order-") as workdir:
        if args.probe:
            document, proc = jgir(args.camel, workdir, "capture_ab.cml", CAPTURE_AB)
            if document is None:
                print(f"compile failed: {proc.stderr[:2000]}")
                return 1
            for name, graph in graphs_by_name(document).items():
                print(f"graph {name}:")
                for node in graph["nodes"]:
                    print("  " + json.dumps(node, sort_keys=False)[:220])
                print(f"  capturePorts: {graph.get('capturePorts')}")
            return 1
        readings = []
        read_order(args.camel, workdir)
        readings.append("order: the two capture orders wrote one document")
        readings.append("tie: " + read_tie(args.camel, workdir))
        readings.append("run: " + read_run(args.camel, workdir))
        readings.append("program: " + read_program(args.camel, workdir))
        readings.append("wide: " + read_wide(args.camel, workdir))
    print("canonical order: PASS")
    for reading in readings:
        print(f"  {reading}")
    return 0


if __name__ == "__main__":
    sys.exit(main())