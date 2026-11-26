"""A block's body compiles into a program of its own, and the program it was compiled for keeps
its own graphs.

A block is an operator whose implementation is a program: it is compiled by the same compiler and
run by the same VM, and no pass walks into it (graph-editing design 5, design principles 9.12).
This case takes the first half of that arrangement and measures it on one program, three ways:

  * `test::block_body` hands the compiler a body written as text and compiles it as a program of
    its own. Its own reading is `BLOCK_BODY built graphs=… nodes=…` -- the body compiled, and into
    graphs of its own;
  * the same run's entry program, read by `std::stats`: the graphs and nodes are the ones the
    program had without the pass. The pass prints the count it saw before and after the body was
    built (`BLOCK_BODY entry graphs a -> b nodes c -> d`), so the comparison is in one run;
  * the `std::jgir` document of the same pipeline: the graphs it names are the entry program's,
    and the body added none of them to it.

The block's operator, its record, and the front end that writes such a body are not here (the
four steps of the plan; this is the first). What is here is the one thing that could be measured
without them: that a body compiles into a program that is not the program it was compiled for.
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


plain = stats()
with_body = invoke(SOURCE, BODY_PASS, "std::stats", "std::nop")
assert with_body.returncode == 0, with_body.stderr
body_lines = [line for line in with_body.stdout.splitlines() if line.startswith("BLOCK_BODY ")]
built = [line for line in body_lines if line.startswith("BLOCK_BODY built ")]
assert len(built) == 1, f"expected the body to compile once, got: {body_lines}"
numbers = dict(re.findall(r"(graphs|nodes)=(\d+)", built[0]))
graphs, nodes = int(numbers["graphs"]), int(numbers["nodes"])
assert graphs >= 1, built[0]
assert nodes >= 1, built[0]
# A program has a root graph (design principles 2.3): the body's program has one of its own, and
# the graphs of a program are named among that program's graphs only, so the name is no identity
# across programs -- the counts are the identity this case uses.
body_graphs = [
    line[len("BLOCK_BODY graph ") :] for line in body_lines if line.startswith("BLOCK_BODY graph ")
]
assert len(body_graphs) == graphs, (body_graphs, graphs)
assert any(line.startswith("__root__ ") for line in body_graphs), body_graphs

# The entry program, read in the same run: the numbers it had before the body was built are the
# numbers it has after, and they are the numbers of a run without the pass at all.
entry = [line for line in body_lines if line.startswith("BLOCK_BODY entry ")]
assert len(entry) == 1, body_lines
assert "took a graph of the body" not in with_body.stdout, entry[0]
entryNumbers = re.search(
    r"graphs (\d+) -> (\d+) nodes (\d+) -> (\d+)", entry[0]
)
assert entryNumbers, entry[0]
beforeGraphs, afterGraphs, beforeNodes, afterNodes = (int(g) for g in entryNumbers.groups())
assert (beforeGraphs, beforeNodes) == (afterGraphs, afterNodes), entry[0]
assert (beforeGraphs, beforeNodes) == (plain["graphs"], plain["nodes"]), (entry[0], plain)
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
    f"block_body: body_graphs={graphs} body_nodes={nodes} entry_graphs={plain['graphs']} "
    f"entry_nodes={plain['nodes']}"
)
print("Block body: PASS")