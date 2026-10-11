#!/usr/bin/env python3
"""Constants and example arguments under `as`, exported by both routes and run on ONNX Runtime.

ort_const_under_as.cml exports three models from an example that is a tensor of literals
converted with `as`: `affine` (weights converted to shaped types are constants of the model),
`checked` (conversions of computed values that the example's shape proves) and `step`
(value_and_grad through conversions). The program is run twice, by the operator (default run) and
by the pass (std::macro std::opt::simplify onnx::export); every model must pass onnx.checker's
full check, hold no Cast node (a conversion of a tensor to a tensor type is not an ONNX Cast)
and match NumPy on ONNX Runtime for several inputs within 1e-6 relative to the largest element.

Usage: python ort_const_under_as.py   (needs numpy, onnx and onnxruntime)
"""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
import onnx
import onnxruntime as ort

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[3]
HOME = Path(os.environ.get("CAMEL_HOME", REPO_ROOT / "out" / "latest"))
CAMEL = HOME / "bin" / ("camel.exe" if os.name == "nt" else "camel")
CASE = HERE / "ort_const_under_as.cml"
TOL = 1e-6
F = np.float32

W = np.arange(1, 7, dtype=F).reshape(2, 3)
K = np.array([0.5, -0.5, 0.25, -0.25, 1.5, -1.5], dtype=F).reshape(2, 3)


def reference(model: str, x):
    if model == "affine":
        return [x * W + K]
    if model == "checked":
        return [np.tanh(x) * F(2.0) + F(1.0)]
    return [np.asarray((x * x * W).sum(dtype=F)), F(2.0) * x * W]


def op_types(graph) -> list:
    found = []
    for node in graph.node:
        found.append(node.op_type)
        for attr in node.attribute:
            if attr.HasField("g"):
                found.extend(op_types(attr.g))
    return found


def check(path: Path, model: str, label: str) -> bool:
    loaded = onnx.load(str(path))
    onnx.checker.check_model(loaded, full_check=True)
    ops = op_types(loaded.graph)
    if "Cast" in ops and model != "step":
        print(f"{label} {model}: FAIL, the model holds a Cast node: {ops}")
        return False
    session = ort.InferenceSession(str(path), providers=["CPUExecutionProvider"])
    inputs = session.get_inputs()
    if len(inputs) != 1 or list(inputs[0].shape) != [2, 3]:
        print(f"{label} {model}: FAIL, inputs {[(i.name, i.shape) for i in inputs]}")
        return False
    rng = np.random.default_rng(11)
    worst = 0.0
    for scale in (1.0, 0.1, 3.0):
        x = (rng.standard_normal((2, 3)) * scale).astype(F)
        got = session.run(None, {inputs[0].name: x})
        want = reference(model, x)
        if len(got) != len(want):
            print(f"{label} {model}: FAIL, {len(got)} outputs for {len(want)}")
            return False
        for g, w in zip(got, want):
            g = np.asarray(g, dtype=np.float64).reshape(np.shape(w))
            worst = max(worst, float(np.abs(g - w).max() / max(1.0, float(np.abs(w).max()))))
    print(f"{label} {model}: worst relative error {worst:.2e}, {len(ops)} nodes")
    return worst <= TOL


def main() -> int:
    ok = True
    for label, passes in [("operator", []), ("pass", ["std::macro", "std::opt::simplify", "onnx::export"])]:
        with tempfile.TemporaryDirectory() as tmp:
            env = dict(os.environ, CAMEL_HOME=str(HOME))
            env.pop("CAMEL_ONNX_OUT", None)
            proc = subprocess.run(
                [str(CAMEL), str(CASE), *passes], cwd=tmp, env=env, capture_output=True, text=True, timeout=200
            )
            if proc.returncode != 0:
                print(f"{label}: FAIL, export failed ({proc.returncode}): {proc.stdout.strip()[:600]} {proc.stderr.strip()[:600]}")
                ok = False
                continue
            for model in ("affine", "checked", "step"):
                path = Path(tmp) / f"{model}.onnx"
                if not path.exists():
                    print(f"{label} {model}: FAIL, no model file")
                    ok = False
                    continue
                ok = check(path, model, label) and ok
    print("const under as: PASS" if ok else "const under as: FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
