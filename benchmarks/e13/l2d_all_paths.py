"""E13/T5 L2d full-path validation of the exported gated-GRU forward.

The exported l2d_fwd graph is a full binary decision tree over the T = 3 gate
predicates (2^3 - 1 = 7 nested Ifs). The three probe arms exercise only three
of the eight paths; this script drives the ORT graph through all eight gate
patterns and compares each against the PyTorch reference (which ① already
aligned with Camel native), so every leaf path of the decision tree is
validated. Gate pattern bit t = 1 uses |x_t| = 0.75 (update), 0 uses 0.1
(skip).

Usage: python l2d_all_paths.py --artifacts <e13 artifacts dir>
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from reference_torch import L2D, l2d_pieces  # noqa: E402

GATE = 1e-6


def torch_loss(pattern: tuple[int, ...]) -> float:
    import torch

    params = {k: torch.from_numpy(v) for k, v in L2D.items()}
    x = np.zeros((2, 3, 2), dtype=np.float32)
    for t, bit in enumerate(pattern):
        x[:, t, :] = 0.75 if bit else 0.1
        x[1, t, 1] *= -1.0  # mixed signs; the gate uses |x_t|
    y, _ = l2d_pieces(params, torch.from_numpy(x))
    return float(((y * y).mean()).detach())


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--artifacts", type=Path, required=True)
    args = parser.parse_args()

    import onnxruntime as ort

    raw = args.artifacts / "raw" / "l2d"
    session = ort.InferenceSession(str(args.artifacts / "onnx" / "l2d_fwd.onnx"), providers=["CPUExecutionProvider"])
    params = {p.stem: np.load(p) for p in (raw / "init").glob("*.npy")}

    ok_all, results = True, {}
    for mask in range(8):
        pattern = tuple((mask >> t) & 1 for t in range(3))
        x = np.zeros((2, 3, 2), dtype=np.float32)
        for t, bit in enumerate(pattern):
            x[:, t, :] = 0.75 if bit else 0.1
            x[1, t, 1] *= -1.0
        feeds = {i.name: (x if i.name.startswith("input") else params[i.name]) for i in session.get_inputs()}
        got = float(np.asarray(session.run(None, feeds)[0]).reshape(-1)[0])
        ref = torch_loss(pattern)
        d = abs(got - ref)
        ok = d <= GATE
        ok_all &= ok
        results["".join(map(str, pattern))] = {"ort": got, "torch": ref, "|d|": d, "ok": ok}
        print(f"pattern {pattern} ({'U' if any(pattern) else 'all-skip' if mask == 0 else ''}): ort={got:.9g} torch={ref:.9g} |d|={d:.2e} {'ok' if ok else 'FAIL'}")
    out = args.artifacts / "raw" / "l2d_fwd_all_paths.json"
    out.write_text(json.dumps({"gate": GATE, "paths": results}, indent=2))
    print("l2d fwd all 8 paths:", "PASS" if ok_all else "FAIL", "->", out)
    return 0 if ok_all else 1


if __name__ == "__main__":
    sys.exit(main())
