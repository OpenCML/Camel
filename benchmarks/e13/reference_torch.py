# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""E13/T5 Level-2 (data-dependent control flow) probes: PyTorch references.

Mirrors the four programs under benchmarks/e13/ exactly (same literals, same
float32 arithmetic, loss mean(y*y), one SGD step at lr 0.01) and compares the
loss and the updated parameters against the Camel native dumps produced by
MODE=run (gate: max|diff| <= 1e-5). The printed self-check section records the
probe semantics that make each program a Level-2 case: branch-arm decisions,
adaptive iteration counts, and per-step gate patterns.

Usage: python reference_torch.py --artifacts <dir with raw/<probe>/ dumps>
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np

LR = 0.01
GATE = 1e-5


# ---------------------------------------------------------------- L2a: early exit
L2A = {
    "w1": np.array([[0.5, -0.25, 0.75, -1.0], [-0.5, 0.25, 0.5, -0.75], [1.0, 0.5, -0.25, -0.25]], dtype=np.float32),
    "b1": np.array([0.0, 0.0, 0.0, 0.0], dtype=np.float32),
    "w2": np.array([[0.4, -0.3, 0.2, 0.1], [-0.2, 0.5, -0.4, 0.3], [0.3, 0.1, -0.1, 0.6], [-0.5, -0.4, -0.3, -0.2]], dtype=np.float32),
    "b2": np.array([0.5, 0.5, 0.5, 0.5], dtype=np.float32),
    "w3": np.array([[0.3, -0.2, 0.1, 0.4], [0.2, 0.5, -0.3, -0.1], [-0.4, 0.1, 0.6, 0.2], [0.1, -0.5, 0.3, -0.3]], dtype=np.float32),
    "b3": np.array([0.1, -0.1, 0.2, 0.0], dtype=np.float32),
    "wo": np.array([[0.5, -0.5], [1.0, 0.25], [-0.75, 0.5], [0.25, -1.0]], dtype=np.float32),
    "bo": np.array([0.2, -0.1], dtype=np.float32),
    "we": np.array([[1.0, 0.5], [-0.5, 1.0], [0.75, -0.25], [0.0, 0.5]], dtype=np.float32),
    "be": np.array([0.1, 0.2], dtype=np.float32),
}
L2A_TAU = 0.5
L2A_X = {
    "a": np.array([[1.0, 2.0, -1.0], [0.5, -1.5, 2.0]], dtype=np.float32),
    "b": np.array([[-0.5, -0.5, -0.5], [-0.5, -0.5, -0.5]], dtype=np.float32),
    "0": np.array([[0.0, 0.0, 0.0], [0.0, 0.0, 0.0]], dtype=np.float32),
}


def sgd_updated(params):
    """p - LR*grad; params untouched by the taken branch arm have grad None == zero tangent."""
    import torch

    with torch.no_grad():
        return {k: (p - LR * (p.grad if p.grad is not None else torch.zeros_like(p))).detach().numpy() for k, p in params.items()}


def l2a_pieces(p, x):
    import torch

    h1 = torch.relu(x @ p["w1"] + p["b1"])
    h2 = torch.relu(h1 @ p["w2"] + p["b2"])
    c = float(h2.abs().mean().detach())
    if c > L2A_TAU:
        y = h2 @ p["we"] + p["be"]
        arm = "exit"
    else:
        y = torch.relu(h2 @ p["w3"] + p["b3"]) @ p["wo"] + p["bo"]
        arm = "full"
    return y, c, arm


# ---------------------------------------------------------------- L2b: adaptive iteration
L2B = {"w": np.array([0.5], dtype=np.float32)}
L2B_TOL = 1e-3
L2B_X = {"a": np.array([4.0], dtype=np.float32), "b": np.array([100.0], dtype=np.float32), "0": np.array([2.0], dtype=np.float32)}


def l2b_newton(w, x):
    import torch

    t = w * x
    v = torch.ones_like(x)
    iters = 0
    while bool((v * v - t).abs().mean() > L2B_TOL):
        v = 0.5 * (v + t / v)
        iters += 1
    return v, iters


# ---------------------------------------------------------------- L2c: branch experts
L2C = {
    "wa1": np.array([[0.5, 1.0, -0.5, 0.25], [-0.75, 0.5, 1.0, -0.5], [0.25, -1.0, 0.5, 0.75]], dtype=np.float32),
    "ba1": np.array([0.1, 0.0, -0.1, 0.2], dtype=np.float32),
    "wa2": np.array([[0.5, -1.0], [1.0, 0.5], [-0.5, 0.75], [0.25, -0.25]], dtype=np.float32),
    "ba2": np.array([0.0, 0.1], dtype=np.float32),
    "wb": np.array([[1.0, -0.5], [0.5, 1.0], [-0.25, 0.75]], dtype=np.float32),
    "bb": np.array([-0.1, 0.2], dtype=np.float32),
}
L2C_X = {
    "a": np.array([[1.0, 2.0, -0.5], [0.5, 1.5, 0.0]], dtype=np.float32),
    "b": np.array([[-1.0, 0.5, -2.0], [-0.5, 1.0, -1.5]], dtype=np.float32),
    "0": np.array([[1.0, -1.0, 0.5], [-0.5, 0.25, -0.25]], dtype=np.float32),
}


def l2c_pieces(p, x):
    import torch

    r = float(x.mean())
    if r > 0.0:
        y = torch.relu(x @ p["wa1"] + p["ba1"]) @ p["wa2"] + p["ba2"]
        arm = "expertA"
    else:
        y = x @ p["wb"] + p["bb"]
        arm = "expertB"
    return y, r, arm


# ---------------------------------------------------------------- L2d: branch inside the loop (gated GRU)
L2D = {    "w_ir": np.array([[0.5, -0.25, 0.75], [1.0, -0.5, 0.25]], dtype=np.float32),
    "w_iz": np.array([[0.25, 0.5, -0.75], [-0.5, 1.0, 0.5]], dtype=np.float32),
    "w_in": np.array([[0.75, -0.5, 0.5], [0.25, -1.0, 0.5]], dtype=np.float32),
    "w_hr": np.array([[0.5, 0.25, -0.25], [-0.5, 0.75, 0.5], [0.25, -0.75, 1.0]], dtype=np.float32),
    "w_hz": np.array([[0.25, -0.5, 0.5], [0.75, 0.5, -1.0], [0.5, 0.25, -0.25]], dtype=np.float32),
    "w_hn": np.array([[-0.5, 0.5, 0.25], [0.5, -0.75, 1.0], [0.25, 0.5, -0.5]], dtype=np.float32),
    "b_ir": np.array([0.1, 0.0, -0.1], dtype=np.float32),
    "b_iz": np.array([0.0, 0.1, 0.2], dtype=np.float32),
    "b_in": np.array([-0.1, 0.2, 0.0], dtype=np.float32),
    "b_hr": np.array([0.1, 0.1, 0.0], dtype=np.float32),
    "b_hz": np.array([0.0, -0.1, 0.1], dtype=np.float32),
    "b_hn": np.array([0.2, 0.0, -0.2], dtype=np.float32),
    "fc_w": np.array([[0.5, -0.5], [1.0, 0.25], [-0.75, 0.5]], dtype=np.float32),
    "fc_b": np.array([0.3, -0.2], dtype=np.float32),
}
L2D_TAU = 0.5
L2D_X = {
    "a": np.array(
        [[[1.0, -1.0], [0.25, -0.25], [0.75, 0.5]], [[0.5, -0.5], [-0.25, 0.25], [-0.5, 1.0]]], dtype=np.float32
    ),
    "b": np.array(
        [[[1.0, -0.75], [0.75, 1.0], [-1.0, 0.75]], [[0.75, -1.0], [1.0, -0.75], [0.75, 1.0]]], dtype=np.float32
    ),
    "0": np.zeros((2, 3, 2), dtype=np.float32),
}


def l2d_cell(p, xt, h):
    import torch

    r = torch.sigmoid(xt @ p["w_ir"] + p["b_ir"] + h @ p["w_hr"] + p["b_hr"])
    z = torch.sigmoid(xt @ p["w_iz"] + p["b_iz"] + h @ p["w_hz"] + p["b_hz"])
    n = torch.tanh(xt @ p["w_in"] + p["b_in"] + r * (h @ p["w_hn"] + p["b_hn"]))
    return (1.0 - z) * n + z * h


def l2d_pieces(p, x):
    import torch

    h = torch.zeros(x.shape[0], p["w_hr"].shape[0])
    gates = []
    for t in range(x.shape[1]):
        xt = x[:, t, :]
        g = float(xt.abs().mean())
        fire = g > L2D_TAU
        gates.append((round(g, 6), "update" if fire else "skip"))
        if fire:
            h = l2d_cell(p, xt, h)
    return h @ p["fc_w"] + p["fc_b"], gates


# ---------------------------------------------------------------- L2x2: dissection, tensor-arm branch
L2X2 = {
    "w1": np.array([[0.5, -0.25, 0.75], [1.0, -0.5, 0.25]], dtype=np.float32),
    "b1": np.array([0.1, 0.0, -0.1], dtype=np.float32),
    "w2": np.array([[1.0], [-1.0], [0.5]], dtype=np.float32),
    "b2": np.array([0.2], dtype=np.float32),
}
L2X2_X = {
    "a": np.array([[1.0, 2.0], [-1.0, 0.5]], dtype=np.float32),
    "b": np.array([[0.1, -0.1], [0.05, 0.0]], dtype=np.float32),
    "0": np.zeros((2, 2), dtype=np.float32),
}


def l2x2_pieces(p, x):
    import torch

    h = torch.relu(x @ p["w1"] + p["b1"])
    y = h @ p["w2"] + p["b2"]
    s = float((y * y).mean().detach())
    z = y * 2.0 if s > 1.0 else y
    return z, s, "scale2" if s > 1.0 else "identity"


# ---------------------------------------------------------------- generic driver
PARAMS = {"l2a": L2A, "l2b": L2B, "l2c": L2C, "l2d": L2D, "l2x2": L2X2}
INPUTS = {"l2a": L2A_X, "l2b": L2B_X, "l2c": L2C_X, "l2d": L2D_X, "l2x2": L2X2_X}


def torch_step(probe: str, arm: str):
    """One loss + SGD step of `probe` on input `arm`; returns (loss, {name: array}, info)."""
    import torch

    params = {k: torch.from_numpy(v).requires_grad_(True) for k, v in PARAMS[probe].items()}
    x = torch.from_numpy(INPUTS[probe][arm])
    if probe == "l2a":
        y, c, armname = l2a_pieces(params, x)
        info = {"c": c, "arm": armname}
    elif probe == "l2b":
        u, iters = l2b_newton(params["w"], x)
        d = u - 1.0
        info = {"iters": iters, "u": float(u.detach())}
        loss = (d * d).mean()
        if loss.requires_grad:
            loss.backward()
        # depth 0 (residual already <= tol): u is constant ones, the tangent of w is exactly zero
        return float(loss.detach()), sgd_updated(params), info
    elif probe == "l2c":
        y, r, armname = l2c_pieces(params, x)
        info = {"route_mean": r, "arm": armname}
    elif probe == "l2d":
        y, gates = l2d_pieces(params, x)
        info = {"gates": gates}
    elif probe == "l2x2":
        y, s, armname = l2x2_pieces(params, x)
        info = {"s": s, "arm": armname}
    else:
        raise SystemExit(f"unknown probe {probe}")
    loss = (y * y).mean()
    if loss.requires_grad:
        loss.backward()
    # l2d arm 0 (every step skipped) leaves the GRU weights untouched: zero tangents
    return float(loss.detach()), sgd_updated(params), info


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--artifacts", type=Path, required=True, help="e13 artifacts dir containing raw/<probe>/")
    parser.add_argument("--probes", nargs="*", default=["l2a", "l2b", "l2c", "l2d"])
    args = parser.parse_args()

    report = {"gate": GATE, "probes": {}}
    ok_all = True
    for probe in args.probes:
        raw = args.artifacts / "raw" / probe
        entry = {"arms": {}}
        for arm, x in INPUTS[probe].items():
            loss_ref, updated_ref, info = torch_step(probe, arm)
            got_loss = float(np.load(raw / f"loss_{arm}.npy").reshape(-1)[0])
            dloss = abs(got_loss - loss_ref)
            diffs = {}
            for name, ref in updated_ref.items():
                got = np.load(raw / f"arm_{arm}" / f"{name}.npy")
                if got.shape != ref.shape:
                    diffs[name] = f"shape {got.shape} vs {ref.shape}"
                    continue
                diffs[name] = float(np.max(np.abs(got - ref)))
            worst = max(v for v in diffs.values() if isinstance(v, float))
            ok = dloss <= GATE and all(isinstance(v, float) and v <= GATE for v in diffs.values())
            ok_all &= ok
            entry["arms"][arm] = {"info": info, "loss_ref": loss_ref, "loss_native": got_loss, "|dloss|": dloss, "worst_param_diff": worst, "ok": bool(ok)}
            print(f"{probe} arm {arm}: info={info} loss ref={loss_ref:.9g} native={got_loss:.9g} |d|={dloss:.2e} worst param {worst:.2e} {'ok' if ok else 'FAIL'}")
        report["probes"][probe] = entry
    out = args.artifacts / "raw" / "native_vs_torch.json"
    out.write_text(json.dumps(report, indent=2))
    print(f"native vs torch (gate {GATE}):", "PASS" if ok_all else "FAIL", "->", out)
    return 0 if ok_all else 1


if __name__ == "__main__":
    sys.exit(main())
