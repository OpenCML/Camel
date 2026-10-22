# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""E5-W1 upscaled transformer: one pre-LN encoder block, d_model=256, 8 heads,
FFN 1024, GELU(tanh), batch [16,64,256]. Mirrors ``transformer`` with a wider
head count; attention is explicit (no fused SDPA) as in the small model.
"""

from __future__ import annotations

import math
from typing import Dict

import numpy as np
import torch
from torch import nn

from common import torch_utils as tu
from common.models import LN_EPS
from common.runner import Runner

N_HEADS_BIG = 8


class BlockBig(nn.Module):
    def __init__(self, w: Dict[str, np.ndarray]):
        super().__init__()
        d = w["ln1_gamma"].shape[0]
        self.d = d
        ln1 = nn.LayerNorm(d, eps=LN_EPS)
        with torch.no_grad():
            ln1.weight.copy_(tu.t(w["ln1_gamma"]))
            ln1.bias.copy_(tu.t(w["ln1_beta"]))
        self.ln1 = ln1
        ln2 = nn.LayerNorm(d, eps=LN_EPS)
        with torch.no_grad():
            ln2.weight.copy_(tu.t(w["ln2_gamma"]))
            ln2.bias.copy_(tu.t(w["ln2_beta"]))
        self.ln2 = ln2
        self.q, self.k, self.v, self.o = (tu.linear(w, n) for n in ("q", "k", "v", "o"))
        self.ffn = nn.Sequential(tu.linear(w, "ffn1"), nn.GELU(approximate="tanh"), tu.linear(w, "ffn2"))
        self.heads = N_HEADS_BIG
        self.scale = 1.0 / math.sqrt(d // N_HEADS_BIG)

    def _mha(self, a):
        b, s, d = a.shape
        dh = d // self.heads

        def split(t):  # [B,S,D] -> [B,H,S,Dh]
            return t.reshape(b, s, self.heads, dh).transpose(1, 2)

        q, k, v = split(self.q(a)), split(self.k(a)), split(self.v(a))
        att = torch.softmax((q @ k.transpose(-2, -1)) * self.scale, dim=-1)
        ctx = (att @ v).transpose(1, 2).reshape(b, s, d)
        return self.o(ctx)

    def forward(self, x):
        h = x + self._mha(self.ln1(x))
        return h + self.ffn(self.ln2(h))


def build_model(w: Dict[str, np.ndarray]) -> nn.Module:
    return BlockBig(w).eval()


def make_eager(w: Dict[str, np.ndarray]) -> Runner:
    return tu.eager_runner(build_model(w))


def make_compiled(w: Dict[str, np.ndarray]) -> Runner:
    return tu.compiled_runner(build_model(w))


def export_onnx(w: Dict[str, np.ndarray], x: np.ndarray, path):
    return tu.export_onnx(build_model(w), x, path)
