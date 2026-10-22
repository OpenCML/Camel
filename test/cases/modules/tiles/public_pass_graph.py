"""Public pass autoloads Tiles and rewrites without probing CUDA or printing logs."""
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[4]
EXE = os.environ.get("CAMEL_EXE", str(ROOT / "out/latest/bin" / ("camel.exe" if os.name == "nt" else "camel")))
env = dict(os.environ, CUDA_VISIBLE_DEVICES="-1")
for name in ("tiles::compile",):
    result = subprocess.run([EXE, "--devices", "cpu", str(Path(__file__).with_name("public_pass.cml")), name, "std::jgir"],
                            cwd=ROOT, env=env, capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    data = json.loads(result.stdout)
    nodes = [node for graph in data["graphs"] for node in graph["nodes"]]
    assert sum(n.get("label") == "tiles:run_preferred_resident" for n in nodes) == 10
    assert any(n.get("label") == "tiles:retain_preferred" for n in nodes)
    assert any(n.get("label") == "tiles:download" for n in nodes)
print("Public Tiles pass: module loading and deferred target selection PASS")
