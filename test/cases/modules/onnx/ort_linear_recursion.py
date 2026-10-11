"""Linear recursions exported as paired ONNX Loops, against NumPy.

export_linear_recursion.cml is exported once and run on ONNX Runtime for inputs that give
recursion depths 0, 1 and more; each of the three results must match the recursion written in
NumPy within 1e-5 (relative to the largest element).

Usage: python ort_linear_recursion.py   (needs numpy, onnx and onnxruntime)
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
PROGRAM = HERE / "export_linear_recursion.cml"
TOL = 1e-5
F = np.float32


def pile(h, d):
    if h.sum() < 1.0:
        return h
    d[0] += 1
    return pile(h * F(0.5), d) + h


def fold(i, n, v, w):
    return v if i >= n else np.tanh(fold(i + 1, n, v * F(0.5), w) @ w) * F(i + 1) + v


def fold2(v, w, d):
    if (v * v).sum() > 0.3:
        d[0] += 1
        r = fold2(v * F(0.6), w, d)
        return r @ w + v if v.sum() > 0.0 else r - v
    return v


def main() -> int:
    import onnx
    import onnxruntime as ort

    exe = REPO_ROOT / "out" / "latest" / "bin" / ("camel.exe" if os.name == "nt" else "camel")
    w = np.array([[0.5, -0.25], [0.1, 0.3]], F)
    cases = [
        np.array([[0.1, 0.2], [0.1, 0.2]], F),
        np.array([[1.0, 1.0], [0.0, 0.1]], F),
        np.array([[0.4, 0.3], [0.3, 0.2]], F),
        np.array([[1, 2], [0.5, -1]], F),
        np.array([[-1, -2], [0.5, -1]], F),
        np.array([[1, 2], [3, 4]], F),
    ]
    rng = np.random.default_rng(1)
    cases += [rng.standard_normal((2, 2)).astype(F) * F(s) for s in (0.3, 0.6, 1.0, 2.0, 3.0)]
    worst, depths = 0.0, set()
    with tempfile.TemporaryDirectory() as t:
        model = Path(t) / "linear.onnx"
        env = {**os.environ, "ONNX_OUT": str(model)}
        env.setdefault("CAMEL_HOME", str(REPO_ROOT / "out" / "latest"))
        proc = subprocess.run([str(exe), str(PROGRAM)], env=env, capture_output=True, text=True)
        if proc.returncode != 0:
            raise SystemExit(f"camel failed:\n{proc.stdout}\n{proc.stderr}")
        onnx.checker.check_model(onnx.load(str(model)), full_check=True)
        sess = ort.InferenceSession(str(model), providers=["CPUExecutionProvider"])
        names = [i.name for i in sess.get_inputs()]
        for x in cases:
            got = sess.run(None, {names[0]: x, names[1]: w})
            n = int(F((x * x).sum()) * F(0.5))
            d1, d2 = [0], [0]
            want = [pile(x, d1), fold(0, n, x, w), fold2(x, w, d2)]
            depths |= {d1[0], n, d2[0]}
            err = max(float(np.abs(g - r).max() / max(1.0, np.abs(r).max())) for g, r in zip(got, want))
            worst = max(worst, err)
            print(f"depths {d1[0]} {n} {d2[0]}: max rel err {err:.2e}")
    ok = worst < TOL and {0, 1} <= depths
    print(f"ort linear recursion: worst {worst:.2e}, depths {sorted(depths)}")
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
