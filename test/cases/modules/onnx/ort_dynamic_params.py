"""A gradient step with parameters as tensor arguments, exported with dynamic axis 0.

The model of ort_dynamic_params.cml is exported once (example batch 3, which is also the extent
of the weights) and run on ONNX Runtime at batch sizes 1, 2, 3, 4, 5 and 9; the value and both
gradients must match Camel's own native run of the same step within 1e-5. The model's inputs
must not share one symbolic size on axis 0.

Usage: python ort_dynamic_params.py   (needs numpy and onnxruntime)
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
PROGRAM = HERE / "ort_dynamic_params.cml"
BATCHES = [1, 2, 3, 4, 5, 9]
TOL = 1e-5


def camel(env: dict, *passes: str) -> None:
    exe = REPO_ROOT / "out" / "latest" / "bin" / ("camel.exe" if os.name == "nt" else "camel")
    full = {**os.environ, **env}
    full.setdefault("CAMEL_HOME", str(REPO_ROOT / "out" / "latest"))
    proc = subprocess.run([str(exe), str(PROGRAM), *passes], env=full, capture_output=True, text=True)
    if proc.returncode != 0:
        raise SystemExit(f"camel failed:\n{proc.stdout}\n{proc.stderr}")


def main() -> int:
    import onnxruntime as ort

    w = np.array([0.5, -0.25, 0.75, 1.0, -0.5, 0.25, 0.125, 0.375, -0.625], np.float32).reshape(3, 3)
    b = np.array([0.1, -0.2, 0.3], np.float32)
    worst = 0.0
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        model = tmp / "step.onnx"
        camel({"MODE": "export", "ONNX_OUT": str(model)}, "std::macro")
        sess = ort.InferenceSession(str(model), providers=["CPUExecutionProvider"])
        dims = [i.shape[0] for i in sess.get_inputs()]
        print(f"axis-0 sizes of the inputs: {dims}")
        if len(set(dims)) != len(dims):
            print("FAIL: inputs share a symbolic size on axis 0")
            return 1
        np.save(tmp / "w.npy", w)
        np.save(tmp / "b.npy", b)
        rng = np.random.default_rng(11)
        for n in BATCHES:
            x = rng.standard_normal((n, 3)).astype(np.float32)
            np.save(tmp / "x.npy", x)
            env = {"MODE": "run", "W": str(tmp / "w.npy"), "B": str(tmp / "b.npy"), "X": str(tmp / "x.npy")}
            env.update({f"OUT{k}": str(tmp / f"o{k}.npy") for k in range(3)})
            camel(env, "std::macro", "std::nvm")
            got = sess.run(None, dict(zip([i.name for i in sess.get_inputs()], [w, b, x])))
            err = max(
                float(np.abs(np.asarray(g).reshape(-1) - np.load(tmp / f"o{k}.npy").reshape(-1)).max())
                for k, g in enumerate(got)
            )
            worst = max(worst, err)
            print(f"batch {n}: max abs err {err:.2e}")
    print(f"ort dynamic params: worst {worst:.2e}")
    print("PASS" if worst < TOL else "FAIL")
    return 0 if worst < TOL else 1


if __name__ == "__main__":
    sys.exit(main())
