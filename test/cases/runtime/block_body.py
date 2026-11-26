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
assert with_body.returncode == 0, with_body.stderr
lines = with_body.stdout.splitlines()
body_lines = [line for line in lines if line.startswith("BLOCK_BODY ")]
call_lines = [line for line in lines if line.startswith("BLOCK_CALL ")]
# What the pass said, on this case's own stdout: the plan pins these lines, so the reading is
# checked as text and not only as the numbers the assertions below derive from it.
for line in body_lines + call_lines:
    print(line)

# Two bodies, each a program of its own: a root graph and the function's graph, in a program that
# is not the one they were compiled for.
built = [line for line in body_lines if line.startswith("BLOCK_BODY built ")]
assert len(built) == 2, f"expected two bodies to compile, got: {body_lines}"
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
assert (before_graphs, before_nodes) == (after_graphs, after_nodes), entry[0]
assert (before_graphs, before_nodes) == (plain["graphs"], plain["nodes"]), (entry[0], plain)

# One kernel, two blocks, and the value each body computes: 41 -> 42, 5 -> 15. The ordinal in the
# node's attributes is not in the call here, so the kernel could not have guessed the program from
# the call's shape: the two calls have the same shape and different values.
values = {}
for line in call_lines:
    match = re.fullmatch(r"BLOCK_CALL (\S+) argument=(-?\d+) value=(-?\d+)", line)
    assert match, line
    values.setdefault(match.group(1), []).append(int(match.group(3)))
assert values == {"block::body": [42, 2], "block::scaled": [15]}, values

again = stats(BODY_PASS)
assert again["graphs"] == plain["graphs"], (plain["graphs"], again["graphs"])
assert again["nodes"] == plain["nodes"], (plain["nodes"], again["nodes"])
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
    f"second_nodes={bodies['second']['nodes']} block_calls={sum(len(v) for v in values.values())} "
    f"entry_graphs={plain['graphs']} entry_nodes={plain['nodes']}"
)
print("Block body: PASS")