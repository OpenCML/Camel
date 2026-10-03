"""E1: multi-step fine-tuning driven by a pure ONNX Runtime InferenceSession.

Camel exports a training step as a pure ONNX inference graph: the inputs are
every parameter (by name, e.g. ``fc1_w``) plus the data input, the outputs are
the loss (``output_0``) and every updated parameter (``output_1_<name>``).
This driver feeds the updated parameters back as the next step's inputs, so
N real SGD steps run on an unmodified onnxruntime with no training API and no
framework. Per-step losses go to ``<out>/losses.npy`` and per-step updated
parameters to ``<out>/trace/step_<i>/<name>.npy`` for the three-way alignment.

Usage:
  python driver.py --model mlp --onnx out/mlp_step.onnx \
      --weights ../artifacts/mlp/weights --data out/data/mlp/batches \
      --steps 100 --out out/runs/mlp/ort
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
import onnxruntime as ort


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--model", required=True)
    p.add_argument("--onnx", required=True, help="exported training-step graph")
    p.add_argument("--weights", required=True, help="dir of initial parameter .npy files")
    p.add_argument("--data", required=True, help="dir of pre-split batch_*.npy")
    p.add_argument("--steps", type=int, default=100)
    p.add_argument("--out", required=True, help="output dir for losses.npy and trace/")
    args = p.parse_args()

    sess = ort.InferenceSession(args.onnx, providers=["CPUExecutionProvider"])
    weights = {f.stem: np.load(f) for f in Path(args.weights).glob("*.npy")}
    (data_in,) = [i.name for i in sess.get_inputs() if i.name not in weights]
    params = [i.name for i in sess.get_inputs() if i.name in weights]
    batches = sorted(Path(args.data).glob("batch_*.npy"), key=lambda f: int(f.stem.split("_")[1]))
    out_names = [o.name for o in sess.get_outputs()]
    out_dir = Path(args.out)
    losses = []
    for i in range(args.steps):
        feeds = {**weights, data_in: np.load(batches[i % len(batches)])}
        got = dict(zip(out_names, sess.run(None, feeds)))
        losses.append(float(got["output_0"]))
        weights = {k: np.ascontiguousarray(got[f"output_1_{k}"]) for k in params}
        step_dir = out_dir / "trace" / f"step_{i}"
        step_dir.mkdir(parents=True, exist_ok=True)
        for k in params:
            np.save(step_dir / f"{k}.npy", weights[k])
        print(f"step={i} loss={losses[-1]:.8g}", flush=True)
    np.save(out_dir / "losses.npy", np.asarray(losses, dtype=np.float64))


if __name__ == "__main__":
    main()
