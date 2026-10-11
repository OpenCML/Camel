"""Loops over the time axis exported for sequence lengths 1 to 7, with and without std::inline.

ort_time_axis.cml is exported once per length and pipeline; each model is run on ONNX Runtime on
five random inputs of that length and compared with NumPy (1e-5). A model that only works on its
example input, or only for some lengths, fails here: the loops used to evaluate one step past the
end of the data.

Usage: python ort_time_axis.py   (needs numpy, onnx and onnxruntime)
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
PROGRAM = HERE / "ort_time_axis.cml"
TOL = 1e-5


def scan(x):
    h = np.zeros((x.shape[0], x.shape[2]), np.float32)
    for t in range(x.shape[1]):
        if h.sum() > 2.5:
            break
        h = np.tanh(h + x[:, t, :])
    return h


def back(x):
    h = np.zeros((x.shape[0], x.shape[2]), np.float32)
    for t in reversed(range(x.shape[1])):
        h = np.tanh(h * np.float32(0.5) + x[:, t, :])
    return h


def main() -> int:
    import onnx
    import onnxruntime as ort

    ort.set_default_logger_severity(4)
    exe = REPO_ROOT / "out" / "latest" / "bin" / ("camel.exe" if os.name == "nt" else "camel")
    failures, worst = 0, 0.0
    with tempfile.TemporaryDirectory() as t:
        for steps in range(1, 8):
            for passes in ([], ["std::inline"]):
                model = Path(t) / f"m{steps}_{len(passes)}.onnx"
                env = {**os.environ, "ONNX_OUT": str(model), "STEPS": str(steps)}
                env.setdefault("CAMEL_HOME", str(REPO_ROOT / "out" / "latest"))
                proc = subprocess.run([str(exe), str(PROGRAM), *passes], env=env, capture_output=True, text=True)
                label = f"length {steps} [{' '.join(passes) or 'default'}]"
                if proc.returncode != 0:
                    print(f"{label}: export failed: {(proc.stdout + proc.stderr)[-300:]}")
                    failures += 1
                    continue
                onnx.checker.check_model(onnx.load(str(model)), full_check=True)
                sess = ort.InferenceSession(str(model), providers=["CPUExecutionProvider"])
                rng = np.random.default_rng(steps)
                bad, err = 0, 0.0
                for scale in (0.01, 0.1, 0.3, 1.0, 2.0):
                    x = (rng.standard_normal((2, steps, 3)) * scale).astype(np.float32)
                    try:
                        got = sess.run(None, {sess.get_inputs()[0].name: x})
                        err = max(err, float(np.abs(got[0] - scan(x)).max()), float(np.abs(got[1] - back(x)).max()))
                    except Exception as e:  # noqa: BLE001
                        bad += 1
                        first = str(e)[:120]
                worst = max(worst, err)
                if bad or err >= TOL:
                    failures += 1
                    print(f"{label}: {bad} of 5 inputs fail on ONNX Runtime, max abs err {err:.2e}" + (f" ({first})" if bad else ""))
    print(f"ort time axis: 14 models, {failures} failing, worst err {worst:.2e}")
    print("PASS" if failures == 0 else "FAIL")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
