"""The core's own slice exported to ONNX: an in-range run gives the NumPy slice, and every run
Camel faults on must make ONNX Runtime fail instead of clamping.

`ort_core_slice.cml` builds `:op/slice_sub`, the node the `[]` forms produce for any rank and any
axis (design principles 5.4); the tensor module is not on that path. An ONNX Slice clamps a bound
that leaves the axis, where Camel faults (design principles 3.9), so a bound the exporter cannot
prove in range goes through a Gather guard. The rows below separate that guard from a clamp: they
run each model with an axis shorter than the bound says, and one of them feeds a start the data
makes negative, which a plain ONNX Slice would read as a position from the end of the axis.

The step's sign is not a case of its own: the rows also pair each backward slice with the forward
slice that writes the same bounds, and the two must give the positions the NumPy spelling gives.
The end a backward slice does not write is the position before the first element, which the kernel
reads as -1; ONNX reaches that position only through an end below every extent, since it adds the
extent to a negative end and a -1 would therefore land on the axis' last position and leave the
slice empty. `sweep` is that backward slice of an axis whose extent only the run knows.

Each mode's exported model is also printed as the Slice it holds (bounds, step, the shape the
exporter declared and the shape ONNX itself infers): a backward slice whose end was not written
came out empty on ONNX Runtime even though the exporter had declared the result's length right,
because the end it wrote was the axis' last position to ONNX rather than the one before the first.

Usage: python ort_core_slice.py   (needs numpy, onnx and onnxruntime)
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
PROGRAM = HERE / "ort_core_slice.cml"


def reshape_rows(values: np.ndarray) -> np.ndarray:
    """The reshape `rows` in the program makes: the data says how many rows axis 0 has."""
    rows = int(values.sum())
    return values.reshape(rows, int(4.0 / values.sum()))


FOUR = [1.0, 2.0, 3.0, 4.0]
SIX = [1.0, 2.0, 3.0, 4.0, 5.0, 6.0]
EIGHT = [1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0]

# mode, the model input, and the slice of it the run must produce (None: Camel faults there)
CASES = [
    ("both", [1.0, 1.0, 1.0, 1.0], lambda x: reshape_rows(x)[1:3]),
    ("both", [1.0, 1.0, 0.0, 0.0], None),  # the axis has 2 rows, the bound says 3
    ("both", [1.0, 0.0, 0.0, 0.0], None),  # the axis has 1 row
    ("open", [1.0, 1.0, 1.0, 1.0], lambda x: reshape_rows(x)[2:]),
    ("open", [1.0, 1.0, 0.0, 0.0], lambda x: reshape_rows(x)[2:]),  # empty: start = the extent
    ("open", [1.0, 0.0, 0.0, 0.0], None),  # start 2 leaves an axis of 1
    ("start", [1.0, 1.0, 1.0, 1.0], lambda x: x[4:]),  # the start is the extent: empty
    ("start", [1.0, 0.0, 0.0, 0.0], lambda x: x[1:]),
    ("start", [0.0, 0.0, 0.0, 0.0], lambda x: x[:]),
    ("start", [-1.0, -1.0, -1.0, 1.0], None),  # a negative start: Camel faults, ONNX would count
    ("rank2", [1.0, 2.0, 3.0, 4.0], lambda x: x.reshape(2, 2)[1:2]),
    ("known", [1.0, 2.0, 3.0, 4.0], lambda x: x[1:3]),
    ("step", [1.0, 2.0, 3.0, 4.0], lambda x: x[0:4:2]),
    # The step's sign: each backward slice beside the forward slice that writes the same bounds.
    ("negBoth", SIX, lambda x: x[5:1:-2]),
    ("posBoth", SIX, lambda x: x[1:5:2]),
    ("negEnd", FOUR, lambda x: x[3::-2]),
    ("posEnd", FOUR, lambda x: x[3::2]),
    ("negAll", SIX, lambda x: x[::-2]),
    ("posAll", SIX, lambda x: x[::2]),
    ("negRank2", EIGHT, lambda x: x.reshape(2, 4)[:, ::-2]),
    ("posRank2", EIGHT, lambda x: x.reshape(2, 4)[:, ::2]),
    # The reverse the library's `reverse` writes, beside the forward slice of the same bounds.
    ("negOne", SIX, lambda x: x[::-1]),
    ("posOne", SIX, lambda x: x[:]),
    # A backward slice of an axis whose extent only the run knows.
    ("sweep", [1.0, 0.0, 0.0, 0.0], lambda x: reshape_rows(x)[::-2]),
    ("sweep", [1.0, 1.0, 0.0, 0.0], lambda x: reshape_rows(x)[::-2]),
    ("sweep", [1.0, 1.0, 1.0, 1.0], lambda x: reshape_rows(x)[::-2]),
]


def inferred_shape(model: Path) -> list:
    """What ONNX itself infers for the Slice's result.

    ONNX keeps a graph output's declared shape as it stands, so the declaration is removed first:
    the two readings differ exactly where ONNX reads a bound the exporter meant differently, and
    ONNX Runtime follows the inference. This is the shape of the `Slice` node the model holds, not
    the one the exporter declared for the model.
    """
    import onnx
    from onnx import shape_inference

    m = onnx.load(str(model))
    # ONNX keeps a graph output's declared shape as it stands, so the declaration is dropped
    # first: the two readings differ exactly where ONNX reads a bound the exporter meant
    # differently, and ONNX Runtime follows the inference. This is the result shape of the `Slice`
    # node the model holds, not the one the exporter declared for the model.
    m.graph.output[0].type.tensor_type.ClearField("shape")
    dims = shape_inference.infer_shapes(m).graph.output[0].type.tensor_type.shape.dim
    return [d.dim_value if d.HasField("dim_value") else d.dim_param for d in dims]


def slice_readout(model: Path) -> str:
    """The Slice the exported model holds: its bounds and step, and the shape it declares."""
    import onnx
    from onnx import numpy_helper

    m = onnx.load(str(model))
    consts = {i.name: numpy_helper.to_array(i) for i in m.graph.initializer}
    out = m.graph.output[0].type.tensor_type.shape.dim
    shape = [d.dim_value if d.HasField("dim_value") else d.dim_param for d in out]
    parts = []
    for node in m.graph.node:
        if node.op_type != "Slice":
            continue

        def arg(k: int) -> object:
            return consts[node.input[k]].tolist() if node.input[k] in consts else "computed"

        parts.append(f"axes={arg(3)} starts={arg(1)} ends={arg(2)} steps={arg(4)}")
    return f"{'; '.join(parts) or 'no Slice'} -> declared {shape}, inferred {inferred_shape(model)}"


def main() -> int:
    import onnxruntime as ort

    ort.set_default_logger_severity(4)
    exe = REPO_ROOT / "out" / "latest" / "bin" / ("camel.exe" if os.name == "nt" else "camel")
    failures = 0
    with tempfile.TemporaryDirectory() as t:
        for mode in sorted({row[0] for row in CASES}):
            model = Path(t) / f"{mode}.onnx"
            env = {**os.environ, "ONNX_OUT": str(model), "MODE": mode}
            env.setdefault("CAMEL_HOME", str(REPO_ROOT / "out" / "latest"))
            proc = subprocess.run([str(exe), str(PROGRAM)], env=env, capture_output=True, text=True)
            if proc.returncode != 0:
                print(f"{mode}: export failed: {(proc.stdout + proc.stderr)[-300:]}")
                failures += 1
                continue
            print(f"{mode}: Slice {slice_readout(model)}")
            session = ort.InferenceSession(str(model), providers=["CPUExecutionProvider"])
            name = session.get_inputs()[0].name
            for rowMode, values, expected in CASES:
                if rowMode != mode:
                    continue
                x = np.array(values, np.float32)
                want = None if expected is None else np.asarray(expected(x), np.float32)
                try:
                    got = session.run(None, {name: x})[0]
                except Exception:
                    got = None
                if want is None:
                    ok = got is None
                    what = "fails" if ok else f"returned {got} where Camel faults"
                else:
                    ok = got is not None and np.array_equal(got, want)
                    what = f"{got}"
                print(f"{mode} {values}: {what} {'ok' if ok else 'FAIL'}")
                failures += 0 if ok else 1
    print(f"ort core slice: {failures} failing")
    if failures == 0:
        print("PASS")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())