"""PyTorch implementation of the ``transformer`` benchmark model (see README.md).

Single pre-LN encoder block, d_model=128, 4 heads, FFN 512, GELU (tanh approx.):
  h = x + MHA(LN1(x));  y = h + FFN(LN2(h))
Attention is written out explicitly (no fused SDPA kernel) so every framework
executes the same computation.
"""

from __future__ import annotations

import math
from pathlib import Path
from typing import Dict

import numpy as np
import torch
from torch import nn

from common import torch_utils as tu
from common.models import D_MODEL, LN_EPS, N_HEADS
from common.runner import Runner


def _layer_norm(w: Dict[str, np.ndarray], prefix: str) -> nn.LayerNorm:
    gamma = w[f"{prefix}_gamma"]
    ln = nn.LayerNorm(gamma.shape[0], eps=LN_EPS)
    with torch.no_grad():
        ln.weight.copy_(tu.t(gamma))
        ln.bias.copy_(tu.t(w[f"{prefix}_beta"]))
    return ln


class Block(nn.Module):
    def __init__(self, w: Dict[str, np.ndarray]):
        super().__init__()
        self.ln1 = _layer_norm(w, "ln1")
        self.q, self.k, self.v, self.o = (tu.linear(w, n) for n in ("q", "k", "v", "o"))
        self.ln2 = _layer_norm(w, "ln2")
        self.ffn = nn.Sequential(tu.linear(w, "ffn1"), nn.GELU(approximate="tanh"), tu.linear(w, "ffn2"))
        self.heads = N_HEADS
        self.scale = 1.0 / math.sqrt(D_MODEL // N_HEADS)  # Python constant: no tracer warnings

    def _mha(self, a):
        b, s, d = a.shape
        dh = d // self.heads

        def split(t):  # [B,S,D] -> [B,H,S,Dh]; head i owns features [i*Dh, (i+1)*Dh)
            return t.reshape(b, s, self.heads, dh).transpose(1, 2)

        q, k, v = split(self.q(a)), split(self.k(a)), split(self.v(a))
        att = torch.softmax((q @ k.transpose(-2, -1)) * self.scale, dim=-1)
        ctx = (att @ v).transpose(1, 2).reshape(b, s, d)
        return self.o(ctx)

    def forward(self, x):
        h = x + self._mha(self.ln1(x))
        return h + self.ffn(self.ln2(h))


def build_model(w: Dict[str, np.ndarray]) -> nn.Module:
    return Block(w).eval()


def make_eager(w: Dict[str, np.ndarray]) -> Runner:
    return tu.eager_runner(build_model(w))


def make_compiled(w: Dict[str, np.ndarray]) -> Runner:
    return tu.compiled_runner(build_model(w))


def export_onnx(w: Dict[str, np.ndarray], x: np.ndarray, path: Path) -> Path:
    return tu.export_onnx(build_model(w), x, path)
