"""The gradient of a data-dependent loop, exported as paired ONNX Loops.

ort_grad_loop.cml (with_grad of two losses over a tail-recursive loop, one with a
data-dependent branch in the loop body) is exported once. For inputs that give loop lengths 0, 1
and more and take both arms of the body's branch, the value and the gradient computed by ONNX
Runtime must match (a) Camel's native run of the same step and (b) a reverse sweep written in
NumPy (float64), within 2e-5 relative to the largest element.

Usage: python ort_grad_loop.py   (needs numpy, onnx and onnxruntime)
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
PROGRAM = HERE / "ort_grad_loop.cml"
TOL = 2e-5


def reference(w, x, branch):
    w = w.astype(np.float64)
    v = x.astype(np.float64)
    n = int(np.float32((x * x).sum()) * np.float32(0.5))
    tape = []
    for _ in range(n):
        if not branch or v.sum() > 0.0:
            z = v @ w
            tape.append((0, v, z))
            v = np.tanh(z) + v * 0.5
        else:
            tape.append((1, v, None))
            v = v * 0.8 - (v @ w) * 0.1
    g = np.ones_like(v)
    dw = np.zeros_like(w)
    for kind, vk, z in reversed(tape):
        if kind == 0:
            dz = g * (1 - np.tanh(z) ** 2)
            dw += vk.T @ dz
            g = dz @ w.T + 0.5 * g
        else:
            dw += vk.T @ (-0.1 * g)
            g = 0.8 * g - 0.1 * (g @ w.T)
    return n, v.sum(), dw, [t[0] for t in tape]


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
    cases = [
        np.array([[0.1, 0.2], [0.1, 0.2]], np.float32),
        np.array([[1.0, 1.0], [0.0, 0.1]], np.float32),
        np.array([[1.0, 2.0], [0.5, -1.0]], np.float32),
        np.array([[-1.0, -2.0], [0.5, -1.0]], np.float32),
        np.array([[2.0, -2.5], [1.5, -1.0]], np.float32),
    ]
    rng = np.random.default_rng(7)
    cases += [rng.standard_normal((2, 2)).astype(np.float32) * s for s in (0.8, 1.2, 1.6, 2.0)]
    worst_ref = worst_native = 0.0
    depths, arms = set(), set()
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        model = tmp / "step.onnx"
        np.save(tmp / "w.npy", w)
        np.save(tmp / "x.npy", cases[2])
        camel(tmp, str(model))
        onnx.checker.check_model(onnx.load(str(model)), full_check=True)
        sess = ort.InferenceSession(str(model), providers=["CPUExecutionProvider"])
        names = [i.name for i in sess.get_inputs()]
        for x in cases:
            np.save(tmp / "x.npy", x)
            camel(tmp, "")
            native = [np.load(tmp / f"native_{k}.npy") for k in range(4)]
            got = sess.run(None, {names[0]: w, names[1]: x})
            n, la, ga, _ = reference(w, x, False)
            _, lb, gb, taken = reference(w, x, True)
            depths.add(n)
            arms |= set(taken)
            e_ref = max(rel(got[0], la), rel(got[1], ga), rel(got[2], lb), rel(got[3], gb))
            e_native = max(rel(got[k], native[k]) for k in range(4))
            worst_ref, worst_native = max(worst_ref, e_ref), max(worst_native, e_native)
            print(f"length {n}, arms {taken}: vs NumPy {e_ref:.2e}, vs native {e_native:.2e}")
    ok = worst_ref < TOL and worst_native < TOL and {0, 1} <= depths and arms == {0, 1}
    print(f"ort grad loop: worst vs NumPy {worst_ref:.2e}, vs native {worst_native:.2e}, lengths {sorted(depths)}")
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
