"""Pin down PyTorch's training-mode BatchNorm semantics empirically (T4).

Answers, with numbers (torch 2.14.0+cpu):
  1. normalize uses the BIASED batch variance (divide by M = N*H*W) — the
     unbiased variant is the control that must FAIL;
  2. the running_var update uses the UNBIASED (M/(M-1)) batch variance — the
     biased variant is the control that must FAIL (the famous torch detail);
  3. running <- (1-momentum)*running + momentum*batch_stat, momentum 0.1, eps 1e-5;
  4. gradients: dbeta = sum(dy), dgamma = sum(dy*xhat),
     dx = gamma/sqrt(var+eps) * (dy - mean(dy) - xhat*mean(dy*xhat)),
     all per channel over (batch, spatial);
  5. running stats are buffers: no gradient, the update is not differentiated;
  6. one value per channel (M == 1) raises ValueError.

NOTE on the loss: sum(y^2) is degenerate through batch_norm — per channel
y = gamma*xhat + beta with sum(xhat)=0 and sum(xhat^2)=M exactly, so the loss
is mathematically constant in x (dx is f32 noise). The gradient checks below
therefore use loss = sum(y * r) with a fixed random r.

Run: .venv/Scripts/python benchmarks/resnetbn/tools/bn_semantics.py
"""

from __future__ import annotations

import numpy as np
import torch
from torch import nn

torch.manual_seed(7)

N, C, H, W = 4, 3, 5, 2
M = N * H * W  # values per channel
EPS = 1e-5
MOMENTUM = 0.1

x_np = np.random.default_rng(3).standard_normal((N, C, H, W)).astype(np.float32)
gamma_np = (1.0 + 0.1 * np.random.default_rng(4).standard_normal(C)).astype(np.float32)
beta_np = (0.1 * np.random.default_rng(5).standard_normal(C)).astype(np.float32)
rm0 = (0.2 * np.random.default_rng(6).standard_normal(C)).astype(np.float32)
rv0 = (1.0 + 0.2 * np.random.default_rng(7).standard_normal(C)).astype(np.float32)
r_np = np.random.default_rng(11).standard_normal((N, C, H, W)).astype(np.float32)

C4 = (None, slice(None), None, None)  # [1, C, 1, 1] broadcast index

bn = nn.BatchNorm2d(C, eps=EPS, momentum=MOMENTUM)
with torch.no_grad():
    bn.weight.copy_(torch.from_numpy(gamma_np))
    bn.bias.copy_(torch.from_numpy(beta_np))
    bn.running_mean.copy_(torch.from_numpy(rm0))
    bn.running_var.copy_(torch.from_numpy(rv0))
bn.train()

x = torch.from_numpy(x_np).requires_grad_(True)
y = bn(x)
loss = (y * torch.from_numpy(r_np)).sum()
loss.backward()

# ---- manual recomputation in float64 -------------------------------------------------
xf = x_np.astype(np.float64)
mu = xf.mean(axis=(0, 2, 3))                       # [C] batch mean
var_biased = xf.var(axis=(0, 2, 3), ddof=0)        # biased  (divide by M)
var_unbiased = xf.var(axis=(0, 2, 3), ddof=1)      # unbiased (divide by M-1)

results = []

def check(name, got, ref, tol=2e-6):
    got = np.asarray(got, dtype=np.float64)
    ref = np.asarray(ref, dtype=np.float64)
    err = float(np.max(np.abs(got - ref))) / max(float(np.max(np.abs(ref))), 1e-12)
    results.append((name, err, err <= tol))
    print(f"  {name:52s} max rel err = {err:.3e}  {'OK' if err <= tol else 'FAIL'}")

print("== forward / running stats (torch %s) ==" % torch.__version__)
y_np = y.detach().numpy()
for tag, var in (("biased", var_biased), ("unbiased (expect FAIL)", var_unbiased)):
    y_ref = gamma_np[C4] * (xf - mu[C4]) / np.sqrt(var[C4] + EPS) + beta_np[C4]
    check(f"normalize with {tag} var", y_np, y_ref)

check("running_mean = .9*rm + .1*batch_mean",
      bn.running_mean.numpy(), (1 - MOMENTUM) * rm0 + MOMENTUM * mu)
check("running_var  = .9*rv + .1*UNBIASED var",
      bn.running_var.numpy(), (1 - MOMENTUM) * rv0 + MOMENTUM * var_unbiased)
check("running_var  = .9*rv + .1*biased var (expect FAIL)",
      bn.running_var.numpy(), (1 - MOMENTUM) * rv0 + MOMENTUM * var_biased, tol=1e-9)

print("== gradients (batch stats, biased normalize; loss = sum(y*r)) ==")
dy = r_np.astype(np.float64)  # d/dy sum(y*r)
inv = 1.0 / np.sqrt(var_biased + EPS)
xhat = (xf - mu[C4]) * inv[C4]
dbeta_ref = dy.sum(axis=(0, 2, 3))
dgamma_ref = (dy * xhat).sum(axis=(0, 2, 3))
mean_dy = dy.mean(axis=(0, 2, 3))
mean_dy_xhat = (dy * xhat).mean(axis=(0, 2, 3))
dx_ref = (gamma_np * inv)[C4] * (dy - mean_dy[C4] - xhat * mean_dy_xhat[C4])
check("dbeta = sum(dy)", bn.bias.grad.numpy(), dbeta_ref, tol=1e-6)
check("dgamma = sum(dy*xhat)", bn.weight.grad.numpy(), dgamma_ref, tol=1e-6)
check("dx = g*inv*(dy - mean(dy) - xhat*mean(dy*xhat))", x.grad.numpy(), dx_ref, tol=5e-6)
print(f"  running stats have .grad? {bn.running_mean.grad is None and bn.running_var.grad is None} (expect True)")

print("== alternate (equivalent) dx form ==")
dx_ref2 = (gamma_np * inv)[C4] / M * (
    M * dy - dbeta_ref[C4] - xhat * dgamma_ref[C4]
)
check("dx = g*inv/M*(M*dy - sum(dy) - xhat*sum(dy*xhat))", x.grad.numpy(), dx_ref2, tol=5e-6)

print("== float64 finite differences of dx (four elements) ==")
def L(xf64):
    m = xf64.mean(axis=(0, 2, 3))
    v = xf64.var(axis=(0, 2, 3), ddof=0)
    xh = (xf64 - m[C4]) / np.sqrt(v[C4] + EPS)
    return float((r_np.astype(np.float64) * (gamma_np[C4] * xh + beta_np[C4])).sum())

worst = 0.0
for idx in [(0, 0, 0, 0), (1, 1, 2, 1), (3, 2, 4, 1), (2, 0, 1, 0)]:
    h = 1e-6
    xp, xm = xf.copy(), xf.copy()
    xp[idx] += h
    xm[idx] -= h
    fd = (L(xp) - L(xm)) / (2 * h)
    worst = max(worst, abs(fd - dx_ref[idx]) / max(abs(fd), 1e-12))
print(f"  worst dx vs f64 finite-diff = {worst:.3e}  {'OK' if worst < 1e-6 else 'FAIL'}")
results.append(("dx finite-diff", worst, worst < 1e-6))

print("== degenerate batch (one value per channel) ==")
bn1 = nn.BatchNorm2d(2).train()
try:
    bn1(torch.randn(1, 2, 1, 1))
    print("  torch accepted N*spatial == 1 (running_var would be nan/inf)")
except Exception as e:  # noqa: BLE001
    print(f"  torch raises: {type(e).__name__}: {e}")

print("== eval mode sanity (uses running stats) ==")
bn.eval()
y_eval = bn(torch.from_numpy(x_np)).detach().numpy()
y_eval_ref = gamma_np[C4] * (xf - bn.running_mean.numpy()[C4]) / np.sqrt(
    bn.running_var.numpy()[C4] + EPS
) + beta_np[C4]
check("eval normalize uses UPDATED running stats", y_eval, y_eval_ref)

fails = [n for n, e, ok in results if not ok and "expect FAIL" not in n]
expected_fails = [n for n, e, ok in results if not ok and "expect FAIL" in n]
print()
print("pinned:", "OK" if not fails and len(expected_fails) == 2 else f"CHECK {fails}")
print("(controls failing as expected:", "; ".join(expected_fails), ")")
