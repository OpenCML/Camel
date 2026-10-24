"""Check the residency conclusion (docs/design/gpu/plan.cn.md 3.2) against the compiled graph.

`TILES_RESIDENCY` is the conclusion; the outlines the same run prints are what today's pass does
with it. The relation this test pins is the one the plan names: a call between two graphs of one
class moves no value (its parameters follow their arguments), while the outline still leaves a
transfer at the callee's entry -- the insertion M5's second half changes. The loop's state stays
on the device, so a round adds no transfer.
"""
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[4]
EXE = os.environ.get(
    "CAMEL_EXE", str(ROOT / "out/latest/bin" / ("camel.exe" if os.name == "nt" else "camel"))
)
CASE = Path(__file__).with_name("resident_loop.cml")

env = dict(os.environ)
run = subprocess.run(
    [EXE, "--devices", "cpu", str(CASE), "tiles::compile", "std::jgir"],
    cwd=ROOT,
    env=env,
    capture_output=True,
    text=True,
)
assert run.returncode == 0, run.stderr or run.stdout

residency = {}
graphs = {}
for line in run.stdout.split("\n"):
    if line.startswith("TILES_RESIDENCY "):
        entry = json.loads(line[len("TILES_RESIDENCY ") :])
        residency[entry["graph"]] = entry
assert residency, "no residency conclusion was printed: " + run.stdout[:400]

step = residency["step"]
assert step["body"] == "device", step
# The call's parameters follow the argument's block: two device graphs, one block, no transfer.
calls = [call for call in step["calls"] if call["callee"] == "twice"]
assert len(calls) == 1, step
assert calls[0] == {
    "callee": "twice",
    "callee_at": "device",
    "caller": "device",
    "transfers": 0,
}, calls[0]
# The loop's state is made on the device and read on the device, so no round transfers anything.
assert step["per_round"] == 0, step
assert step["crossings"] == [], step
# A host scope calling a device graph is the boundary that does transfer, and it transfers once
# for the call, not once per value.
main = residency["main"]
assert main["body"] == "host", main
assert [call for call in main["calls"] if call["callee"] == "step"][0]["transfers"] == 1, main

# What today's outline does with that conclusion: `twice`'s entry still gets a transfer of its
# own, because each outlined region uploads its inputs instead of following the argument's block.
at = next(i for i, line in enumerate(run.stdout.split("\n")) if line.startswith("{"))
document = json.loads("\n".join(run.stdout.split("\n")[at:]))
for graph in document["graphs"]:
    graphs[graph["name"]] = graph
labels = [node.get("label") for node in graphs["twice"]["nodes"]]
assert labels.count("tiles:run_preferred_resident") == 1, labels
assert labels.count("tiles:download") + labels.count("tiles:upload") == 1, labels
print("Resident loop: PASS")