"""Export-time capability check versus load-time failure (exemplar ORT_007).

The same small model, ``relu(int64(x) - 2)``, goes to ONNX Runtime's CPU
provider along two paths:

  Camel    ``onnx.export_model`` checks every node against the provider's
           capability data (modules/onnx/capabilities/onnxruntime-cpu.json).
           Relu on int64 tensors is valid ONNX from opset 14, but the CPU
           provider has no kernel for it at opset 17, so the export fails,
           naming the source position of the ``relu`` call. With the check
           off (target ``'none'``) Camel writes the model, and ONNX Runtime
           refuses to load it: the capability data is what the runtime does.
  PyTorch  ``torch.onnx.export`` writes a model that passes ``onnx.checker``;
           the failure only appears when ONNX Runtime creates a session.

The script reproduces both and exits 0 when the comparison holds (the test
plan runs it). Usage: ``.venv/bin/python capability_demo.py``.
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
CASE = REPO_ROOT / "test" / "cases" / "modules" / "onnx" / "export_capability.cml"
OPSET = 17


def camel_binary() -> Path:
    exe = "camel.exe" if os.name == "nt" else "camel"
    return Path(os.environ.get("CAMEL_BENCH_BIN", REPO_ROOT / "out" / "latest" / "bin" / exe))


def ort_load_error(path: Path) -> str | None:
    import onnxruntime as ort

    try:
        ort.InferenceSession(str(path), providers=["CPUExecutionProvider"])
    except Exception as exc:  # the provider's own error text
        return str(exc).strip().splitlines()[0]
    return None


def camel_path(tmp: Path) -> bool:
    checked, unchecked = tmp / "camel.onnx", tmp / "camel_unchecked.onnx"
    env = {**os.environ, "ONNX_OUT": str(checked), "ONNX_OUT_UNCHECKED": str(unchecked)}
    env.setdefault("CAMEL_HOME", str(REPO_ROOT / "out" / "latest"))
    proc = subprocess.run([str(camel_binary()), str(CASE)], capture_output=True, text=True, env=env)
    out = re.sub(r"\x1b\[[0-9;]*m", "", proc.stdout + proc.stderr)
    found = re.search(r"(has no kernel for Relu on int64 tensors at opset \d+)[^(]*\(at ([^)]+)\)", out)
    ok = proc.returncode != 0 and found is not None and not checked.exists()
    print("camel:  export rejected" if found else "camel:  export NOT rejected")
    if found:
        print(f"        reason: {found.group(1)}")
        print(f"        source: {found.group(2)}")
    load = ort_load_error(unchecked) if unchecked.exists() else "model not written"
    print(f"        same model without the check: ONNX Runtime load -> {load or 'ok'}")
    return ok and load is not None and "NOT_IMPLEMENTED" in load


def torch_path(tmp: Path) -> bool:
    import onnx
    import torch

    class Counts(torch.nn.Module):
        def forward(self, t):
            return torch.relu(t.to(torch.int64) - 2)

    x = torch.tensor([[1.0, 2.0], [3.0, 4.0]])
    path = tmp / "torch.onnx"
    torch.onnx.export(Counts(), (x,), str(path), opset_version=OPSET, dynamo=False)
    onnx.checker.check_model(onnx.load(str(path)), full_check=True)
    ops = [n.op_type for n in onnx.load(str(path)).graph.node]
    print(f"torch:  torch.onnx.export ok (opset {OPSET}, nodes {ops}), onnx.checker ok")
    load = ort_load_error(path)
    print(f"        ONNX Runtime load -> {load or 'ok'}")
    return load is not None and "NOT_IMPLEMENTED" in load


def main() -> int:
    with tempfile.TemporaryDirectory() as tmp:
        camel_ok = camel_path(Path(tmp))
        torch_ok = torch_path(Path(tmp))
    holds = camel_ok and torch_ok
    print("capability comparison:", "REPRODUCED" if holds else "NOT REPRODUCED")
    return 0 if holds else 1


if __name__ == "__main__":
    sys.exit(main())
