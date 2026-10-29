"""The canonical JSON GIR document and the equivalence checker (plan S0).

S0 changes no language and no pipeline: it adds a second shape of the existing JSON dump
(`std::jgir canonical=true`, version 2) that keeps only what graph equivalence is defined on,
and an independent structural checker (`test/tools/cml_roundtrip.py`). This case pins both:

  * the document is deterministic and contains no names, origins or original node numbers;
  * the default dump is untouched (still version 1);
  * the document carries the `sync` and `order` edges, reduced to the part of the order no data
    path or other ordering path already implies, so the two spellings of one `var` assignment
    write one document; two effects on one resource stay ordered in it; and the checker says
    different when an ordered pair is dropped;
  * the checker says equal for the same program twice, for a document whose input lists are
    reversed (equivalence orders edges by kind and slot), for two programs that differ only in
    names, and for a real program round-tripped through the no-op pass `std::stats`;
  * it says different for two programs whose documents differ only in a node attribute, and for
    a document whose one attribute value was changed;
  * over budget it stops and reports, with exit code 2, never "equal".

It also fails if it examined no pairs at all.
"""
import copy
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "test" / "tools"))
from camel_arg import camel_from_argv  # noqa: E402

# The install this case measures: `--camel <path>` names it; without it this tree's `out/latest/bin/camel`. One rule, in one place: test/tools/camel_arg.py.
EXE, ARGV = camel_from_argv(ROOT)
CHECKER = ROOT / "test/tools/cml_roundtrip.py"
PYTHON = sys.executable
CASES = ROOT / "test/cases"

TMP = Path(tempfile.mkdtemp(prefix="cml-canon-"))
LOOPS = CASES / "loops/captures.cml"
CALLS = CASES / "trans/jgir_duplicate.cml"
NAME_A = CASES / "trans/cml_canon_name_a.cml"
NAME_B = CASES / "trans/cml_canon_name_b.cml"
ATTR_A = CASES / "trans/cml_canon_attr_a.cml"
ATTR_B = CASES / "trans/cml_canon_attr_b.cml"

VERDICTS = 0


def invoke(*args):
    return subprocess.run([str(a) for a in args], cwd=str(ROOT), capture_output=True, text=True)


def canonical(source, *passes):
    """The canonical document a pipeline writes for `source`, as (text, parsed)."""
    done = invoke(EXE, source, *passes, "std::jgir canonical=true")
    assert done.returncode == 0, (source, passes, done.stdout, done.stderr)
    body = "\n".join(
        line for line in done.stdout.splitlines() if not line.startswith("GRAPH_STATS")
    )
    document = json.loads(body)
    assert document["format"] == "camel.gir"
    assert document["version"] == 2 and document["canonical"] is True, (
        "the canonical mode is the version 2 document (docs/technical/gir-interchange.md)"
    )
    return body, document


def write(name, value):
    path = TMP / name
    path.write_text(value if isinstance(value, str) else json.dumps(value, indent=2), encoding="utf-8")
    return path


def compare(left, right, expect_equal, *options):
    """Run the checker; returns its output. Counts every pair actually compared."""
    global VERDICTS
    done = invoke(PYTHON, CHECKER, left, right, *options)
    VERDICTS += 1
    if expect_equal:
        assert done.returncode == 0, (left, right, done.stdout, done.stderr)
        assert done.stdout.startswith("EQUAL:"), done.stdout
    else:
        assert done.returncode == 1, (left, right, done.stdout, done.stderr)
        assert done.stdout.startswith("DIFFERENT:"), done.stdout
    return done.stdout


def nodes_of(document):
    return {(graph["id"], node["id"]): node for graph in document["graphs"] for node in graph["nodes"]}


def differing_fields(left, right):
    """Which graph/node fields differ between two canonical documents."""
    fields = set()
    lg = {graph["id"]: graph for graph in left["graphs"]}
    rg = {graph["id"]: graph for graph in right["graphs"]}
    assert set(lg) == set(rg), "the documents do not even share their graph numbers"
    for gid in lg:
        for key in set(lg[gid]) | set(rg[gid]):
            if key != "nodes" and lg[gid].get(key) != rg[gid].get(key):
                fields.add(f"graph.{key}")
        ln = {node["id"]: node for node in lg[gid]["nodes"]}
        rn = {node["id"]: node for node in rg[gid]["nodes"]}
        assert set(ln) == set(rn), "the documents do not even share their node numbers"
        for nid in ln:
            for key in set(ln[nid]) | set(rn[nid]):
                if ln[nid].get(key) != rn[nid].get(key):
                    fields.add(f"node.{key}")
    return fields


# -- 1. the document is what it says it is ------------------------------------------------

base_text, base = canonical(LOOPS)
assert (base_text, base) == canonical(LOOPS), "the canonical dump is not deterministic"
optional = {"root"}
required_graph = {
    "id", "flags", "signature", "nodes", "regions",
    "normalPorts", "contextPorts", "capturePorts", "exit",
}
required_node = {"id", "kind", "type", "inputs"}
allowed_node = required_node | {"region", "attrs", "perms", "param", "callee", "loop", "value", "payload"}
allowed_graph = required_graph | optional
assert set(base) == {
    "format", "version", "canonical", "root", "entry", "decls", "tags", "types", "graphs"
}, set(base)
assert base["root"] in {graph["id"] for graph in base["graphs"]}
assert base["entry"] in {graph["id"] for graph in base["graphs"]}, (
    "the root's exit calls a function constant, so the entry graph is known"
)
type_count = len(base["types"])
for graph in base["graphs"]:
    assert set(graph) <= allowed_graph, set(graph) - allowed_graph
    assert required_graph <= set(graph), required_graph - set(graph)
    assert "name" not in graph and "dependencies" not in graph
    assert isinstance(graph["signature"], int) and 0 <= graph["signature"] < type_count
    ids = [node["id"] for node in graph["nodes"]]
    assert ids == [f"n{i}" for i in range(len(ids))], "node numbers are this export's own"
    for node in graph["nodes"]:
        assert set(node) <= allowed_node, set(node) - allowed_node
        assert required_node <= set(node), required_node - set(node)
        assert "label" not in node and "origin" not in node and "source" not in node
        assert "outputType" not in node, "types are table entries, not display strings"
        assert isinstance(node["type"], int) and 0 <= node["type"] < type_count
        assert isinstance(node["kind"], str) and node["kind"] not in ("ARGS", "EXIT")
        slots = {}
        for item in node["inputs"]:
            assert set(item) == {"kind", "slot", "source"}, item
            assert item["kind"] in ("normal", "context", "control", "sync", "order")
            slots.setdefault(item["kind"], []).append(item["slot"])
            assert item["source"] is None or item["source"] in set(ids)
        for kind, used in slots.items():
            assert used == list(range(len(used))), (node["id"], kind, used)
        if node.get("callee") is not None:
            assert node["callee"] in {graph2["id"] for graph2 in base["graphs"]}
    region_ids = [region["id"] for region in graph["regions"]]
    assert region_ids == list(range(1, len(region_ids) + 1)), "region numbers are this export's own"
    for region in graph["regions"]:
        assert set(region) <= {"id", "kind", "index", "parent", "owner", "result", "ports", "params", "attrs"}
        assert region["kind"] in ("arm", "loop_cond", "loop_body"), region["kind"]

# The default dump is unchanged: version 1, and the display fields are still there.
display = invoke(EXE, LOOPS, "std::jgir")
assert display.returncode == 0, display.stderr
v1 = json.loads(display.stdout)
assert v1["format"] == "camel.gir" and v1["version"] == 1
assert "canonical" not in v1
assert any("label" in node for graph in v1["graphs"] for node in graph["nodes"])

# -- 2. the pairs -------------------------------------------------------------------------

# Same program twice: equal. (Its text was already asserted identical above.)
compare(write("base.json", base), write("base2.json", canonical(LOOPS)[1]), True)

# Only the order of the input lists differs: the value edges are compared by kind and slot and the
# ordering edges as the pairs they spell, so equal.
reordered = copy.deepcopy(base)
for graph in reordered["graphs"]:
    for node in graph["nodes"]:
        node["inputs"] = list(reversed(node["inputs"]))
assert differing_fields(base, reordered), "the reordered document is identical, so it proves nothing"
compare(write("reordered.json", reordered), write("base3.json", base), True)

# Only names differ between two real programs: equal, and the dumps are byte-identical
# (a name that leaks would show up here).
name_a_text, name_a = canonical(NAME_A)
name_b_text, name_b = canonical(NAME_B)
assert name_a_text == name_b_text, "a name reached the canonical dump"
compare(write("name_a.json", name_a), write("name_b.json", name_b), True)

# Only an attribute differs between two real programs: different.
attr_a_text, attr_a = canonical(ATTR_A)
attr_b_text, attr_b = canonical(ATTR_B)
assert attr_a_text != attr_b_text, "the attribute did not reach the canonical dump"
attr_fields = differing_fields(attr_a, attr_b)
assert attr_fields and attr_fields <= {"node.attrs", "node.perms"}, attr_fields
compare(write("attr_a.json", attr_a), write("attr_b.json", attr_b), False)

# Only one attribute value differs: different, and the difference is that value.
mutated = copy.deepcopy(attr_b)
found = False
for graph in mutated["graphs"]:
    for node in graph["nodes"]:
        for tag in sorted(node.get("attrs") or {}):
            node["attrs"][tag] = not node["attrs"][tag]
            found = True
            break
        if found:
            break
    if found:
        break
assert found, "the attribute-only pair has no attributes to change"
assert differing_fields(attr_b, mutated) == {"node.attrs"}
compare(write("mutated.json", mutated), write("attr_b2.json", attr_b), False)

# A real program round-tripped through a pass that hands the graph on unchanged: equal.
through_stats = canonical(LOOPS, "std::stats")[0]
compare(write("stats.json", json.loads(through_stats)), write("base4.json", base), True)

# A real program with branches, loops, calls and captures, twice: equal.
calls = canonical(CALLS)[1]
compare(write("calls.json", calls), write("calls2.json", json.loads(canonical(CALLS)[0])), True)

# -- 3. the ordering edges ----------------------------------------------------------------

# A program whose body holds both kinds of ordering edge: a read of a `var` and the write after
# it are one ordered pair with no data path between them (an `order` edge), and the statements of
# the sync block are a chain (a `sync` edge). Both are in the graph before any pass runs.
ORDERED = write(
    "ordering_order.cml",
    "func main(): int sync {\n"
    "    var x = 1\n"
    "    let y: int = x\n"
    "    x = 5\n"
    "    println(y)\n"
    "    println(x)\n"
    "    return 0\n"
    "}\n",
)
ordered = canonical(ORDERED)[1]

# Two effects on one ambient resource, neither of which reads the other's value. Their order is
# the source order, and it is what the document has to show: the point of exporting the ordering
# edges at all. The builder does not write an `order` edge between two effects of one resource
# yet, so today the pair is carried by the sync chain of the block and by the control wait; the
# assertion below names no kind, because the order is the same order whichever kind spells it,
# and a same-resource `order` edge would join the same pair without changing this.
DOUBLE = write(
    "ordering_effects.cml",
    "func main(): int sync {\n" "    println(9)\n" "    println(0)\n" "    return 0\n" "}\n",
)
double = canonical(DOUBLE)[1]


def ordering_edges(document):
    """The ordering edges of a document: {(graph, target): {source: {kinds}}}."""
    edges = {}
    for graph in document["graphs"]:
        for node in graph["nodes"]:
            for item in node["inputs"]:
                if item["kind"] not in ("control", "sync", "order") or item["source"] is None:
                    continue
                per = edges.setdefault((graph["id"], node["id"]), {})
                per.setdefault(item["source"], set()).add(item["kind"])
    return edges


def ordered_through_another_path(document, gid, source, target):
    """Whether `source` reaches `target` over at least two edges of the graph the document holds.

    Every kind counts (data, control, sync, order): each of them orders the two ends, and a
    document keeps an edge only where no such path already orders them.
    """
    successors = {}
    for graph in document["graphs"]:
        if graph["id"] != gid:
            continue
        for node in graph["nodes"]:
            for item in node["inputs"]:
                if item["source"] is None:
                    continue
                if (item["source"], node["id"]) == (source, target):
                    continue
                successors.setdefault(item["source"], []).append(node["id"])
    seen, level = {source}, successors.get(source, [])
    depth = 1
    while level:
        if target in level:
            return depth >= 2
        next_level = []
        for node in level:
            for successor in successors.get(node, []):
                if successor not in seen:
                    seen.add(successor)
                    next_level.append(successor)
        level, depth = next_level, depth + 1
    return False


# Both kinds reached the document.
loops_edges = ordering_edges(base)
assert any(
    "sync" in kinds for sources in loops_edges.values() for kinds in sources.values()
), "no sync edge reached the canonical document"
assert any(
    "order" in kinds for sources in ordering_edges(ordered).values() for kinds in sources.values()
), "no order edge reached the canonical document"

# The reduction: no `sync` or `order` edge is left whose two ends another path already orders.
# Without it the document would carry the edges of one spelling of a program and not the ones of
# another (docs/technical/gir-interchange.md, "Canonical documents").
for document, name in ((base, "loops"), (ordered, "ordering")):
    for (gid, target), sources in ordering_edges(document).items():
        for source, kinds in sources.items():
            if not kinds & {"sync", "order"}:
                continue
            assert not ordered_through_another_path(document, gid, source, target), (
                f"{name}: {source} -> {target} is written as {sorted(kinds)} although another "
                "path of the graph already orders the two ends"
            )

# A redundant ordering edge really is gone: the read of a `var` and the write after it are one
# pair here, and the printed body of the same assignment -- whose read and write are joined by an
# `order` edge a data path already implies -- writes the same document as the assignment itself.
VAR_PARAM = CASES / "trans/cml/var_param.cml"
printed = TMP / "var_param_printed.cml"
just_printed = invoke(EXE, VAR_PARAM, "std::cml")
assert just_printed.returncode == 0, just_printed.stderr
printed.write_text(just_printed.stdout, encoding="utf-8")
printed_doc = canonical(printed)[1]
compare(
    write("var_param.json", canonical(VAR_PARAM)[1]),
    write("var_param_printed.json", printed_doc),
    True,
)

# The document orders the two `println`s of one resource, in the order the program wrote them:
# the one that prints 9 comes before the one that prints 0.
printlns = [
    key
    for key, node in nodes_of(double).items()
    if "println" in str((node.get("payload") or {}).get("op"))
]
assert len(printlns) == 2, printlns
values = {}
for key in printlns:
    for item in nodes_of(double)[key]["inputs"]:
        if item["kind"] != "normal":
            continue
        printed_value = nodes_of(double).get((key[0], item["source"]), {}).get("value") or {}
        if printed_value.get("k") in ("i32", "i64"):
            values[key] = printed_value["v"]
assert sorted(values.values()) == [0, 9], values
nine = next(key for key, value in values.items() if value == 9)
zero = next(key for key, value in values.items() if value == 0)
assert nine[0] == zero[0], "the two effects are statements of one graph"
assert nine[1] in ordering_edges(double).get(zero, {}), (
    "the document does not order the two printlns of one resource: "
    f"{ordering_edges(double).get(zero, {})}"
)

# The checker reads them: dropping one ordered pair from a document makes it different, and so
# does dropping the `order` edge of a pair that carries no other kind.
dropped = copy.deepcopy(ordered)
for graph in dropped["graphs"]:
    for node in graph["nodes"]:
        node["inputs"] = [item for item in node["inputs"] if item["kind"] != "order"]
assert differing_fields(ordered, dropped) == {"node.inputs"}
compare(write("dropped_order.json", dropped), write("ordered.json", ordered), False)

unpaired = copy.deepcopy(double)
for graph in unpaired["graphs"]:
    for node in graph["nodes"]:
        node["inputs"] = [
            item
            for item in node["inputs"]
            if not (item["source"] == nine[1] and item["kind"] in ("control", "sync", "order"))
        ]
assert differing_fields(double, unpaired) == {"node.inputs"}
compare(write("unpaired.json", unpaired), write("double.json", double), False)

# -- 4. budget: stops and reports, never passes --------------------------------------------

def over_budget(*options):
    left = write("budget_a.json", base)
    right = write("budget_b.json", json.loads(canonical(CALLS)[0]))
    done = invoke(PYTHON, CHECKER, left, right, *options)
    assert done.returncode == 2, (options, done.returncode, done.stdout, done.stderr)
    assert done.stdout.startswith("BUDGET:"), done.stdout
    assert "EQUAL" not in done.stdout
    return done.stdout


# Different programs: the budget is checked while the documents are built.
over_budget("--max-nodes", "1")
# The same document twice: refinement finishes, the search must stop inside its budget and
# report, not answer.
same = write("budget_same.json", base)
for options in (("--max-steps", "0"), ("--max-steps", "1")):
    done = invoke(PYTHON, CHECKER, same, same, *options)
    assert done.returncode == 2, (options, done.returncode, done.stdout, done.stderr)
    assert done.stdout.startswith("BUDGET:"), done.stdout
    assert "EQUAL" not in done.stdout

# A display document is not a canonical one: the checker says so instead of comparing apples
# with oranges.
v1_path = write("v1.json", v1)
done = invoke(PYTHON, CHECKER, v1_path, v1_path)
assert done.returncode == 1 and "canonical" in done.stdout, done.stdout

assert VERDICTS >= 4, f"the case examined only {VERDICTS} pairs"
print(f"canonical GIR pairs compared: {VERDICTS}")
print("canonical GIR: PASS")