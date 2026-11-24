"""Exercise GIR format semantics and the JSON interchange using real compiled loops."""
import json
import os
import subprocess
import sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "test" / "tools"))
from camel_arg import camel_from_argv  # noqa: E402

# The install this case measures: `--camel <path>` names it; without it this tree's `out/latest/bin/camel`. One rule, in one place: test/tools/camel_arg.py.
EXE, ARGV = camel_from_argv(ROOT)
SOURCE = str(ROOT / "test/cases/loops/captures.cml")
def export(name):
    # Why the pipeline ends in `std::nop`: the case reads the dump this call writes -- a DOT graph
    # for `gir`/`rgir`/`agir`/`argir` and a version 1 JSON document for `jgir` -- and every one of
    # those passes hands the graph on, so the fallback would run `captures.cml` behind the dump.
    # That run is not decoration: its bytes follow the document (`json.loads` below reads them as
    # "Extra data") and its exit code is the one `export` returns. The dump is the subject.
    p = subprocess.run([EXE, SOURCE, "std::" + name, "std::nop"], cwd=ROOT, capture_output=True, text=True)
    assert p.returncode == 0, p.stderr
    return p.stdout
compact, rich = export("gir"), export("rgir")
assert "digraph GraphIR" in compact and "shape=" not in compact
assert "shape=" in rich and "tooltip=" in rich
assert 'role="' in export("agir") and "fillcolor=" not in export("agir")
assert "fillcolor=" in export("argir")
data = json.loads(export("jgir"))
assert data["format"] == "camel.gir" and data["version"] == 1
graphs = {g["id"]: g for g in data["graphs"]}
assert len(graphs) == len(data["graphs"]) and data["root"] in graphs
ids = set()
kinds = set()
for g in graphs.values():
    local = {n["id"] for n in g["nodes"]}
    assert len(local) == len(g["nodes"]) and not ids.intersection(local)
    ids.update(local)
    assert g["exit"] in local and all(d in graphs for d in g["dependencies"])
    for field in ("normalPorts", "contextPorts", "capturePorts"):
        assert all(p in local for p in g[field])
    for n in g["nodes"]:
        kinds.add(n["kind"])
        assert n["kind"] not in ("ARGS", "EXIT"), "visual boundaries are not real nodes"
        assert "outputType" in n
        if n["kind"] == "LOOP":
            # A loop is two regions of its graph and names no graph of its own (principles 3.10):
            # the document carries the regions and no `condition`/`body` graph slot.
            assert n["loop"]["bodyRegion"] != 0
            assert "condition" not in n["loop"] and "body" not in n["loop"]
            assert n["outputType"] in ("int", "(int, int)")
        if n.get("callee") is not None:
            assert n["callee"] in graphs
        if "source" in n:
            assert n["source"]["file"].endswith("captures.cml")
            assert n["source"]["start"]["line"] >= 0
        if "origin" in n:
            assert isinstance(n["origin"], str) and n["origin"].isdigit()
    for e in g["edges"]:
        assert e["source"] in local and e["target"] in local
        # The edge kinds the graph IR defines (docs/design/effects/edges.cn.md 1): the data kinds
        # by port role (normal/context), the sync edge that joins a `sync` block's statements, and
        # the order edge that only orders. `control` is what the transition still writes beside a
        # chain link's sync edge, and it goes away with it.
        assert e["kind"] in ("normal", "context", "control", "sync", "order")
        # A sync and an order edge carry no data: they name their two ends and no port, so the
        # document has no port indices for them.
        if e["kind"] in ("normal", "context", "control"):
            assert e["sourceIndex"] >= 0 and e["targetIndex"] >= 0
        else:
            assert "sourceIndex" not in e and "targetIndex" not in e
assert {"LOOP", "DATA", "OPER", "PORT"}.issubset(kinds)
# Why the pipeline ends in `std::nop`: the document is what is read here (the duplicate operands
# are asserted on its edges), and `std::jgir` hands the graph on, so the fallback would run
# `jgir_duplicate.cml` behind it -- measured as exit 4 and `json.loads` "Extra data".
duplicate = subprocess.run([EXE, str(ROOT / "test/cases/trans/jgir_duplicate.cml"), "std::jgir", "std::nop"], cwd=ROOT, capture_output=True, text=True)
assert duplicate.returncode == 0, duplicate.stderr
found_duplicate = False
for g in json.loads(duplicate.stdout)["graphs"]:
    for n in g["nodes"]:
        if n["kind"] == "OPER" and n["label"] == ":op/add_l":
            inputs = [e for e in g["edges"] if e["target"] == n["id"] and e["kind"] == "normal"]
            assert len(inputs) == 2 and inputs[0]["source"] == inputs[1]["source"]
            assert {e["targetIndex"] for e in inputs} == {0, 1}
            assert {e["sourceIndex"] for e in inputs} == {0}
            found_duplicate = True
assert found_duplicate
for retired in ("dot", "rdot", "graphviz", "readable_graphviz", "annotated_graphviz", "readable_annotated_graphviz"):
    # No `std::nop` here, and the reason is the case: what is asserted is that an unknown pass is
    # refused (`returncode != 0`), not a dump. Nothing is handed on, so there is no run to stop --
    # and a `std::nop` after a name the compiler rejects would read as if this pipeline printed
    # something to keep.
    p = subprocess.run([EXE, SOURCE, "std::" + retired], cwd=ROOT, capture_output=True, text=True)
    assert p.returncode != 0, retired

# A library module has no `main`. Translating it is not executing it, so every graph dump
# must work; only a run still requires an entry (acceptance for the no-main library fix).
LIBRARY = str(ROOT / "test/cases/trans/library_module.cml")
KNOWN_KINDS = {
    "DATA", "PORT", "CAST", "COPY", "FILL", "ACCS",
    "BRCH", "JOIN", "CALL", "FUNC", "OPER", "GATE", "LOOP",
}
def invoke(*args):
    return subprocess.run([EXE, *args], cwd=ROOT, capture_output=True, text=True)

# Why the two pipelines below end in `std::nop`: a library has no `main`, and translating it is not
# executing it -- but `std::gir`/`std::jgir` hand the graph on, so the fallback would try to run the
# module, fail with `EntryModuleMissingMain` and make the dump's own exit code that failure. The
# dumps are what is asserted here; the run of a library is asserted separately, further down.
lib_dot = invoke(LIBRARY, "std::gir", "std::nop")
assert lib_dot.returncode == 0, lib_dot.stderr
assert "digraph GraphIR" in lib_dot.stdout and lib_dot.stdout.strip()

lib_json = invoke(LIBRARY, "std::jgir", "std::nop")
assert lib_json.returncode == 0, lib_json.stderr
library = json.loads(lib_json.stdout)
assert library["format"] == "camel.gir" and library["version"] == 1
assert library["graphs"], "a library module still has graphs"
lib_graphs = {g["id"]: g for g in library["graphs"]}
assert len(lib_graphs) == len(library["graphs"])
assert library["root"] in lib_graphs
lib_node_ids = set()
for g in lib_graphs.values():
    local = {n["id"] for n in g["nodes"]}
    assert len(local) == len(g["nodes"]) and not lib_node_ids.intersection(local)
    lib_node_ids.update(local)
    assert g["exit"] in local, "every graph has a real exit node"
    assert all(d in lib_graphs for d in g["dependencies"])
    assert all(n["kind"] in KNOWN_KINDS for n in g["nodes"])
# The front end requires every graph to be reachable from root through dependencies.
reachable, todo = set(), [library["root"]]
while todo:
    gid = todo.pop()
    if gid in reachable:
        continue
    reachable.add(gid)
    todo.extend(lib_graphs[gid]["dependencies"])
assert reachable == set(lib_graphs), "every library graph is reachable from root"

lib_inspect = invoke("inspect", "-g", LIBRARY)
assert lib_inspect.returncode == 0, lib_inspect.stderr
assert "digraph GraphIR" in lib_inspect.stdout and lib_inspect.stdout.strip()

lib_run = invoke(LIBRARY)
assert lib_run.returncode != 0, "executing a library module still requires main"
assert "EntryModuleMissingMain" in (lib_run.stdout + lib_run.stderr)
# The entry requirement is not about compilation: naming a scheduler is still a run.
for scheduler in ("std::fvm", "std::jit"):
    scheduled = invoke(LIBRARY, scheduler)
    assert scheduled.returncode != 0, scheduler
    assert "EntryModuleMissingMain" in (scheduled.stdout + scheduled.stderr), scheduler

# `check` analyses a module without entering it: a library passes it. A program with main
# keeps the same behaviour.
lib_check = invoke("check", LIBRARY)
assert lib_check.returncode == 0, (lib_check.stdout + lib_check.stderr)
assert "EntryModuleMissingMain" not in (lib_check.stdout + lib_check.stderr)
main_check = invoke("check", SOURCE)
assert main_check.returncode == 0, (main_check.stdout + main_check.stderr)

print("Library module GIR without main: PASS")
print("GIR formats and JSON v1: PASS")
