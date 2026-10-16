"""Exercise control-edge construction, source rewrites, and bounded cleanup through the CLI."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
EXE = ROOT / "out/latest/bin" / ("camel.exe" if os.name == "nt" else "camel")
CASES = ROOT / "test/cases"

def run(source, *passes):
    proc = subprocess.run([str(EXE), str(source), *passes], cwd=ROOT,
                          capture_output=True, text=True, timeout=20)
    assert proc.returncode == 0, proc.stderr
    return proc.stdout

def export(source, *passes):
    return json.loads(run(source, *passes, "std::jgir"))

def redundant(graph):
    kinds = {n["id"]: n["kind"] for n in graph["nodes"]}
    parents = {n: set() for n in kinds}
    for edge in graph["edges"]:
        if edge["kind"] == "control":
            parents[edge["target"]].add(edge["source"])
    found = []
    for target, preds in parents.items():
        for pred in preds:
            seen = set()
            work = list(preds - {pred})
            while work:
                n = work.pop()
                if n == pred:
                    found.append((pred, target))
                    break
                if n in seen or kinds[n] in ("BRCH", "JOIN"):
                    continue
                seen.add(n)
                work.extend(parents[n])
    return found

chain = CASES / "opt/generic/control_chain.cml"
raw = export(chain)
assert all(not redundant(g) for g in raw["graphs"]), "compiler left a redundant control edge"
assert any(n["kind"] == "GATE" for g in raw["graphs"] for n in g["nodes"])
for pipeline in ([], ["std::inline"], ["std::opt"], ["std::opt::ctrl"]):
    for backend in ("std::nvm", "std::fvm", "std::jit", "std::tf", "std::prl"):
        assert run(chain, *pipeline, backend).split() == ["1", "2"]

sample = CASES / "opt/inline/consecutive_sync_calls.cml"
graph = next(g for g in export(sample, "std::opt")["graphs"] if g["name"] == "main")
assert sum(e["kind"] == "control" for e in graph["edges"]) == 4
assert not redundant(graph), "integrated optimizer left transitive controls"

branch = CASES / "opt/progen/findings/inline_untaken_arm_fault.cml"
before = export(branch, "std::inline")
after = export(branch, "std::inline", "std::opt::ctrl")
assert after == export(branch, "std::inline", "std::opt::ctrl", "std::opt::ctrl"), "cleanup is not idempotent"
for old, new in zip(before["graphs"], after["graphs"]):
    assert old["nodes"] == new["nodes"], "cleanup changed nodes or branch metadata"
    assert [e for e in old["edges"] if e["kind"] != "control"] == [
        e for e in new["edges"] if e["kind"] != "control"], "cleanup changed value edges"
    boundaries = {n["id"] for n in old["nodes"] if n["kind"] in ("BRCH", "JOIN")}
    boundary_edges = lambda g: [e for e in g["edges"] if e["source"] in boundaries or e["target"] in boundaries]
    assert boundary_edges(old) == boundary_edges(new), "branch boundary edges changed"
for backend in ("std::nvm", "std::fvm", "std::jit", "std::tf", "std::prl"):
    assert run(branch, "std::inline", "std::opt::ctrl", backend).strip() == ""

with tempfile.TemporaryDirectory() as folder:
    source = Path(folder) / "long_chain.cml"
    source.write_text("module long_chain\nfunc main(): int sync {\n" +
                      "".join(f"println({i})\n" for i in range(256)) + "return 0\n}\n")
    main = next(g for g in export(source)["graphs"] if g["name"] == "main")
    assert sum(e["kind"] == "control" for e in main["edges"]) == 256
print("Control construction, rewrites, branch protection and linear chain: PASS")
