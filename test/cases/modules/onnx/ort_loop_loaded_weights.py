#!/usr/bin/env python3
"""A data-dependent loop with weights loaded from a file, exported with a dynamic batch axis.

ort_loop_loaded_weights.cml reads its weights with load_npy into a struct the exported function
captures, so their declared type says neither element type nor shape. The model (state, counter,
loss, gradient with respect to the weights) is exported once, must pass onnx.checker's full
check and hold a Loop, and for batches of 1, 2, 3, 5 and 7 rows, on which the loop takes 0, some
and the maximum number of steps, every output on ONNX Runtime must match Camel's native run of
the same program within 2e-5 relative to the largest element (the counter exactly).

The export used to end with "Runtime draft encoding failed to allocate ... bytes".

Usage: python ort_loop_loaded_weights.py   (needs numpy, onnx and onnxruntime)
"""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
import onnx
import onnxruntime as ort

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[3]
HOME = Path(os.environ.get("CAMEL_HOME", REPO_ROOT / "out" / "latest"))
CAMEL = HOME / "bin" / ("camel.exe" if os.name == "nt" else "camel")
CASE = HERE / "ort_loop_loaded_weights.cml"
TOL = 2e-5
W = np.array([[0.5, -0.25], [0.1, 0.3]], np.float32)


def camel(cwd: Path, out: str) -> None:
    env = dict(os.environ, CAMEL_HOME=str(HOME), ONNX_OUT=out)
    proc = subprocess.run([str(CAMEL), str(CASE)], cwd=cwd, env=env, capture_output=True, text=True, timeout=200)
    if proc.returncode != 0:
        raise SystemExit(f"FAIL: camel failed ({proc.returncode}):\n{proc.stdout[:1500]}\n{proc.stderr[:1500]}")


def op_types(graph) -> list:
    found = []
    for node in graph.node:
        found.append(node.op_type)
        for attr in node.attribute:
            if attr.HasField("g"):
                found.extend(op_types(attr.g))
    return found


def main() -> int:
    rng = np.random.default_rng(3)
    ok, worst, counters, batches = True, 0.0, set(), []
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        np.save(tmp / "w.npy", W)
        np.save(tmp / "x.npy", rng.standard_normal((2, 2)).astype(np.float32))
        camel(tmp, str(tmp / "m.onnx"))
        model = onnx.load(str(tmp / "m.onnx"))
        onnx.checker.check_model(model, full_check=True)
        ops = op_types(model.graph)
        if "Loop" not in ops:
            print(f"FAIL: the model holds no Loop: {sorted(set(ops))}")
            ok = False
        options = ort.SessionOptions()
        options.log_severity_level = 3
        session = ort.InferenceSession(str(tmp / "m.onnx"), options, providers=["CPUExecutionProvider"])
        name = session.get_inputs()[0].name
        print("input", session.get_inputs()[0].shape, "nodes", len(ops))
        for batch, scale in ((1, 0.3), (1, 1.0), (2, 0.5), (3, 0.4), (3, 1.0), (5, 0.3), (5, 2.0), (7, 0.2)):
            x = (rng.standard_normal((batch, 2)) * scale).astype(np.float32)
            np.save(tmp / "x.npy", x)
            camel(tmp, "")
            got = session.run(None, {name: x})
            native = [np.load(tmp / f"native_{i}.npy") for i in range(4)]
            errors = [
                float(
                    np.abs(np.asarray(got[i], np.float64).reshape(native[i].shape) - native[i]).max()
                    / max(1.0, float(np.abs(native[i]).max()))
                )
                for i in (0, 2, 3)
            ]
            same = int(np.asarray(got[1]).reshape(-1)[0]) == int(native[1][0])
            counters.add(int(native[1][0]))
            batches.append(batch)
            worst = max(worst, *errors)
            ok = ok and same and max(errors) < TOL
            print(f"  batch {batch}: counter {int(native[1][0])} {'ok' if same else 'MISMATCH'}; worst {max(errors):.2e}")
    if 0 not in counters or 6 not in counters or len(counters) < 3:
        print(f"FAIL: the inputs do not cover 0, some and 6 steps: {sorted(counters)}")
        ok = False
    print(f"ort loop loaded weights: worst {worst:.2e}; batches {sorted(set(batches))}; steps {sorted(counters)}; {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
