"""PyTorch implementation of the ``lenet`` benchmark model (see README.md).

x[64,1,28,28] -> Conv(1->6,k5,p2) -> ReLU -> MaxPool2 -> Conv(6->16,k5) -> ReLU
-> MaxPool2 -> Flatten(NCHW) -> Linear(400,120) -> ReLU -> Linear(120,84) -> ReLU
-> Linear(84,10)
"""

from __future__ import annotations

from pathlib import Path
from typing import Dict

import numpy as np
from torch import nn

from common import torch_utils as tu
from common.runner import Runner


class LeNet(nn.Module):
    def __init__(self, w: Dict[str, np.ndarray]):
        super().__init__()
        self.features = nn.Sequential(
            tu.conv2d(w, "conv1", padding=2),
            nn.ReLU(),
            nn.MaxPool2d(2, 2),
            tu.conv2d(w, "conv2", padding=0),
            nn.ReLU(),
            nn.MaxPool2d(2, 2),
        )
        self.classifier = nn.Sequential(
            tu.linear(w, "fc1"),
            nn.ReLU(),
            tu.linear(w, "fc2"),
            nn.ReLU(),
            tu.linear(w, "fc3"),
        )

    def forward(self, x):
        x = self.features(x)
        return self.classifier(x.flatten(1))


def build_model(w: Dict[str, np.ndarray]) -> nn.Module:
    return LeNet(w).eval()


def make_eager(w: Dict[str, np.ndarray]) -> Runner:
    return tu.eager_runner(build_model(w))


def make_compiled(w: Dict[str, np.ndarray]) -> Runner:
    return tu.compiled_runner(build_model(w))


def export_onnx(w: Dict[str, np.ndarray], x: np.ndarray, path: Path) -> Path:
    return tu.export_onnx(build_model(w), x, path)
