"""A program is the graphs its entry reaches, and naming a Camel pass adds none of its own.

Design principles 2.3: a program is its root function, and what belongs to it is the graphs its
entry reaches through calls and static references. A pass written in Camel is compiled as a program
of its own, so naming it in a pipeline loads that pass and adds nothing to the program being run.
The reading is what a reader of the program is handed -- `std::stats` counts the graphs reachable
from the root, and `std::jgir` writes the same set -- so both are read here, on one program, three
ways:

  * the plain pipeline: the graphs and nodes the program has;
  * `std::stats std::nop opt::cse`: the same program, with a pass written in Camel named where it
    never runs (after `std::nop`). Resolving that name imports and compiles the pass's library as
    a program of its own, so this reading says whether those graphs stayed outside this program;
  * the `std::jgir` document: the graphs it names, which must be the same set, and none of them a
    library graph (`opt::`, `rules::`, `types::` are what a pass library's graphs are called).

The comparison is the point, not the absolute numbers: the same numbers twice on a tree that
keeps a pass's program out of the entry program, and 210 against 5 on a tree that does not.
"""
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "test" / "tools"))
from camel_arg import camel_from_argv  # noqa: E402

# The install this case measures: `--camel <path>` names it; without it this tree's
# `out/latest/bin/camel`. One rule, in one place: test/tools/camel_arg.py.
EXE, ARGV = camel_from_argv(ROOT)
SOURCE = str(ROOT / "test/cases/loops/captures.cml")
# The graphs of a library module written in Camel are named after the module that holds them.
LIBRARY_PREFIXES = ("opt::", "rules::", "types::")


def invoke(*args):
    return subprocess.run([EXE, *ARGV, *args], cwd=ROOT, capture_output=True, text=True)


def stats(*named_passes):
    # Why the pipeline is `std::stats std::nop <names>`: a name is resolved (and its module
    # imported) before anything runs, while `std::stats` is the first thing to run -- so the count
    # is of the program the compiler handed over, with the named passes resolved and none executed.
    # `std::nop` is what keeps them from being executed: it empties the graph.
    p = invoke(SOURCE, "std::stats", "std::nop", *named_passes)
    assert p.returncode == 0, p.stderr
    lines = [line for line in p.stdout.splitlines() if line.startswith("GRAPH_STATS ")]
    assert len(lines) == 1, f"expected one GRAPH_STATS line, got {len(lines)}: {p.stdout[:400]}"
    return json.loads(lines[0][len("GRAPH_STATS ") :])


plain = stats()
named = stats("opt::cse")
# The program this case measures: its entry and the one function the entry calls. Pinned, because
# the count is the reading: a pass's 205 library graphs joining the program would show up here as
# 207, and a program that grew a graph for another reason would show up as 3.
assert plain["graphs"] == 2, f"{plain['graphs']} graphs: {plain['graph_names']}"
assert plain["graph_names"] == ["__root__", "main"], plain["graph_names"]
assert named["graphs"] == plain["graphs"], (
    f"naming a Camel pass added {named['graphs'] - plain['graphs']} graph(s) to the program: "
    f"{plain['graphs']} without it, {named['graphs']} with it"
)
assert named["nodes"] == plain["nodes"], (plain["nodes"], named["nodes"])
assert named["graph_names"] == plain["graph_names"], (plain["graph_names"], named["graph_names"])

document = invoke(SOURCE, "std::jgir", "std::nop")
assert document.returncode == 0, document.stderr
written = json.loads(document.stdout)
assert len(written["graphs"]) == plain["graphs"], (len(written["graphs"]), plain["graphs"])
names = [g["id"] for g in written["graphs"]]
for name in names:
    assert not name.startswith(LIBRARY_PREFIXES), f"a library graph is in the program: {name}"

print(
    f"program_graphs: graphs={plain['graphs']} nodes={plain['nodes']} "
    f"named_a_camel_pass={named['graphs']} library_graphs=0"
)
print("Program graphs: PASS")