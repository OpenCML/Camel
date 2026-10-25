#!/usr/bin/env python3
"""Named struct values exported to ONNX, by both routes and run on ONNX Runtime.

ort_struct_casts.cml exports five models from a `Run = { state: Tensor, steps: int }` value
built in a different position each time: an anonymous literal in the declaration's order
(identity, its CAST exports as nothing), one whose fields are written the other way round (a
copy into the declaration's order, exported field by field), one bound with
`let r: Run = ...` (the CAST at a declaration), one passed to a parameter declared `Run` (the
CAST at a call argument), and one on the path where a recursion whose depth depends on the
model input returns (the CAST inside the ONNX Loop). The program is run twice, by the
operator (default run) and by the pass (std::macro std::opt onnx::export); every model must
pass onnx.checker's full check, hold neither a Cast nor a Reshape node (an identity conversion
exports as nothing, a reorder only identities between the initializer's fields; the loop
condition's own Cast aside), name its outputs after the declaration's fields with the counter
an int64, and match NumPy on ONNX Runtime for several inputs (counters exactly) within 1e-6
relative to the largest element.

Usage: python ort_struct_casts.py   (needs numpy, onnx and onnxruntime)
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
CASE = HERE / "ort_struct_casts.cml"
TOL = 1e-6
F = np.float32

# Every model's output names, in order: the field path of each tensor or scalar of the result.
OUTPUTS = {
    "identity": ["output_state", "output_steps"],
    "reordered": ["output_state", "output_steps"],
    "declared": ["output_state", "output_steps"],
    "argument": ["output_0_state", "output_0_steps", "output_1"],
    "walk": ["output_state", "output_steps"],
}

# Inputs on which `walk` stops after 0, 1 and its maximum of 3 steps.
INPUTS = [
    np.array([[0.1, -0.2], [0.3, 0.1]], dtype=F),
    np.array([[1.2, 0.0], [0.0, 0.0]], dtype=F),
    np.array([[3.0, -1.0], [2.0, 1.5]], dtype=F),
]
# The last output of each model for INPUTS: the counter is a constant except in `walk`.
COUNTERS = {
    "identity": [1, 1, 1],
    "reordered": [1, 1, 1],
    "declared": [2, 2, 2],
    "argument": [6, 6, 6],
    "walk": [0, 1, 3],
}


def walk_reference(x):
    """`walk` of the case: shrink until h*h < 1 (counting at most three steps)."""
    h, k = x.astype(F), 0
    while k < 3 and not (h * h).sum() < F(1.0):
        h = np.tanh(h) * F(0.3) + h * F(0.6)
        k += 1
    return h, k


def reference(model: str, x):
    if model == "identity":
        return [np.tanh(x) * F(2.0), 1]
    if model == "reordered":
        return [np.tanh(x) * F(2.0), 1]
    if model == "declared":
        return [np.tanh(x) * F(3.0), 2]
    if model == "argument":
        return [np.tanh(x), 3, 6]
    state, steps = walk_reference(x)
    return [state, steps]


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
    # A conversion that only names the value must leave no node at all: neither the ONNX Cast an
    #  element type conversion would need nor the Reshape a data movement would need. The
    # identity conversion exports as nothing. `walk`'s loop condition is the one place where an
    # ONNX Cast is the operator's own (the counter's comparison), not a struct conversion: check
    # it here rather than forbidding the operator.
    if "Reshape" in ops:
        print(f"{label} {model}: FAIL, the model holds a Reshape node: {ops}")
        return False
    if "Cast" in ops and model != "walk":
        print(f"{label} {model}: FAIL, the model holds a Cast node: {ops}")
        return False
    session = ort.InferenceSession(str(path), providers=["CPUExecutionProvider"])
    inputs = session.get_inputs()
    if len(inputs) != 1 or list(inputs[0].shape) != [2, 2]:
        print(f"{label} {model}: FAIL, inputs {[(i.name, i.shape) for i in inputs]}")
        return False
    outputs = session.get_outputs()
    names = [o.name for o in outputs]
    if names != OUTPUTS[model]:
        print(f"{label} {model}: FAIL, outputs {names}, expected {OUTPUTS[model]}")
        return False
    counter = outputs[-1]
    if counter.type != "tensor(int64)" or list(counter.shape) != []:
        print(f"{label} {model}: FAIL, counter output {counter.name} is {counter.type} {counter.shape}")
        return False
    steps = []
    worst = 0.0
    for x in INPUTS:
        got = session.run(None, {inputs[0].name: x})
        want = reference(model, x)
        if len(got) != len(want):
            print(f"{label} {model}: FAIL, {len(got)} outputs for {len(want)}")
            return False
        steps.append(int(got[-1]))
        for g, w in zip(got[:-1], want[:-1]):
            g = np.asarray(g, dtype=np.float64).reshape(np.shape(w))
            worst = max(worst, float(np.abs(g - w).max() / max(1.0, float(np.abs(w).max()))))
        if steps[-1] != int(want[-1]):
            print(f"{label} {model}: FAIL, counter {steps[-1]}, NumPy {want[-1]}")
            return False
    if steps != COUNTERS[model]:
        print(f"{label} {model}: FAIL, counters {steps}, expected {COUNTERS[model]}")
        return False
    print(f"{label} {model}: worst relative error {worst:.2e}, counters {steps}, {len(ops)} nodes")
    return worst <= TOL


def main() -> int:
    ok = True
    for label, passes in [("operator", []), ("pass", ["std::macro", "std::opt", "onnx::export"])]:
        with tempfile.TemporaryDirectory() as tmp:
            env = dict(os.environ, CAMEL_HOME=str(HOME))
            proc = subprocess.run(
                [str(CAMEL), str(CASE), *passes], cwd=tmp, env=env, capture_output=True, text=True, timeout=200
            )
            if proc.returncode != 0:
                print(f"{label}: FAIL, export failed ({proc.returncode}): {proc.stdout.strip()[:600]} {proc.stderr.strip()[:600]}")
                ok = False
                continue
            for model in OUTPUTS:
                path = Path(tmp) / f"{model}.onnx"
                if not path.exists():
                    print(f"{label} {model}: FAIL, no model file")
                    ok = False
                    continue
                ok = check(path, model, label) and ok
    print("struct casts: PASS" if ok else "struct casts: FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())