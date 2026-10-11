"""A loop returning (state, counter) and its gradient, exported with a dynamic batch axis.

ort_loop_values_dynamic.cml is exported once (example batch 2) and must pass the full check of
onnx.checker with a symbolic first dimension. For batches of 1, 2, 3, 5 and 7 rows on which the
loop takes 0, some and the maximum number of steps, the state, the counter, the loss and the
gradient of the captured weights computed by ONNX Runtime must match (a) NumPy (float64
arithmetic, the number of steps decided in float32) and (b) the native run of the same program,
within 2e-5 relative to the largest element; the counter exactly.

Usage: python ort_loop_values_dynamic.py   (needs numpy, onnx and onnxruntime)
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
PROGRAM = HERE / "ort_loop_values_dynamic.cml"
TOL = 2e-5
LIMIT = 6
W = np.array([[0.5, -0.25], [0.1, 0.3]], np.float32)


def reference(x):
    h, n = x.astype(np.float32), 0
    while not (n >= LIMIT or float((h * h).sum(dtype=np.float32)) < 1.0):
        h = np.tanh(h @ W) * np.float32(0.3) + h * np.float32(0.6)
        n += 1
    w = W.astype(np.float64)
    h = x.astype(np.float64)
    tape = []
    for _ in range(n):
        z = h @ w
        tape.append((h, z))
        h = np.tanh(z) * 0.3 + h * 0.6
    g, dw = 2 * h, np.zeros_like(w)
    for hk, z in reversed(tape):
        dz = g * 0.3 * (1 - np.tanh(z) ** 2)
        dw += hk.T @ dz
        g = dz @ w.T + 0.6 * g
    return n, [h, None, (h * h).sum(), dw]


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

    rng = np.random.default_rng(3)
    worst_ref = worst_native = 0.0
    counters, batches = set(), set()
    ok = True
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        model = tmp / "step.onnx"
        np.save(tmp / "x.npy", rng.standard_normal((2, 2)).astype(np.float32))
        camel(tmp, str(model))
        onnx.checker.check_model(onnx.load(str(model)), full_check=True)
        quiet = ort.SessionOptions()
        quiet.log_severity_level = 3
        sess = ort.InferenceSession(str(model), quiet, providers=["CPUExecutionProvider"])
        dims = sess.get_inputs()[0].shape
        kinds = [o.type for o in sess.get_outputs()]
        print(f"input dims {dims}, outputs {kinds}")
        ok &= isinstance(dims[0], str) and dims[1] == 2 and kinds[1] == "tensor(int64)"
        trials = ((1, 0.3), (1, 1.0), (2, 0.5), (3, 0.4), (3, 1.0), (5, 0.3), (5, 2.0), (7, 0.2), (7, 3.0))
        for batch, scale in trials:
            x = (rng.standard_normal((batch, 2)) * scale).astype(np.float32)
            np.save(tmp / "x.npy", x)
            camel(tmp, "")
            native = [np.load(tmp / f"native_{k}.npy") for k in range(4)]
            got = sess.run(None, {sess.get_inputs()[0].name: x})
            n, want = reference(x)
            counter = int(np.asarray(got[1]).reshape(-1)[0])
            same = counter == n == int(native[1][0])
            e_ref = max(rel(got[k], want[k]) for k in (0, 2, 3))
            e_native = max(rel(got[k], native[k]) for k in (0, 2, 3))
            worst_ref, worst_native = max(worst_ref, e_ref), max(worst_native, e_native)
            counters.add(n)
            batches.add(batch)
            ok &= same and e_ref < TOL and e_native < TOL
            print(
                f"batch {batch}, {n} steps: counter {counter} {'ok' if same else 'MISMATCH'}, "
                f"vs NumPy {e_ref:.2e}, vs native {e_native:.2e}")
    covered = {0, LIMIT} <= counters and any(0 < c < LIMIT for c in counters) and len(batches) >= 4
    print(
        f"ort loop values dynamic: worst vs NumPy {worst_ref:.2e}, vs native {worst_native:.2e}, "
        f"steps {sorted(counters)}, batches {sorted(batches)}")
    print("PASS" if ok and covered else "FAIL")
    return 0 if ok and covered else 1


if __name__ == "__main__":
    sys.exit(main())
