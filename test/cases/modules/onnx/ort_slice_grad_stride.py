"""A slice whose step is not 1 on ONNX Runtime: the exported model puts every gradient element at
the position the step names and leaves the rest of the array zero.

`ort_slice_grad_stride.cml` slices with a step of 2, with a negative step of 2, and along the
second axis of a rank-2 value with a negative step whose start the source left out; it exports
the training step of that loss and prints the values its own backward pass gives. The expected
loss and gradients below are the analytic ones -- `d loss / d x = w` at the five positions the
two rank-1 slices take and zero at the one they do not, `d loss / d b = a` at the columns the
rank-2 slice takes -- so the comparison does not go through Camel's own backward pass.

Usage: python ort_slice_grad_stride.py   (needs numpy, onnx and onnxruntime)
"""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[3]
PROGRAM = HERE / "ort_slice_grad_stride.cml"


def expected(w: np.ndarray, x: np.ndarray, a: np.ndarray, b: np.ndarray):
    """What the loss, `d loss / d x` and `d loss / d b` are for the three slices."""
    loss = np.float32(
        np.sum(w[0:5:2] * x[0:5:2])
        + np.sum(w[5:1:-2] * x[5:1:-2])
        + np.sum(a[:, :0:-2] * b[:, :0:-2])
    )
    gradX = np.zeros_like(x)
    gradX[0:5:2] = w[0:5:2]
    gradX[5:1:-2] = w[5:1:-2]
    gradB = np.zeros_like(b)
    gradB[:, :0:-2] = a[:, :0:-2]
    return np.array([loss], np.float32), gradX, gradB


# The program's own four values, a second set, and one whose sliced parts are zero.
CASES = [
    (
        [0.5, 0.25, -1.0, 2.0, 1.5, -0.5],
        [1.0, 2.0, 3.0, 4.0, 5.0, 6.0],
        [1.0, 2.0, 3.0, 4.0, -1.0, -2.0, -3.0, -4.0],
        [0.5, 1.5, 2.5, 3.5, -0.5, -1.5, -2.5, -3.5],
    ),
    (
        [3.0, -2.0, 0.5, 7.0, -1.5, 4.0],
        [0.5, 1.5, -2.5, 4.0, 0.25, -3.0],
        [2.0, -1.0, 0.5, 3.0, 1.0, -2.0, -0.5, 4.0],
        [1.0, 2.0, -1.0, 0.5, 3.0, -0.5, 2.0, -2.0],
    ),
    (
        [0.0, 5.0, 0.0, -5.0, 0.0, 2.0],
        [1.0, -1.0, 2.0, -2.0, 3.0, -3.0],
        [0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0],
        [1.0, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0],
    ),
]


def main() -> int:
    import onnx
    import onnxruntime as ort

    ort.set_default_logger_severity(4)
    exe = REPO_ROOT / "out" / "latest" / "bin" / ("camel.exe" if os.name == "nt" else "camel")
    failures = 0
    with tempfile.TemporaryDirectory() as t:
        path = Path(t) / "slice_grad_stride.onnx"
        env = {**os.environ, "ONNX_OUT": str(path)}
        env.setdefault("CAMEL_HOME", str(REPO_ROOT / "out" / "latest"))
        proc = subprocess.run([str(exe), str(PROGRAM)], env=env, capture_output=True, text=True)
        if proc.returncode != 0 or not path.exists():
            print(f"export failed: {(proc.stdout + proc.stderr)[-400:]}")
            print("ort slice grad stride: 1 failing")
            return 1
        # What the program's own backward pass gave, beside what the model computes below.
        for line in proc.stdout.splitlines():
            if line.strip():
                print(f"camel: {line.strip()}")

        model = onnx.load(str(path))
        kinds = Counter(node.op_type for node in model.graph.node)
        shapes = {
            value.name: [dim.dim_value or dim.dim_param for dim in value.type.tensor_type.shape.dim]
            for value in list(model.graph.input)
            + list(model.graph.output)
            + list(onnx.shape_inference.infer_shapes(model).graph.value_info)
        }
        print(
            f"model: {len(model.graph.node)} nodes, {kinds['ScatterND']} ScatterND, "
            f"{kinds['Pad']} Pad, outputs "
            f"{[(out.name, shapes[out.name]) for out in model.graph.output]}"
        )
        try:
            session = ort.InferenceSession(str(path), providers=["CPUExecutionProvider"])
        except Exception as exc:
            # A broadcast ONNX Runtime refuses is named by its node: the inferred shape of every
            # input says which pair of dimensions it read as different.
            print(f"session load failed: {exc}")
            for node in model.graph.node:
                print(
                    f"  {node.name} {node.op_type} "
                    f"{ {i: shapes.get(i, '?') for i in node.input} } -> "
                    f"{ {o: shapes.get(o, '?') for o in node.output} }"
                )
            print("ort slice grad stride: 1 failing")
            return 1
        names = [entry.name for entry in session.get_inputs()]
        for wValues, xValues, aValues, bValues in CASES:
            w = np.array(wValues, np.float32)
            x = np.array(xValues, np.float32)
            a = np.array(aValues, np.float32).reshape(2, 4)
            b = np.array(bValues, np.float32).reshape(2, 4)
            wantLoss, wantGradX, wantGradB = expected(w, x, a, b)
            gotLoss, gotGradX, gotGradB = session.run(
                None, {names[0]: w, names[1]: x, names[2]: a, names[3]: b}
            )
            ok = (
                np.allclose(gotLoss, wantLoss, atol=1e-6)
                and np.allclose(gotGradX, wantGradX, atol=1e-6)
                and np.allclose(gotGradB, wantGradB, atol=1e-6)
                and gotGradX.dtype == np.float32
                and gotGradB.dtype == np.float32
            )
            print(f"w={wValues} x={xValues}")
            print(f"  loss={gotLoss} gradX={gotGradX} {'ok' if ok else 'FAIL'}")
            print(f"  gradB={gotGradB.tolist()} {'ok' if ok else 'FAIL'}")
            failures += 0 if ok else 1
    print(f"ort slice grad stride: {failures} failing")
    if failures == 0:
        print("PASS")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())