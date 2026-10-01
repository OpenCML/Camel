"""PyTorch implementation of the ``mlp`` benchmark model (see README.md).

x[64,784] -> Linear(784,256) -> ReLU -> Linear(256,128) -> ReLU -> Linear(128,10)
"""

from __future__ import annotations

from pathlib import Path
from typing import Dict

import numpy as np
from torch import nn

from common import torch_utils as tu
from common.runner import Runner


class MLP(nn.Module):
    def __init__(self, w: Dict[str, np.ndarray]):
        super().__init__()
        self.fc1 = tu.linear(w, "fc1")
        self.fc2 = tu.linear(w, "fc2")
        self.fc3 = tu.linear(w, "fc3")
        self.relu = nn.ReLU()

    def forward(self, x):
        x = self.relu(self.fc1(x))
        x = self.relu(self.fc2(x))
        return self.fc3(x)


def build_model(w: Dict[str, np.ndarray]) -> nn.Module:
    return MLP(w).eval()


def make_eager(w: Dict[str, np.ndarray]) -> Runner:
    return tu.eager_runner(build_model(w))


def make_compiled(w: Dict[str, np.ndarray]) -> Runner:
    return tu.compiled_runner(build_model(w))


def export_onnx(w: Dict[str, np.ndarray], x: np.ndarray, path: Path) -> Path:
    return tu.export_onnx(build_model(w), x, path)
