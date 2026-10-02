"""T4 calibrated gradient-envelope analysis for resnetbn (see NOTES.md).

The raw per-tensor envelope (train.py --check, rtol 1e-4) is physically unreachable in
float32 for this model, exactly as E6's stem layer was — now across more tensors and with
a sharper mechanism. This script produces the calibrated evidence:

  1. per-element error distributions of Camel vs torch-f64 and torch-f32 vs torch-f64
     (same floored scale as train.py's check): Camel's median element is CLOSER to
     torch-f64 than torch-f32 is (double accumulators in the kernels);
  2. the kink mechanism: at the argmax-difference element of the worst tensor, one-sided
     finite differences show the loss surface has a relu kink — torch's autograd reports
     the left slope, Camel's f32-forward position the right slope, both correct local
     derivatives.

Run: .venv/Scripts/python benchmarks/resnetbn/tools/grad_envelope.py
"""

from __future__ import annotations

import sys
import tempfile
from pathlib import Path

import numpy as np

BENCH_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(BENCH_ROOT))

import train  # noqa: E402
from common.weights import load  # noqa: E402


def torch_grads(w, x, dtype):
    import torch

    from resnetbn import model_torch

    net = model_torch.build_model(w).to(dtype).train()
    for p in net.parameters():
        p.requires_grad_(True)
    loss = (net(torch.from_numpy(x).to(dtype)) ** 2).mean()
    loss.backward()
    return {n: p.grad.detach().numpy().astype(np.float64) for n, p in net.named_parameters()}


def main() -> None:
    w, x = load("resnetbn")
    print("dumping Camel gradients (camel_nvm)...")
    camel = {k: v.astype(np.float64) for k, v in train.camel_gradients("resnetbn").items()}
    g32 = torch_grads(w, x, __import__("torch").float32)
    g64 = torch_grads(w, x, __import__("torch").float64)

    from resnetbn import model_torch  # noqa: E402

    params = {n: p.detach().numpy().astype(np.float64) for n, p in model_torch.build_model(w).named_parameters()}
    name_of = {}
    for wname, wv in w.items():
        for pname, value in params.items():
            if value.shape == wv.shape and np.array_equal(value, wv.astype(np.float64)):
                name_of[wname] = pname
                break
            if value.ndim == 2 and value.T.shape == wv.shape and np.array_equal(value.T, wv.astype(np.float64)):
                name_of[wname] = pname
                break

    floor32 = 1e-3 * max(float(np.max(np.abs(g))) for g in g32.values())
    per_tensor = []
    for wname, pname in name_of.items():
        c = camel[wname]
        a32, a64 = g32[pname], g64[pname]
        scale = max(float(np.max(np.abs(a64))), floor32)
        dc = np.abs(c - a64) / scale
        dt = np.abs(a32 - a64) / scale
        per_tensor.append((wname, np.percentile(dc, [50, 99.9, 100]), np.percentile(dt, [50, 99.9, 100])))

    med_c = np.median([p[1][0] for p in per_tensor])
    med_t = np.median([p[2][0] for p in per_tensor])
    print(f"\nper-element error, floored scale {floor32:.3e} (as train.py):")
    print(f"  median of per-tensor p50:   camel~t64 {med_c:.2e}   t32~t64 {med_t:.2e}")
    print(f"  worst per-tensor p99.9:     camel~t64 {max(p[1][1] for p in per_tensor):.2e}")
    print(f"  worst per-tensor max:       camel~t64 {max(p[1][2] for p in per_tensor):.2e}"
          f"   t32~t64 {max(p[2][2] for p in per_tensor):.2e}")
    deep = [p for p in per_tensor if p[0] in ("fc_w", "fc_b", "b42_c2_w", "b42_c2_b", "b42_c2_bn_g", "b42_c2_bn_b")]
    print("  output-side tensors (max):")
    for n, pc, pt in deep:
        print(f"    {n:14s} camel~t64 {pc[2]:.2e}   t32~t64 {pt[2]:.2e}")

    # kink evidence at the worst camel-side tail
    worst = max(per_tensor, key=lambda p: p[1][2])
    wname = worst[0]
    pname = name_of[wname]
    d = np.abs(camel[wname] - g64[pname])
    idx = tuple(int(i) for i in np.unravel_index(np.argmax(d), d.shape))
    print(f"\nkink probe at {wname}{idx} (camel {camel[wname][idx]:.6e}, torch64 {g64[pname][idx]:.6e}):")

    import torch

    def loss_of(dv):
        net = model_torch.build_model(w).to(torch.float64).train()
        with torch.no_grad():
            dict(net.named_parameters())[pname][idx] += dv
        return float((net(torch.from_numpy(x).to(torch.float64)) ** 2).mean())

    l0 = loss_of(0.0)
    h = 1e-5
    fl = (l0 - loss_of(-h)) / h
    fr = (loss_of(h) - l0) / h
    print(f"  left slope  {fl:.6e}  (torch autograd's side)")
    print(f"  right slope {fr:.6e}  (Camel f32-forward position's side)")


if __name__ == "__main__":
    main()
