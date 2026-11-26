"""A block's body compiles into a program of its own, the block's operator runs it, and the
program it was compiled for keeps its own graphs.

A block is an operator whose implementation is a program: it is compiled by the same compiler and
run by the same VM, and no pass walks into it (graph-editing design 5, design principles 9.12).
This case measures the arrangement on one program, in four readings:

  * `test::block_body` hands the compiler two bodies written as text and compiles each as a program
    of its own: `BLOCK_BODY built <what> graphs=… nodes=…`, and one line per graph of each
    (`BLOCK_BODY graph <what> <graph> nodes=…`);
  * the entry program, read in the same run and by `std::stats`: the graphs and nodes are the ones
    the program had without the pass. The pass prints the counts it saw before and after
    (`BLOCK_BODY entry graphs a -> b nodes c -> d`), so the comparison is in one run;
  * the values two blocks answer through one kernel (`BLOCK_CALL <uri> argument=… value=…`): the
    URI finds the operator through the executor manager (`block` protocol), the node's attribute
    names which program of the context's store to run, and the value is the body's own arithmetic;
  * the same block run a second time, after its first run is over, so "the program is pinned to the
    context" is a value and not the absence of a crash;
  * the `std::jgir` document of the same pipeline: the graphs it names are the entry program's, and
    the bodies added none of them to it.

The block's own node in the outer graph, its record, and the front end that writes a body are the
steps after this one.
"""
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "test" / "tools"))
from camel_arg import camel_from_argv  # noqa: E402

EXE, ARGV = camel_from_argv(ROOT)
SOURCE = str(ROOT / "test/cases/loops/captures.cml")
BODY_PASS = "test::block_body"


def invoke(*args):
    return subprocess.run([EXE, *ARGV, *args], cwd=ROOT, capture_output=True, text=True)


def stats(*passes):
    p = invoke(SOURCE, *passes, "std::stats", "std::nop")
    assert p.returncode == 0, p.stderr
    lines = [line for line in p.stdout.splitlines() if line.startswith("GRAPH_STATS ")]
    assert len(lines) == 1, f"expected one GRAPH_STATS line, got {len(lines)}: {p.stdout[:400]}"
    return json.loads(lines[0][len("GRAPH_STATS ") :])


def numbered(line):
    return {name: int(value) for name, value in re.findall(r"(graphs|nodes)=(\d+)", line)}


plain = stats()
with_body = invoke(SOURCE, BODY_PASS, "std::stats", "std::nop")
if with_body.returncode != 0:
    tail = (with_body.stderr or with_body.stdout).strip().splitlines()
    raise AssertionError(f"rc={with_body.returncode} {tail[-1][:200] if tail else 'no output'}")
lines = with_body.stdout.splitlines()
body_lines = [line for line in lines if line.startswith("BLOCK_BODY ")]
call_lines = [line for line in lines if line.startswith("BLOCK_CALL ")]
trait_lines = [line for line in lines if line.startswith("BLOCK_TRAITS ")]
# What the pass said, on this case's own stdout: the plan pins these lines, so the reading is
# checked as text and not only as the numbers the assertions below derive from it.
arrange_lines = [line for line in lines if line.startswith("BLOCK_ARRANGE ")]
cost_lines = [line for line in lines if line.startswith("BLOCK_COST ")]
for line in body_lines + trait_lines + arrange_lines + cost_lines + call_lines:
    print(line)

# Five bodies, each a program of its own: a root graph and the function's graph, in a program that
# is not the one they were compiled for.
built = [line for line in body_lines if line.startswith("BLOCK_BODY built ")]
assert len(built) == 10, f"expected ten bodies to compile, got: {body_lines}"
bodies = {}
for line in built:
    what = line[len("BLOCK_BODY built ") :].split(" ")[0]
    counts = numbered(line)
    assert counts["graphs"] >= 2, line
    assert counts["nodes"] >= 1, line
    bodies[what] = counts
# A program has a root graph (design principles 2.3), and the graphs of a program are named among
# that program's graphs only: the name is no identity across two programs, so the counts are the
# identity this case uses.
graphs_of = {}
for line in body_lines:
    if not line.startswith("BLOCK_BODY graph "):
        continue
    parts = line[len("BLOCK_BODY graph ") :].split(" ")
    what, graph = parts[0], parts[1]
    graphs_of.setdefault(what, []).append(graph)
for what, counts in bodies.items():
    assert len(graphs_of.get(what, [])) == counts["graphs"], (what, graphs_of.get(what), counts)
    assert "__root__" in graphs_of[what], (what, graphs_of[what])

# The block's operator is named by what its body computes, and its record is what the body does:
# both are read off the body's own graph, not written by this case.
traits = {}
for line in trait_lines:
    match = re.fullmatch(
        r"BLOCK_TRAITS (\S+) uri=(block:[0-9a-f]{16}) pure=(\w+) mayFault=(\w+) "
        r"mayNotTerminate=(\w+) foldable=(\w+) portable=([0-9a-f]{16})",
        line,
    )
    assert match, line
    traits[match.group(1)] = {
        "uri": match.group(2),
        "pure": match.group(3) == "true",
        "portable": match.group(7),
        "mayFault": match.group(4) == "true",
        "mayNotTerminate": match.group(5) == "true",
        "foldable": match.group(6) == "true",
    }
assert set(traits) == set(bodies), (set(traits), set(bodies))
uris = {what: entry["uri"] for what, entry in traits.items()}
# Four bodies that compute different things are four blocks.
assert len({uris["body"], uris["second"], uris["effectful"], uris["fallible"]}) == 4, uris
# The same body, compiled twice, is one block under one name: a body is compiled as the block's own
# module, so the name of the module that handed it over is not an input of its identity.
assert uris["twin"] == uris["body"], (uris["body"], uris["twin"])
# The record, read from the body's graphs: arithmetic is pure and cannot fault, printing is not
# pure, and a division can divide by zero.
assert traits["body"]["pure"] and not traits["body"]["mayFault"], traits["body"]
assert not traits["body"]["mayNotTerminate"] and traits["body"]["foldable"], traits["body"]
assert traits["second"]["pure"] and not traits["second"]["mayFault"], traits["second"]
assert not traits["effectful"]["pure"], traits["effectful"]
assert traits["fallible"]["pure"] and traits["fallible"]["mayFault"], traits["fallible"]
# A body that calls itself never returns, and that is a fact of the record now, beside the fault:
# it is not hidden in `foldable`, which goes on saying only that the result may be kept.
assert traits["spin"]["mayNotTerminate"] and traits["spin"]["foldable"], traits["spin"]
# The family layer, asked through its own functions: the block whose body ends is freely arrangeable,
# the block whose body calls itself is not, and the term that fails is the termination one. This is
# the fact store's answer, not the record this case checked above -- and it only says "may not end"
# because the body's graph says so: take the call out of the body and the same line turns over.
arrange = {}
for line in arrange_lines:
    match = re.fullmatch(r"BLOCK_ARRANGE (\S+) family=(.+) all=(\d) terminates=(\d) foldable_field=(\d)", line)
    assert match, line
    arrange[match.group(1)] = {
        "family": match.group(2),
        "all": match.group(3) == "1",
        "terminates": match.group(4) == "1",
        "foldable": match.group(5) == "1",
    }
assert arrange["body"]["terminates"] and "may not terminate" not in arrange["body"]["family"], arrange
assert not arrange["spin"]["all"] and not arrange["spin"]["terminates"], arrange
assert "may not terminate" in arrange["spin"]["family"], arrange
# The same body with the call taken out: the reading turns over, so the term is the body's and not
# something this fixture wrote. That is the evidence, harder than any assertion about a constant.
assert arrange["stops"]["terminates"] and not arrange["spin"]["terminates"], arrange
assert "may not terminate" not in arrange["stops"]["family"], arrange
assert arrange["body"]["foldable"] and arrange["spin"]["foldable"], arrange
# The cost fact: an operator that states one, one that states nothing, and a stated one whose value
# is the number the silence would have produced. Only the stated bit tells the last two apart, which
# is why the reader returns an answer that can be "not stated".
cost = {}
for line in cost_lines:
    match = re.fullmatch(r"BLOCK_COST (\S+) stated=(\d) value=([\d.]+)", line)
    assert match, line
    cost[match.group(1)] = {"stated": match.group(2) == "1", "value": float(match.group(3))}
assert cost["declared_eight"] == {"stated": True, "value": 8.0}, cost
assert cost["declared_one"] == {"stated": True, "value": 1.0}, cost
assert cost["silent"] == {"stated": False, "value": 1.0}, cost
assert cost["declared_one"]["stated"] != cost["silent"]["stated"], cost
assert not any(traits[what]["mayNotTerminate"] for what in ("body", "second", "effectful", "fallible")), traits
store = int(re.search(r"BLOCK_BODY store blocks=(\d+)", with_body.stdout).group(1))
by_uri = {}
for what, uri in uris.items():
    by_uri.setdefault(uri, []).append(what)
assert store == len(by_uri), (store, {u: w for u, w in by_uri.items() if len(w) > 1})
# And the same body spelled another way -- another function name, another parameter name -- is the
# same block: a name nobody sees cannot name a block. The other hash disagrees here, and it should:
# that one is the cache key, which has to tell two candidates apart, not the name of a computation.
assert uris["renamed"] == uris["body"], (uris["body"], uris["renamed"])
assert traits["renamed"]["portable"] != traits["body"]["portable"], traits["renamed"]
# Two bodies that differ only in which parameter is subtracted from which are two blocks.
assert uris["mirror"] != uris["diff"], (uris["diff"], uris["mirror"])
assert len({uris[what] for what in ("body", "second", "effectful", "fallible", "spin", "diff", "mirror")}) == 7, uris

# The entry program, read in the same run: the numbers it had before the bodies were built are the
# numbers it has after, and they are the numbers of a run without the pass at all.
entry = [line for line in body_lines if line.startswith("BLOCK_BODY entry ")]
assert len(entry) == 1, body_lines
assert "took a graph of the body" not in with_body.stdout, entry[0]
entry_numbers = re.search(r"graphs (\d+) -> (\d+) nodes (\d+) -> (\d+)", entry[0])
assert entry_numbers, entry[0]
before_graphs, after_graphs, before_nodes, after_nodes = (
    int(value) for value in entry_numbers.groups()
)
assert before_graphs == after_graphs, entry[0]
# The entry program gained the block's one node and kept every graph: the bodies' graphs are not
# here, and nothing else moved either.
assert before_nodes == plain["nodes"], (entry[0], plain["nodes"])
assert (after_graphs, after_nodes) == (plain["graphs"], plain["nodes"] + 1), entry[0]

# One kernel, two blocks, and the value each body computes: 41 -> 42, 5 -> 15. The ordinal in the
# node's attributes is not in the call here, so the kernel could not have guessed the program from
# the call's shape: the two calls have the same shape and different values.
values = {}
for line in call_lines:
    match = re.fullmatch(
        r"BLOCK_CALL (\S+) (\S+) argument=(-?\d+) value=(-?\d+)", line
    )
    assert match, line
    assert match.group(2) == uris[match.group(1)], line
    values.setdefault(match.group(1), []).append(int(match.group(4)))
assert values == {"body": [42, 2], "second": [15]}, values

again = stats(BODY_PASS)
# One node and one operator joined the entry program -- the same thing counted twice, so both rise
# by one. The node stands between two nodes the graph already had, so the chain it joins has one
# link more: one node in a chain is one edge more, which is the same count seen the other way. Its
# argument is a value the graph already had, so no value node joined it. Calls, closures, branches
# and the graphs themselves are untouched.
assert again["nodes"] == plain["nodes"] + 1, (plain["nodes"], again["nodes"])
assert again["opers"] == plain["opers"] + 1, (plain["opers"], again["opers"])
assert again["edges"] == plain["edges"] + 1, (plain["edges"], again["edges"])
for field in ("graphs", "direct_calls", "indirect_calls", "closures", "branches", "loops"):
    assert again[field] == plain[field], (field, plain[field], again[field])
assert again["graph_names"] == plain["graph_names"], (plain["graph_names"], again["graph_names"])

document = invoke(SOURCE, BODY_PASS, "std::jgir", "std::nop")
assert document.returncode == 0, document.stderr
# The pass's own lines come before the document: the document is what starts at the first brace.
start = document.stdout.find("{")
assert start >= 0, document.stdout[:400]
end = document.stdout.rfind("}")
written = json.loads(document.stdout[start : end + 1])
assert len(written["graphs"]) == plain["graphs"], (len(written["graphs"]), plain["graphs"])

print(
    f"block_body: body_graphs={bodies['body']['graphs']} body_nodes={bodies['body']['nodes']} "
    f"blocks={len(bodies)} store={store} block_calls={sum(len(v) for v in values.values())} "
    f"entry_nodes={plain['nodes']}->{again['nodes']} entry_opers={plain['opers']}->{again['opers']}"
)
print("Block body: PASS")