# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""The three PyTorch counterparts of ``train_step.cml``, plus a float64 reference.

One script, selected by ``--variant``:

  naive      the same model and step as the Camel program, line for line: separate Q/K/V
             projections, ``exp / sum(exp)`` softmax, ``log(softmax)`` cross-entropy.
  expert     stable softmax (``F.scaled_dot_product_attention``), ``F.log_softmax`` for the
             cross-entropy, and one fused QKV projection.
  compiled   ``torch.compile`` of the naive loss (forward and the compiled backward), with the
             same eager Adam step as the other variants.
  reference  the same mathematics in float64, used only as the numerical reference.

The harness runs all four with the same starting weights and data, which the Camel program
writes as .npy files (``NAIVE_INIT_DIR``).  Adam is the same explicit formula in every variant,
computed in float64 and stored in float32, because that is what ``nn.adam`` does; the optimizer
is not one of the differences under test.

Usage:
  python torch_impl.py --variant naive --init artifacts/init --steps 16 --warmup 3
  python torch_impl.py --variant expert --init artifacts/init --save-final artifacts/expert.npz
  python torch_impl.py --variant compiled --init artifacts/init
  python torch_impl.py --variant reference --init artifacts/init --save-final artifacts/ref.npz
"""

from __future__ import annotations

import argparse
import json
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn.functional as F

BATCH = 8
SEQUENCE = 16
D_MODEL = 64
N_HEADS = 4
D_HEAD = 16
D_FF = 128
VOCAB = 32
LR = 0.003
BETA1 = 0.9
BETA2 = 0.999
EPS = 1e-8
NORM_EPS = 1e-6

BLOCKS = ("block0", "block1")
# The order every variant is compared in.  The expert's fused QKV is split back into wq/wk/wv.
CANONICAL = [
    f"{block}.{name}"
    for block in BLOCKS
    for name in ("norm1", "wq", "wk", "wv", "wo", "norm2", "w1", "b1", "w2", "b2")
] + ["norm_f", "head"]


# ---------------------------------------------------------------------------
# Parameters


def load_arrays(directory: Path) -> dict[str, np.ndarray]:
    """Read the .npy leaves the Camel program wrote with nn.save_params."""
    arrays: dict[str, np.ndarray] = {}
    for path in sorted(directory.glob("*.npy")):
        name = path.name[:-4]
        if name.startswith("model."):
            name = name[len("model.") :]
        arrays[name] = np.load(path)
    missing = [name for name in CANONICAL if name not in arrays]
    if missing:
        raise SystemExit(f"{directory}: missing {', '.join(missing)}")
    return arrays


def make_params(arrays: dict[str, np.ndarray], fused: bool, dtype=torch.float32) -> dict:
    params = {}
    for block in BLOCKS:
        for name in ("norm1", "wo", "norm2", "w1", "b1", "w2", "b2"):
            key = f"{block}.{name}"
            params[key] = torch.from_numpy(arrays[key].copy()).to(dtype).requires_grad_(True)
        if fused:
            qkv = np.concatenate(
                [arrays[f"{block}.wq"], arrays[f"{block}.wk"], arrays[f"{block}.wv"]], axis=1
            )
            params[f"{block}.qkv"] = torch.from_numpy(qkv).to(dtype).requires_grad_(True)
        else:
            for name in ("wq", "wk", "wv"):
                key = f"{block}.{name}"
                params[key] = torch.from_numpy(arrays[key].copy()).to(dtype).requires_grad_(True)
    for name in ("norm_f", "head"):
        params[name] = torch.from_numpy(arrays[name].copy()).to(dtype).requires_grad_(True)
    return params


def canonical(params: dict) -> dict[str, torch.Tensor]:
    """Split a fused QKV back into the three projections, so all variants compare elementwise."""
    out = {}
    for block in BLOCKS:
        for name in ("norm1", "wq", "wk", "wv", "wo", "norm2", "w1", "b1", "w2", "b2"):
            key = f"{block}.{name}"
            if key in params:
                out[key] = params[key]
        if f"{block}.qkv" in params:
            q, k, v = params[f"{block}.qkv"].split(D_MODEL, dim=1)
            out[f"{block}.wq"], out[f"{block}.wk"], out[f"{block}.wv"] = q, k, v
    out["norm_f"] = params["norm_f"]
    out["head"] = params["head"]
    return out


# ---------------------------------------------------------------------------
# Model


def rmsnorm(x: torch.Tensor, gain: torch.Tensor) -> torch.Tensor:
    # The mean in float64, as tensor.mean accumulates in f64; the division and scale in the
    # tensor's own width, as the Camel kernel does.
    scale = (x.double() * x.double()).mean(-1, keepdim=True).to(x.dtype)
    return x / torch.sqrt(scale + NORM_EPS) * gain


def split_heads(t: torch.Tensor) -> torch.Tensor:
    return t.reshape(BATCH, SEQUENCE, N_HEADS, D_HEAD).permute(0, 2, 1, 3)


def merge_heads(t: torch.Tensor) -> torch.Tensor:
    return t.permute(0, 2, 1, 3).reshape(BATCH, SEQUENCE, D_MODEL)


def naive_block(params: dict, block: str, x: torch.Tensor) -> torch.Tensor:
    h = rmsnorm(x, params[f"{block}.norm1"])
    q = split_heads(h @ params[f"{block}.wq"])
    k = split_heads(h @ params[f"{block}.wk"])
    v = split_heads(h @ params[f"{block}.wv"])
    scores = (q @ k.transpose(-2, -1)) / (D_HEAD**0.5)
    p = torch.exp(scores) / torch.exp(scores).sum(-1, keepdim=True)
    attended = merge_heads(p @ v) @ params[f"{block}.wo"]
    y = x + attended
    f = F.gelu(rmsnorm(y, params[f"{block}.norm2"]) @ params[f"{block}.w1"] + params[f"{block}.b1"],
               approximate="tanh")
    return y + (f @ params[f"{block}.w2"] + params[f"{block}.b2"])


def expert_block(params: dict, block: str, x: torch.Tensor) -> torch.Tensor:
    h = rmsnorm(x, params[f"{block}.norm1"])
    qkv = h @ params[f"{block}.qkv"]
    q, k, v = qkv.split(D_MODEL, dim=-1)
    q, k, v = split_heads(q), split_heads(k), split_heads(v)
    attended = F.scaled_dot_product_attention(q, k, v, scale=1.0 / (D_HEAD**0.5))
    y = x + merge_heads(attended) @ params[f"{block}.wo"]
    f = F.gelu(rmsnorm(y, params[f"{block}.norm2"]) @ params[f"{block}.w1"] + params[f"{block}.b1"],
               approximate="tanh")
    return y + (f @ params[f"{block}.w2"] + params[f"{block}.b2"])


def logits(params: dict, x: torch.Tensor, expert: bool) -> torch.Tensor:
    block = expert_block if expert else naive_block
    h = block(params, "block0", x)
    h = block(params, "block1", h)
    return rmsnorm(h, params["norm_f"]) @ params["head"]


def naive_loss(params: dict, x: torch.Tensor, labels: torch.Tensor) -> torch.Tensor:
    z = logits(params, x, expert=False)
    p = torch.exp(z) / torch.exp(z).sum(-1, keepdim=True)
    picked = p.gather(-1, labels.unsqueeze(-1)).squeeze(-1)
    return -torch.log(picked).mean()


def expert_loss(params: dict, x: torch.Tensor, labels: torch.Tensor) -> torch.Tensor:
    z = logits(params, x, expert=True)
    logp = F.log_softmax(z, dim=-1)
    return -logp.gather(-1, labels.unsqueeze(-1)).mean()


def reference_loss(params: dict, x: torch.Tensor, labels: torch.Tensor) -> torch.Tensor:
    # The same graph as the naive loss, with the stable softmax and log_softmax of float64.
    z = logits(params, x, expert=False)
    return -F.log_softmax(z, dim=-1).gather(-1, labels.unsqueeze(-1)).mean()


# ---------------------------------------------------------------------------
# The step


def adam_step(params, grads, m, v, step):
    """nn.adam's formula: moments in double, stored in the parameters' width."""
    c1 = 1.0 - BETA1**step
    c2 = 1.0 - BETA2**step
    out, out_m, out_v = {}, {}, {}
    for name, p in params.items():
        dtype = p.dtype
        g = grads[name].double()
        m1 = BETA1 * m[name].double() + (1.0 - BETA1) * g
        v1 = BETA2 * v[name].double() + (1.0 - BETA2) * g * g
        p1 = p.double() - LR * (m1 / c1) / (torch.sqrt(v1 / c2) + EPS)
        out[name] = p1.to(dtype)
        out_m[name] = m1.to(dtype)
        out_v[name] = v1.to(dtype)
    return out, out_m, out_v


def run(loss_fn, params, x, labels, steps):
    m = {name: torch.zeros_like(p) for name, p in params.items()}
    v = {name: torch.zeros_like(p) for name, p in params.items()}
    losses, times = [], []
    for i in range(steps):
        start = time.perf_counter()
        loss = loss_fn(params, x, labels)
        grads = torch.autograd.grad(loss, list(params.values()))
        params, m, v = adam_step(params, dict(zip(params, grads)), m, v, i + 1)
        times.append((time.perf_counter() - start) * 1000.0)
        losses.append(float(loss.detach()))
    with torch.no_grad():
        final_loss = float(loss_fn(params, x, labels))
    return params, losses, times, final_loss


def save_final(path: Path, params: dict) -> None:
    arrays = {name: tensor.detach().cpu().numpy() for name, tensor in canonical(params).items()}
    np.savez(path, **arrays)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--variant", choices=("naive", "expert", "compiled", "reference"),
                        required=True)
    parser.add_argument("--init", required=True, help="directory of .npy leaves from Camel")
    parser.add_argument("--steps", type=int, default=16)
    parser.add_argument("--warmup", type=int, default=3)
    parser.add_argument("--threads", type=int, default=1)
    parser.add_argument("--save-final", default=None)
    args = parser.parse_args()

    torch.set_num_threads(args.threads)
    torch.manual_seed(0)

    arrays = load_arrays(Path(args.init))
    x = torch.from_numpy(np.load(Path(args.init) / "x.npy")).float()
    labels = torch.from_numpy(np.load(Path(args.init) / "labels.npy")).long()

    if args.variant == "reference":
        params = make_params(arrays, fused=False, dtype=torch.float64)
        loss_fn = reference_loss
        init_fn = reference_loss
    elif args.variant == "expert":
        params = make_params(arrays, fused=True)
        loss_fn = expert_loss
        init_fn = expert_loss
    else:
        params = make_params(arrays, fused=False)
        loss_fn = naive_loss
        init_fn = naive_loss
        if args.variant == "compiled":
            loss_fn = torch.compile(loss_fn)

    with torch.no_grad():
        init_loss = float(init_fn(params, x, labels))
    params, losses, times, final_loss = run(loss_fn, params, x, labels, args.steps)

    if args.save_final:
        save_final(Path(args.save_final), params)

    timed = times[args.warmup :] if args.warmup < len(times) else times
    result = {
        "variant": args.variant,
        "torch": torch.__version__,
        "threads": args.threads,
        "steps": args.steps,
        "warmup": args.warmup,
        "init_loss": init_loss,
        "losses": losses,
        "final_loss": final_loss,
        "step_ms": times,
        "median_ms": float(np.median(timed)),
        "mean_ms": float(np.mean(timed)),
    }
    print(f"torch variant={args.variant} init_loss={init_loss:.9f} final_loss={final_loss:.9f} "
          f"median_ms={result['median_ms']:.6f}")
    print("RESULT " + json.dumps(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())