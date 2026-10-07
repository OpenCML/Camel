"""Tail recursions that carry and return several values, exported as ONNX Loops.

Each model of ort_loop_values.cml (a tensor state with an integer counter returned as a tuple,
with two returning paths, as a struct, with a third carried tensor, and as a nested tuple with a
carried float and a bool) is exported once and must pass onnx.checker's full check. For inputs
on which the recursion stops after 0, 1 and several steps and at the maximum count, every output
of the model on ONNX Runtime must match (a) the same recursion written in NumPy and (b) Camel's
native run of the same program: tensors and floats within 1e-5 relative to the largest element,
integer counters and bools exactly; the counters must be int64 outputs.

Usage: python ort_loop_values.py   (needs numpy, onnx and onnxruntime)
"""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[3]
PROGRAM = HERE / "ort_loop_values.cml"
TOL = 1e-5
LIMIT = 6
F = np.float32


def step(h):
    return np.tanh(h) * F(0.3) + h * F(0.6)


def norm(h) -> float:
    return float((h * h).sum(dtype=np.float32))


def reference(model: str, x):
    """The outputs of `model` in the order of the ONNX outputs, and the number of steps."""
    h, k = x.astype(np.float32), 0
    acc, e = h * F(0.0), F(0.25)
    while not (k >= LIMIT or norm(h) < 1.0):
        acc, e = acc + h, e * F(0.5) + (h * h).sum(dtype=np.float32)
        h, k = step(h), k + 1
    if model == "pair":
        return [h * F(2.0), np.int64(k)], k
    if model == "tree":
        return ([h * F(3.0), np.int64(k + 100)] if k >= LIMIT else [h, np.int64(k)]), k
    if model == "rec":
        return [h, np.int64(k)], k
    if model == "three":
        return [h, np.int64(k), acc], k
    return [h, np.int64(k), F(e), np.bool_(k >= LIMIT)], k


MODELS = {"pair": 2, "tree": 2, "rec": 2, "three": 3, "nested": 4}


def camel(cwd: Path, model: str, onnx_out: str) -> None:
    exe = REPO_ROOT / "out" / "latest" / "bin" / ("camel.exe" if os.name == "nt" else "camel")
    env = {**os.environ, "MODEL": model, "ONNX_OUT": onnx_out}
    env.setdefault("CAMEL_HOME", str(REPO_ROOT / "out" / "latest"))
    proc = subprocess.run([str(exe), str(PROGRAM)], cwd=cwd, env=env, capture_output=True, text=True)
    if proc.returncode != 0:
        raise SystemExit(f"camel failed ({model}):\n{proc.stdout}\n{proc.stderr}")


def count_ops(graph, counts=None) -> dict:
    """Operator counts of a graph, the bodies of its Loops and the branches of its Ifs included."""
    counts = {} if counts is None else counts
    for node in graph.node:
        counts[node.op_type] = counts.get(node.op_type, 0) + 1
        for attr in node.attribute:
            if attr.HasField("g"):
                count_ops(attr.g, counts)
    return counts


def differs(got, want) -> float:
    """0.0 for an exact match of an integer or bool; the relative error of a float value."""
    want = np.asarray(want)
    got = np.asarray(got)
    if want.dtype.kind in "ib":
        return 0.0 if got.astype(np.int64).reshape(-1).tolist() == want.astype(np.int64).reshape(-1).tolist() else float("inf")
    got = got.astype(np.float64).reshape(want.shape)
    return float(np.abs(got - want).max() / max(1.0, np.abs(want).max()))


def main() -> int:
    import onnx
    import onnxruntime as ort

    base = np.array([[1.0, -2.0], [0.5, 1.5]], np.float32)
    cases = [base * F(s) for s in (0.2, 0.38, 0.45, 0.6, 0.8, 1.0, 4.0)]
    cases.append(np.array([[0.1, 0.0], [0.0, -0.2]], np.float32))
    ok = True
    worst_ref = worst_native = 0.0
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        sessions = {}
        np.save(tmp / "x.npy", cases[3])
        for model in MODELS:
            path = tmp / f"{model}.onnx"
            camel(tmp, model, str(path))
            proto = onnx.load(str(path))
            onnx.checker.check_model(proto, full_check=True)
            ops = count_ops(proto.graph)
            loops = ops.get("Loop", 0)
            quiet = ort.SessionOptions()
            quiet.log_severity_level = 3
            sessions[model] = ort.InferenceSession(str(path), quiet, providers=["CPUExecutionProvider"])
            kinds = [o.type for o in sessions[model].get_outputs()]
            print(f"{model}: {sum(ops.values())} nodes, {loops} Loop, {ops.get('If', 0)} If, outputs {kinds}")
            ok &= loops == 1 and len(kinds) == MODELS[model] and kinds[1] == "tensor(int64)"
            if model == "nested":
                ok &= kinds[2] == "tensor(float)" and kinds[3] == "tensor(bool)"
        depths = set()
        for x in cases:
            np.save(tmp / "x.npy", x)
            camel(tmp, "", "")
            for model, count in MODELS.items():
                sess = sessions[model]
                got = sess.run(None, {sess.get_inputs()[0].name: x})
                want, k = reference(model, x)
                depths.add(k)
                native = [np.load(tmp / f"{model}_{i}.npy") for i in range(count)]
                e_ref = max(differs(got[i], want[i]) for i in range(count))
                e_native = max(differs(got[i], native[i]) for i in range(count))
                exact = int(np.asarray(got[1]).reshape(-1)[0]) == int(np.asarray(want[1])) == int(native[1][0])
                worst_ref, worst_native = max(worst_ref, e_ref), max(worst_native, e_native)
                good = e_ref < TOL and e_native < TOL and exact
                ok &= good
                print(
                    f"{model}, {k} steps: counter {int(np.asarray(got[1]).reshape(-1)[0])}, "
                    f"vs NumPy {e_ref:.2e}, vs native {e_native:.2e} {'ok' if good else 'MISMATCH'}")
    covered = {0, 1, LIMIT} <= depths and any(1 < d < LIMIT for d in depths)
    print(f"ort loop values: worst vs NumPy {worst_ref:.2e}, vs native {worst_native:.2e}, steps {sorted(depths)}")
    print("PASS" if ok and covered else "FAIL")
    return 0 if ok and covered else 1


if __name__ == "__main__":
    sys.exit(main())
