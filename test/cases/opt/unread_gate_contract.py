"""Check that unused gate values do not keep control-only gates alive."""
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
EXE = ROOT / "out/latest/bin" / ("camel.exe" if os.name == "nt" else "camel")
CASE = ROOT / "test/cases/opt/generic/unread_gate.cml"


def run(*passes):
    result = subprocess.run([str(EXE), str(CASE), *passes], cwd=ROOT,
                            capture_output=True, text=True, timeout=20)
    assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout


def main_graph(*passes):
    return next(g for g in json.loads(run(*passes, "std::jgir"))["graphs"]
                if g["name"] == "main")


before = main_graph("std::inline")
assert sum(n["kind"] == "GATE" for n in before["nodes"]) == 4
for pipeline in (("std::inline", "std::opt::dce"), ("std::opt",)):
    graph = main_graph(*pipeline)
    nodes = {n["id"]: n for n in graph["nodes"]}
    gates = [n for n in graph["nodes"] if n["kind"] == "GATE"]
    assert len(gates) == 2, "unread control gates survived"
    assert nodes[graph["exit"]]["kind"] == "GATE", "return gate was removed"
    read_gate = next(n for n in gates if n["id"] != graph["exit"])
    assert any(e["source"] == read_gate["id"] and e["kind"] == "normal"
               for e in graph["edges"]), "gate with a read value was removed"
    assert sum(n["label"] == ":io/println" for n in nodes.values()) == 8, "effect lost"
    assert graph == main_graph(*pipeline, "std::opt::dce"), "DCE did not reach a fixed point"

expected = ["1", "2", "3", "4", "5", "3", "4", "4"]
for pipeline in ((), ("std::inline",), ("std::inline", "std::opt::dce"), ("std::opt",)):
    for backend in ("std::nvm", "std::fvm", "std::jit", "std::tf", "std::prl"):
        assert run(*pipeline, backend).split() == expected, (pipeline, backend)
print("Unread gates removed; effects, read values and return gates preserved: PASS")
