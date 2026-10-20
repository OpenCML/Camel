"""The gradient of tail recursions that carry and return several values, as paired ONNX Loops.

ort_grad_loop_values.cml (with_grad of four losses over loops that return a tensor state
together with an integer counter: as a tuple, with the counter entering the loss as a number, as
a struct, and with two more differentiable carried values, a tensor and a float) is exported
once and must pass onnx.checker's full check. For inputs on which the loop stops after 0, 1 and
several steps and at its maximum count, the values and gradients computed by ONNX Runtime must
match (a) a reverse sweep written in NumPy (float64 arithmetic on the float32 trajectory's
decisions) and (b) Camel's native run of the same step, within 2e-5 relative to the largest
element; the counter, an int64 output of the model, must be exact.

Usage: python ort_grad_loop_values.py   (needs numpy, onnx and onnxruntime)
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
PROGRAM = HERE / "ort_grad_loop_values.cml"
TOL = 2e-5
LIMIT = 6


def steps_of(w, x) -> int:
    """The number of steps, decided in float32 as the program decides it."""
    h, k = x.astype(np.float32), 0
    while not (k >= LIMIT or float((h * h).sum(dtype=np.float32)) < 1.0):
        h = np.tanh(h @ w) * np.float32(0.3) + h * np.float32(0.6)
        k += 1
    return k


def reference(w, x):
    """Values and gradients of the four losses, for the number of steps the program takes."""
    n = steps_of(w, x)
    w = w.astype(np.float64)
    h = x.astype(np.float64)
    acc, e = h * 0.0, 0.25
    tape = []
    for _ in range(n):
        z = h @ w
        tape.append((h, z))
        acc, e = acc + h * h, e * 0.5 + h.sum()
        h = np.tanh(z) * 0.3 + h * 0.6

    def sweep(gh, gacc, ge):
        dw = np.zeros_like(w)
        for hk, z in reversed(tape):
            dz = gh * 0.3 * (1 - np.tanh(z) ** 2)
            dw += hk.T @ dz
            gh = dz @ w.T + 0.6 * gh + gacc * 2 * hk + ge
            ge = 0.5 * ge
        return dw

    zero = np.zeros_like(h)
    scale = 1.0 + 0.5 * n
    return n, [
        (h * h).sum(), sweep(2 * h, zero, 0.0),
        (h * h).sum() * scale, sweep(2 * h * scale, zero, 0.0),
        h.sum(), sweep(np.ones_like(h), zero, 0.0),
        h.sum() + acc.sum() * 0.1 + e * 0.3, sweep(np.ones_like(h), 0.1 * np.ones_like(h), 0.3),
    ]


def camel(cwd: Path, onnx_out: str) -> None:
    exe = REPO_ROOT / "out" / "latest" / "bin" / ("camel.exe" if os.name == "nt" else "camel")
    env = {**os.environ, "ONNX_OUT": onnx_out}
    env.setdefault("CAMEL_HOME", str(REPO_ROOT / "out" / "latest"))
    proc = subprocess.run([str(exe), str(PROGRAM)], cwd=cwd, env=env, capture_output=True, text=True)
    if proc.returncode != 0:
        raise SystemExit(f"camel failed:\n{proc.stdout}\n{proc.stderr}")


def rel(a, b) -> float:
    b = np.asarray(b, np.float64)
    a = np.asarray(a, np.float64).reshape(b.shape)
    return float(np.abs(a - b).max() / max(1.0, np.abs(b).max()))


def main() -> int:
    import onnx
    import onnxruntime as ort

    w = np.array([[0.5, -0.25], [0.1, 0.3]], np.float32)
    base = np.array([[1.0, -2.0], [0.5, 1.5]], np.float32)
    cases = [base * np.float32(s) for s in (0.2, 0.38, 0.45, 0.6, 0.8, 1.0, 4.0)]
    cases.append(np.array([[1.0, 2.0], [3.0, 4.0]], np.float32))
    worst_ref = worst_native = 0.0
    depths = set()
    exact = True
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        model = tmp / "step.onnx"
        np.save(tmp / "w.npy", w)
        np.save(tmp / "x.npy", cases[3])
        camel(tmp, str(model))
        proto = onnx.load(str(model))
        onnx.checker.check_model(proto, full_check=True)
        quiet = ort.SessionOptions()
        quiet.log_severity_level = 3
        sess = ort.InferenceSession(str(model), quiet, providers=["CPUExecutionProvider"])
        names = [i.name for i in sess.get_inputs()]
        kinds = [o.type for o in sess.get_outputs()]
        typed = len(kinds) == 9 and kinds[8] == "tensor(int64)"
        print(f"outputs {kinds}")
        for x in cases:
            np.save(tmp / "x.npy", x)
            camel(tmp, "")
            native = [np.load(tmp / f"native_{k}.npy") for k in range(9)]
            got = sess.run(None, {names[0]: w, names[1]: x})
            n, want = reference(w, x)
            depths.add(n)
            e_ref = max(rel(got[k], want[k]) for k in range(8))
            e_native = max(rel(got[k], native[k]) for k in range(8))
            counter = int(np.asarray(got[8]).reshape(-1)[0])
            same = counter == n == int(native[8][0])
            exact &= same
            worst_ref, worst_native = max(worst_ref, e_ref), max(worst_native, e_native)
            print(
                f"{n} steps: counter {counter} {'ok' if same else 'MISMATCH'}, "
                f"vs NumPy {e_ref:.2e}, vs native {e_native:.2e}")
    covered = {0, 1, LIMIT} <= depths and any(1 < d < LIMIT for d in depths)
    ok = worst_ref < TOL and worst_native < TOL and exact and typed and covered
    print(f"ort grad loop values: worst vs NumPy {worst_ref:.2e}, vs native {worst_native:.2e}, steps {sorted(depths)}")
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
