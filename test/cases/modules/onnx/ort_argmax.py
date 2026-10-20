"""argmax exported to ONNX follows design principles 7.7 on ONNX Runtime.

ort_argmax.cml exports argmax over axis 1, over axis 1 with keepdims and over axis 0, from a
float input and (MODE=int) from an int64 input. Camel's rule: the position of the first NaN
when there is one, else the first position of the largest value by IEEE 754-2019 maximum
(-0 < +0), the first such position on ties. ONNX ArgMax takes the first index on ties with
select_last_index=0, but it orders -0 and +0 the same and does not state its NaN behavior, so
the model composes the rule; this script checks it against the rule written in NumPy, on the
cases that separate the rules (NaN, all NaN, ties, +0 against -0, negative values, another
axis) and on random inputs with NaNs and signed zeros.

Usage: python ort_argmax.py   (needs numpy, onnx and onnxruntime)
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
PROGRAM = HERE / "ort_argmax.cml"


def reference(a: np.ndarray, axis: int, keepdims: bool) -> np.ndarray:
    """argmax by design principles 7.7 (integers: the first maximum)."""
    a = np.asarray(a)
    ax = axis if axis >= 0 else axis + a.ndim
    moved = np.moveaxis(a, ax, -1)
    flat = moved.reshape(-1, moved.shape[-1])
    out: list[int] = []
    for row in flat:
        if np.issubdtype(a.dtype, np.integer):
            out.append(int(np.argmax(row)))  # first maximum
            continue
        nans = [i for i, v in enumerate(row) if np.isnan(v)]
        if nans:
            out.append(nans[0])
            continue
        best = 0
        for i in range(1, len(row)):
            v, b = row[i], row[best]
            # IEEE maximum: NaN would win (handled above), and +0 is above -0.
            if v > b or (v == b and v == 0 and np.signbit(b) and not np.signbit(v)):
                best = i
        out.append(best)
    shape = list(a.shape)
    if keepdims:
        shape[ax] = 1
    else:
        del shape[ax]
    return np.array(out, np.int64).reshape(shape)


def export(exe: Path, mode: str, model: Path) -> object | None:
    env = {**os.environ, "ONNX_OUT": str(model), "MODE": mode}
    env.setdefault("CAMEL_HOME", str(REPO_ROOT / "out" / "latest"))
    proc = subprocess.run([str(exe), str(PROGRAM)], env=env, capture_output=True, text=True)
    if proc.returncode != 0:
        print(f"{mode}: export failed: {(proc.stdout + proc.stderr)[-400:]}")
        return None
    import onnxruntime as ort

    return ort.InferenceSession(str(model), providers=["CPUExecutionProvider"])


def check(session, mode: str, x: np.ndarray, label: str) -> int:
    name = session.get_inputs()[0].name
    got = session.run(None, {name: x})
    wants = [reference(x, 1, False), reference(x, 1, True), reference(x, 0, False)]
    failures = 0
    for i, (want, out) in enumerate(zip(wants, got)):
        ok = out.shape == want.shape and np.array_equal(out, want)
        if not ok:
            print(f"{mode} {label} output {i}: got {out.tolist()} want {want.tolist()} FAIL")
            failures += 1
    if failures == 0:
        print(f"{mode} {label}: ok")
    return failures


FLOAT_CASES = [
    ("tie", [[1.0, 3.0, 3.0, -2.0, -2.0, 7.0, 7.0, 0.5],
             [-1.0, -1.0, -1.0, -1.0, -1.0, -1.0, -1.0, -1.0]]),
    ("nan", [[np.nan, 5.0, np.nan, -9.0, 5.0, 5.0, 5.0, 5.0],
             [-2.0, np.nan, np.nan, np.nan, -2.0, -2.0, -2.0, -2.0]]),
    ("signed zero", [[-0.0, 0.0, -0.0, 0.0, -1.0, -1.0, -1.0, -1.0],
                     [-0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0]]),
    ("all nan", [[np.nan] * 8, [1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0]]),
    ("negatives", [[-5.0, -3.0, -7.0, -3.0, -3.0, -8.0, -9.0, -1.0],
                   [-0.5, -0.25, -0.25, -1.0, -1.0, -1.0, -1.0, -1.0]]),
    ("plus zero after minus zero", [[2.0, -0.0, 0.0, 2.0, -0.0, 0.0, 1.0, 1.0],
                                    [0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0]]),
    ("infinity", [[float("inf"), 1.0, 1.0, -float("inf"), 0.0, 0.0, 0.0, 0.0],
                  [-float("inf")] * 8]),
    ("equal", [[1.5] * 8, [1.5] * 8]),
]


def main() -> int:
    import onnxruntime as ort

    ort.set_default_logger_severity(4)
    exe = REPO_ROOT / "out" / "latest" / "bin" / ("camel.exe" if os.name == "nt" else "camel")
    rng = np.random.default_rng(11)
    failures = 0
    with tempfile.TemporaryDirectory() as t:
        session = export(exe, "float", Path(t) / "float.onnx")
        if session is None:
            return 1
        for label, rows in FLOAT_CASES:
            failures += check(session, "float", np.array(rows, np.float32), label)
        for trial in range(40):
            x = rng.integers(-3, 4, size=(2, 8)).astype(np.float32)
            mask = rng.random(x.shape) < 0.2
            x[mask] = np.nan
            zeros = rng.random(x.shape) < 0.3
            x[zeros] = rng.choice([0.0, -0.0], size=int(zeros.sum()))
            failures += check(session, "float", x, f"random {trial}")

        session = export(exe, "int", Path(t) / "int.onnx")
        if session is None:
            return 1
        int_cases = [
            ("tie", [[3, 7, 7, 1, 3, 7, 7, 1], [-1, -1, -1, -1, -1, -1, -1, -1]]),
            ("negatives", [[-5, -3, -7, -3, -2, -2, -9, -2], [-2, -2, -9, -2, -2, -2, -9, -2]]),
            ("equal", [[4, 4, 4, 4, 4, 4, 4, 4], [4, 4, 4, 4, 4, 4, 4, 4]]),
        ]
        for label, rows in int_cases:
            failures += check(session, "int", np.array(rows, np.int64), label)
        for trial in range(20):
            x = rng.integers(-4, 5, size=(2, 8)).astype(np.int64)
            failures += check(session, "int", x, f"random {trial}")
    print(f"ort argmax: {failures} failing")
    if failures == 0:
        print("PASS")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())