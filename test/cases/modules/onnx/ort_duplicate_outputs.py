#!/usr/bin/env python3
"""One result used as two outputs of a model: two values, two names.

ort_duplicate_outputs.cml exports four models in which the same value is more than one output:
a tensor returned twice, a struct value returned twice (both fields of both), the same constant
returned twice, and one computed scalar returned twice. Structural CSE used to make the second
output reuse the first output's Identity node, so the graph named one value as two outputs.

The program is run twice, by the operator (default run) and by the pass (std::macro std::opt
onnx::export). Every model must pass onnx.checker's full check, give every graph output and
every output ONNX Runtime reports a distinct name, load on ONNX Runtime with as many outputs as
the function returns, and return the values the function computes.

Usage: python ort_duplicate_outputs.py   (needs numpy, onnx and onnxruntime)
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
CASE = HERE / "ort_duplicate_outputs.cml"
TOL = 1e-6
F = np.float32

# Every model's output names, in the order the function returns them.
OUTPUTS = {
    "twice": ["output_0", "output_1"],
    "repeated": ["output_0_state", "output_0_steps", "output_1_state", "output_1_steps"],
    "constants": ["output_0", "output_1"],
    "computed": ["output_0", "output_1"],
}


def reference(model: str, x):
    if model == "twice":
        return [np.tanh(x), np.tanh(x)]
    if model == "repeated":
        return [np.tanh(x), 2, np.tanh(x), 2]
    if model == "constants":
        return [F(1.5), F(1.5)]
    total = x.astype(F).sum(dtype=F)
    return [total, total]


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
    names = [o.name for o in loaded.graph.output]
    if names != OUTPUTS[model]:
        print(f"{label} {model}: FAIL, graph outputs {names}, expected {OUTPUTS[model]}")
        return False
    if len(set(names)) != len(names):
        print(f"{label} {model}: FAIL, duplicate graph output name in {names}")
        return False
    # The defect is visible here even though onnx.checker accepts it: ONNX Runtime reports two
    # outputs with one name, which is the same value twice only by accident.
    session = ort.InferenceSession(str(path), providers=["CPUExecutionProvider"])
    inputs = session.get_inputs()
    if len(inputs) != 1 or list(inputs[0].shape) != [2, 2]:
        print(f"{label} {model}: FAIL, inputs {[(i.name, i.shape) for i in inputs]}")
        return False
    got_names = [o.name for o in session.get_outputs()]
    if got_names != OUTPUTS[model] or len(set(got_names)) != len(got_names):
        print(f"{label} {model}: FAIL, runtime outputs {got_names}")
        return False
    x = np.array([[1.0, -2.0], [0.5, 1.5]], dtype=F)
    got = session.run(None, {inputs[0].name: x})
    want = reference(model, x)
    if len(got) != len(want):
        print(f"{label} {model}: FAIL, {len(got)} outputs for {len(want)}")
        return False
    worst = 0.0
    for g, w in zip(got, want):
        g = np.asarray(g, dtype=np.float64).reshape(np.shape(w))
        worst = max(worst, float(np.abs(g - w).max() / max(1.0, float(np.abs(w).max()))))
    print(f"{label} {model}: worst relative error {worst:.2e}, outputs {got_names}")
    if worst > TOL:
        print(f"{label} {model}: FAIL, worst relative error {worst:.2e}")
        return False
    return True


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
    print("duplicate outputs: PASS" if ok else "duplicate outputs: FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())