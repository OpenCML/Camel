"""Runs the exported forward loop on ONNX Runtime and compares it with Camel's own arrays.

The model is `forward_collect.cml`: a loss summed over a loop whose rounds the budget decides.
The same six rows ask for two rounds under a budget of 0.5 and six under 100, so the collected
array -- the Loop's scan output -- is two values long on one call and six on the other. Camel
writes every value it used and computed into the directory this runs in; this runs the exported
model on the same inputs, checks the model with `onnx.checker` first, and compares the scan
output, the round counter and the summed total with Camel's.

Tolerance: both sides compute in f64 and take the same operations in the same order (four
products, a sum, a subtraction, a square, an accumulation), so the only difference is the order
the four products are added in, which is a rounding difference of at most 4 ulp -- 8.9e-16
relative at f64's 2.2e-16 -- and the check asks for 1e-12 relative, four orders tighter than an
f32 that would hide a different reduction order entirely.

Usage: python compare_forward.py   (needs numpy, onnx and onnxruntime)
"""

from __future__ import annotations

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
PROGRAM = HERE / "loop_collect" / "forward_collect.cml"
TOLERANCE = 1e-12


def camel(tmp: Path, model: Path) -> str:
    exe = REPO_ROOT / "out" / "latest" / "bin" / ("camel.exe" if os.name == "nt" else "camel")
    env = {**os.environ, "ONNX_OUT": str(model), "NPY_DIR": str(tmp)}
    env.setdefault("CAMEL_HOME", str(REPO_ROOT / "out" / "latest"))
    proc = subprocess.run([str(exe), str(PROGRAM)], env=env, cwd=tmp, capture_output=True, text=True)
    if proc.returncode != 0:
        raise SystemExit(f"camel failed:\n{proc.stdout}\n{proc.stderr}")
    return proc.stdout


def close(native: np.ndarray, exported: np.ndarray) -> tuple[bool, float]:
    native = np.asarray(native, dtype=np.float64).ravel()
    exported = np.asarray(exported, dtype=np.float64).ravel()
    if native.shape != exported.shape:
        return False, float("inf")
    if native.size == 0:
        return True, 0.0
    scale = np.maximum(np.abs(native), 1.0)
    return bool(np.all(np.abs(native - exported) <= TOLERANCE * scale)), float(
        np.max(np.abs(native - exported) / scale)
    )


def main() -> int:
    ok = True
    with tempfile.TemporaryDirectory() as t:
        tmp, model = Path(t), Path(t) / "forward.onnx"
        stdout = camel(tmp, model)
        print(stdout.strip())
        w = np.load(tmp / "w.npy")
        xs = np.load(tmp / "xs.npy")
        ts = np.load(tmp / "ts.npy")
        losses = {
            "short": np.load(tmp / "losses_short.npy"),
            "long": np.load(tmp / "losses_long.npy"),
        }
        totals = np.load(tmp / "totals.npy")
        rounds = np.load(tmp / "rounds.npy")

        loaded = onnx.load(str(model))
        onnx.checker.check_model(loaded, full_check=True)
        loops = [node for node in loaded.graph.node if node.op_type == "Loop"]
        print(f"model: {len(loaded.graph.node)} nodes, Loop {len(loops)}, onnx.checker full_check ok")
        ok &= len(loops) == 1

        session = ort.InferenceSession(str(model), providers=["CPUExecutionProvider"])
        names = [i.name for i in session.get_inputs()]
        print(f"input names {names} output names {[o.name for o in session.get_outputs()]}")
        # The exported function's parameters in order are `w`, `xs`, `ts` and `budget`; the model
        # names its inputs positionally.
        positional = list(names)

        for which, budget, expected_rounds, expected_total in (
            ("short", 0.5, int(rounds[0]), float(totals[0])),
            ("long", 100.0, int(rounds[1]), float(totals[1])),
        ):
            feeds = dict(
                zip(
                    positional,
                    [w, xs, ts, np.array([budget], dtype=np.float64)],
                )
            )
            got = session.run(None, feeds)
            scan, counter, total = got[0], got[1], got[2]
            same_scan, worst = close(losses[which], scan)
            same_total, worst_total = close([expected_total], [total])
            counted = int(np.asarray(counter).ravel()[0]) == expected_rounds
            print(
                f"budget {budget}: rounds camel {expected_rounds} ort {int(np.asarray(counter).ravel()[0])} "
                f"losses camel {losses[which].shape} {losses[which].tolist()} "
                f"ort {np.asarray(scan).shape} {np.asarray(scan).ravel().tolist()} "
                f"max rel {worst:.3e} | total camel {expected_total!r} ort {float(total)!r} "
                f"max rel {worst_total:.3e} "
                f"{'ok' if same_scan and same_total and counted else 'MISMATCH'}"
            )
            ok &= same_scan and same_total and counted

    print("cc forward ORT:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())