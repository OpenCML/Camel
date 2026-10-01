"""Regenerates the spatial-operator fixtures (inputs and PyTorch references).

Run from the repository root with a Python that has numpy and torch, e.g.
``benchmarks/.venv/bin/python test/cases/modules/nn/fixtures/spatial/generate.py``.
The Camel test (spatial_ops.cml) compares its results against the ref_*.npy files.
"""

from pathlib import Path

import numpy as np
import torch
import torch.nn.functional as F

out = Path(__file__).resolve().parent
rng = np.random.default_rng(0)


def save(name, array):
    np.save(out / f"{name}.npy", np.asarray(array, dtype=np.float32))


x = rng.standard_normal((2, 3, 9, 7)).astype(np.float32)
w = rng.standard_normal((4, 3, 3, 3)).astype(np.float32)
b = rng.standard_normal(4).astype(np.float32)
save("x", x), save("w", w), save("b", b)

# conv2d(stride=2, padding=1) forward and gradients
X, W, B = (torch.tensor(a, requires_grad=True) for a in (x, w, b))
y = F.conv2d(X, W, B, stride=2, padding=1)
dy = torch.tensor(rng.standard_normal(tuple(y.shape)).astype(np.float32))
y.backward(dy)
save("dy", dy), save("ref_y", y.detach()), save("ref_dx", X.grad), save("ref_dw", W.grad), save("ref_db", B.grad)

# max_pool2d(kernel=2) forward and gradient
X = torch.tensor(x, requires_grad=True)
mp = F.max_pool2d(X, 2)
g = torch.tensor(rng.standard_normal(tuple(mp.shape)).astype(np.float32))
mp.backward(g)
save("gmp", g), save("ref_mp", mp.detach()), save("ref_dmp", X.grad)

# avg_pool2d(kernel=3, stride=1, padding=1, count_include_pad=True) forward and gradient
X = torch.tensor(x, requires_grad=True)
ap = F.avg_pool2d(X, 3, 1, 1)
g = torch.tensor(rng.standard_normal(tuple(ap.shape)).astype(np.float32))
ap.backward(g)
save("gap", g), save("ref_ap", ap.detach()), save("ref_dap", X.grad)

# inference batch_norm (eps = 1e-5)
m = rng.standard_normal(3).astype(np.float32)
v = (rng.random(3) + 0.5).astype(np.float32)
gm = rng.standard_normal(3).astype(np.float32)
bt = rng.standard_normal(3).astype(np.float32)
save("m", m), save("v", v), save("gm", gm), save("bt", bt)
save("ref_bn", F.batch_norm(*(torch.tensor(a) for a in (x, m, v, gm, bt)), False, 0.0, 1e-5))
