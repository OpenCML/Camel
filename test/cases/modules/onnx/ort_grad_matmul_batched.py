"""The exported gradient of batched matrix products on doubles, on ONNX Runtime at its defaults.

`export_grad_matmul_batched.cml` differentiates `sum(a @ b * x) + sum((a @ v) ** 2)` for a of
shape (2, 2, 3), b (2, 3, 2), v (3) and x (2, 2, 2). Two of its gradients are transposed products
(`dy @ b^T` and `a^T @ dy`), and the batched-by-vector product contributes a third whose operand
is a rank-2 `a^T @ dy`. onnxruntime-cpu's level-2 MatmulTransposeFusion rewrites a MatMul beside a
Transpose into com.microsoft.FusedMatMul, whose CPU kernel exists for float only, so a model
written that way is valid ONNX and fails to load at the default optimisation level (rule of
2026-10-26, onnx-gradient-width-and-fused-matmul). The lowering writes a rank-2 pair as Gemm and a
batched one as Einsum; this test opens the model with a default InferenceSession (optimisation
ALL) and compares the loss and the three gradients with NumPy's analytic ones, so the comparison
never goes through Camel's own backward pass.

Usage: python ort_grad_matmul_batched.py   (needs numpy, onnx and onnxruntime)
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
PROGRAM = HERE / "export_grad_matmul_batched.cml"
TOL = 1e-11

# The program's own (a, b, v, x), then two more.
CASES = [
    (
        [0.5, -0.25, 0.75, 1.0, -0.5, 0.25, 0.125, -0.375, 0.625, -0.125, 0.875, -0.75],
        [1.0, -1.0, 0.5, 0.25, -0.75, 0.125, 0.375, -0.5, 1.25, 0.75, -0.25, 0.5],
        [0.1, -0.2, 0.3],
        [1.0, 2.0, -1.0, 0.5, 0.25, -0.75, 1.5, -0.5],
    ),
    (
        [0.25, 1.5, -0.75, 0.5, -1.25, 0.875, -0.125, 0.375, 1.0, -0.5, 0.625, -0.25],
        [-0.5, 0.75, 1.25, -1.0, 0.25, 0.5, -0.375, 0.125, -0.875, 1.5, 0.625, -0.25],
        [-0.5, 0.25, 0.75],
        [-1.0, 0.5, 1.25, -0.25, 0.75, -0.5, 0.125, 1.0],
    ),
    (
        [1.0, 0.0, -1.0, 0.5, 0.5, 0.5, -0.25, 0.75, 1.25, -1.5, 0.25, -0.5],
        [0.5, 1.0, -0.5, 0.25, 1.5, -1.25, 0.75, -0.75, 0.25, -0.5, 1.0, 0.125],
        [1.0, -1.0, 0.5],
        [0.5, -0.5, 1.5, 1.0, -1.25, 0.25, -0.75, 0.75],
    ),
]


def expected(a: np.ndarray, b: np.ndarray, v: np.ndarray, x: np.ndarray) -> list:
    """loss and the three gradients of `sum(a @ b * x) + sum((a @ v) ** 2)`, in float64."""
    p = a @ b
    q = a @ v
    loss = np.array([np.sum(p * x) + np.sum(q * q)])
    dp = x
    dq = 2.0 * q
    da = dp @ b.transpose(0, 2, 1) + dq[..., None] * v
    db = a.transpose(0, 2, 1) @ dp
    dv = (a.transpose(0, 2, 1) @ dq[..., None]).sum(axis=0)[:, 0]
    return [loss, da, db, dv]


def by_field(names: list, field: str) -> str:
    for name in names:
        if name == field or name.endswith("_" + field):
            return name
    raise SystemExit(f"no model input or output named {field}: {names}")


def camel(model: Path) -> None:
    exe = REPO_ROOT / "out" / "latest" / "bin" / ("camel.exe" if os.name == "nt" else "camel")
    env = {**os.environ, "ONNX_OUT": str(model)}
    env.setdefault("CAMEL_HOME", str(REPO_ROOT / "out" / "latest"))
    proc = subprocess.run(
        [str(exe), str(PROGRAM)], env=env, capture_output=True, text=True, cwd=REPO_ROOT
    )
    if proc.returncode != 0:
        raise SystemExit(f"export failed:\n{proc.stdout}\n{proc.stderr}")


def main() -> int:
    import onnx
    import onnxruntime as ort

    ort.set_default_logger_severity(4)
    failures = 0
    with tempfile.TemporaryDirectory() as t:
        model = Path(t) / "grad_matmul_batched.onnx"
        camel(model)

        ops = [node.op_type for node in onnx.load(str(model)).graph.node]
        einsums = ops.count("Einsum")
        print(f"ops: {sorted(set(ops))} ({len(ops)} nodes, {einsums} Einsum)")
        # The four transposed products of the backward pass are Einsum nodes; the two forward
        # products keep their MatMul, which the target has no reason to rewrite. What must not
        # be left is a Transpose for a MatMul to swallow.
        if "Transpose" in ops:
            print("FAIL: a Transpose is left beside a MatMul for the target to fuse")
            failures += 1
        if einsums != 4 or ops.count("MatMul") != 2:
            print(f"FAIL: expected 4 Einsum and 2 MatMul, got {einsums} and {ops.count('MatMul')}")
            failures += 1

        # A default SessionOptions is the target's default: graph optimisation ALL, which is
        # where MatmulTransposeFusion lives.
        options = ort.SessionOptions()
        print(f"graph optimisation level {options.graph_optimization_level}")
        if options.graph_optimization_level != ort.GraphOptimizationLevel.ORT_ENABLE_ALL:
            print("FAIL: this test is meant to run at the target's default level")
            failures += 1
        session = ort.InferenceSession(
            str(model), options, providers=["CPUExecutionProvider"]
        )
        inputs = [i.name for i in session.get_inputs()]
        outputs = [o.name for o in session.get_outputs()]
        print(f"inputs {inputs} outputs {outputs}")
        # The struct's fields keep their names (a, b, v); the tensor argument is the one the
        # exporter names by position (input1, after the four field inputs).
        fields = {field: by_field(inputs, field) for field in ("a", "b", "v")}
        rest = [name for name in inputs if name not in fields.values()]
        if len(rest) != 1:
            print(f"FAIL: expected one data input beside a, b and v, got {inputs}")
            return 1
        fields["x"] = rest[0]
        # The gradients are named by the struct's fields; the loss is the output left over, so
        # the comparison is by name and never by the order two lists happen to share.
        gradientNames = [by_field(outputs, field) for field in ("a", "b", "v")]
        lossNames = [name for name in outputs if name not in gradientNames]
        if len(lossNames) != 1:
            print(f"FAIL: expected one loss output beside the three gradients, got {outputs}")
            return 1
        order = lossNames + gradientNames

        for index, (aValues, bValues, vValues, xValues) in enumerate(CASES):
            a = np.array(aValues, np.float64).reshape(2, 2, 3)
            b = np.array(bValues, np.float64).reshape(2, 3, 2)
            v = np.array(vValues, np.float64)
            x = np.array(xValues, np.float64).reshape(2, 2, 2)
            want = expected(a, b, v, x)
            feeds = {name: value for name, value in zip(("a", "b", "v", "x"), (a, b, v, x))}
            feeds = {fields[key]: value for key, value in feeds.items()}
            byName = dict(zip(outputs, session.run(None, feeds)))
            got = [byName[name] for name in order]
            worst = 0.0
            for wanted, array in zip(want, got):
                err = float(np.abs(np.asarray(array, np.float64) - wanted).max())
                worst = max(worst, err)
            ok = worst < TOL
            print(f"case {index}: max abs err {worst:.2e} {'ok' if ok else 'FAIL'}")
            if not ok:
                print(f"  want {[w.tolist() for w in want]}")
                print(f"  got  {[np.asarray(g).tolist() for g in got]}")
            failures += 0 if ok else 1
    print(f"ort grad matmul batched: {failures} failing")
    if failures == 0:
        print("PASS")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())