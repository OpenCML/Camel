"""Public pass autoloads Tiles and rewrites without probing CUDA or printing logs."""
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'test' / 'tools'))
from camel_arg import camel_from_argv  # noqa: E402

# `--camel <path>` names the install this case measures: the author's ruling of 2026-10-25 replaced
# the `CAMEL_EXE` environment switch with that argument. Without it the install is
# `$CAMEL_HOME/bin/camel`, else this tree's `out/latest/bin/camel`. One rule, in one place:
# test/tools/camel_arg.py. `ARGV` is this process's arguments without the `--camel` pair.
EXE, ARGV = camel_from_argv(ROOT)
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
