"""PyTorch implementation of the ``gru`` benchmark model (see README.md).

The primary model is an explicit GRU cell loop over time using the PyTorch GRU
equations with per-gate [in, out] weight matrices, followed by Linear(128,10)
on the final hidden state. ``NNGRU`` is a separate *library-kernel reference*
that feeds the same weights to ``torch.nn.GRU`` (config ``torch_nn_gru``).
"""

from __future__ import annotations

from pathlib import Path
from typing import Dict

import numpy as np
import torch
from torch import nn

from common import torch_utils as tu
from common.runner import Runner

GATES = "rzn"


class ExplicitGRU(nn.Module):
    def __init__(self, w: Dict[str, np.ndarray]):
        super().__init__()
        for g in GATES:
            setattr(self, f"w_i{g}", tu.param(w[f"w_i{g}"]))  # [64,128]
            setattr(self, f"w_h{g}", tu.param(w[f"w_h{g}"]))  # [128,128]
            setattr(self, f"b_i{g}", tu.param(w[f"b_i{g}"]))
            setattr(self, f"b_h{g}", tu.param(w[f"b_h{g}"]))
        self.hidden = w["w_hr"].shape[0]
        self.fc = tu.linear(w, "fc")

    def forward(self, x):
        b, steps, _ = x.shape
        h = x.new_zeros((b, self.hidden))
        for s in range(steps):  # unrolled at trace/compile time (fixed T=16)
            xt = x[:, s, :]
            r = torch.sigmoid(xt @ self.w_ir + self.b_ir + h @ self.w_hr + self.b_hr)
            z = torch.sigmoid(xt @ self.w_iz + self.b_iz + h @ self.w_hz + self.b_hz)
            n = torch.tanh(xt @ self.w_in + self.b_in + r * (h @ self.w_hn + self.b_hn))
            h = (1.0 - z) * n + z * h
        return self.fc(h)


class NNGRU(nn.Module):
    """Library-kernel reference: torch.nn.GRU loaded with the same weights.

    nn.GRU stacks gates in (r, z, n) order as [3H, in] (i.e. transposed).
    """

    def __init__(self, w: Dict[str, np.ndarray]):
        super().__init__()
        n_in, hidden = w["w_ir"].shape
        self.gru = nn.GRU(n_in, hidden, num_layers=1, batch_first=True)
        with torch.no_grad():
            self.gru.weight_ih_l0.copy_(tu.t(np.concatenate([w[f"w_i{g}"].T for g in GATES])))
            self.gru.weight_hh_l0.copy_(tu.t(np.concatenate([w[f"w_h{g}"].T for g in GATES])))
            self.gru.bias_ih_l0.copy_(tu.t(np.concatenate([w[f"b_i{g}"] for g in GATES])))
            self.gru.bias_hh_l0.copy_(tu.t(np.concatenate([w[f"b_h{g}"] for g in GATES])))
        self.fc = tu.linear(w, "fc")

    def forward(self, x):
        _, h_n = self.gru(x)  # h0 defaults to zeros
        return self.fc(h_n[0])


def build_model(w: Dict[str, np.ndarray]) -> nn.Module:
    return ExplicitGRU(w).eval()


def make_eager(w: Dict[str, np.ndarray]) -> Runner:
    return tu.eager_runner(build_model(w))


def make_compiled(w: Dict[str, np.ndarray]) -> Runner:
    return tu.compiled_runner(build_model(w))


def make_nn_gru(w: Dict[str, np.ndarray]) -> Runner:
    return tu.eager_runner(NNGRU(w).eval())


def export_onnx(w: Dict[str, np.ndarray], x: np.ndarray, path: Path) -> Path:
    return tu.export_onnx(build_model(w), x, path)
