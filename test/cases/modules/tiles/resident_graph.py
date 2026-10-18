"""Check transfer placement and direct resident reuse in the rewritten graph."""
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[4]
EXE = os.environ.get("CAMEL_EXE", str(ROOT / "out/latest/bin" / ("camel.exe" if os.name == "nt" else "camel")))
env = dict(os.environ, CAMEL_ENABLE_TEST_PASSES="1")
p = subprocess.run([EXE, str(Path(__file__).with_name("region_execution.cml")),
                    "tiles::test_outline_resident", "std::jgir"],
                   cwd=ROOT, env=env, capture_output=True, text=True)
assert p.returncode == 0, p.stderr
prefix, payload = p.stdout.split("\n", 1)
assert prefix == "TILES_OUTLINED 10", prefix
graphs = {g["name"]: g for g in json.loads(payload)["graphs"]}

def ops(graph, label):
    return [n for n in graph["nodes"] if n.get("label") == "tiles:" + label]

def incoming(graph, node, kind="normal"):
    return [e["source"] for e in graph["edges"] if e["target"] == node and e["kind"] == kind]

g = graphs["detour"]
assert len(ops(g, "upload")) == len(ops(g, "download")) == len(ops(g, "run_compiled_device")) == 2
nodes = {n["id"]: n for n in g["nodes"]}
calls = ops(g, "run_compiled_device")
consumer = next(n for n in calls if nodes[incoming(g, n["id"])[0]]["kind"] == "FILL")
producer = next(n for n in calls if n != consumer)
packed = incoming(g, consumer["id"])[0]
assert producer["id"] in incoming(g, packed, "context"), "resident edge must bypass host copy"
cpu = next(n for n in g["nodes"] if n.get("label") == "cpu")
host = nodes[incoming(g, cpu["id"])[0]]
assert host["label"] == "tiles:download"
assert incoming(g, host["id"]) == [producer["id"]]
assert any(incoming(g, n["id"]) == [cpu["id"]] for n in ops(g, "upload"))
assert all(nodes[incoming(g, n["id"])[0]].get("label") != "tiles:download" for n in ops(g, "upload"))
for name, uploads, downloads in [("chain", 1, 1), ("shared_values", 1, 2), ("constant_axis", 1, 1)]:
    graph = graphs[name]
    assert len(ops(graph, "upload")) == uploads, name
    assert len(ops(graph, "download")) == downloads, name
    assert len(ops(graph, "run_compiled_device")) == 1, name
print("Resident graph transfers: PASS")
