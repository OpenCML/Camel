"""PyTorch implementation of the ``resnet`` benchmark model (E6, see common.models).

ResNet-18-depth residual CNN without batch_norm: conv3x3(3->64) stem, eight
basic blocks 64->128->256->512 (stride-2 first convs with 1x1 shortcuts),
global average pool, linear 512->10. Input [16,3,32,32].
"""

from __future__ import annotations

from typing import Dict

import numpy as np
import torch
import torch.nn.functional as F
from torch import nn

from common import torch_utils as tu

_BLOCKS = [
    ("b11", 1, False),
    ("b12", 1, False),
    ("b21", 2, True),
    ("b22", 1, False),
    ("b31", 2, True),
    ("b32", 1, False),
    ("b41", 2, True),
    ("b42", 1, False),
]


class _Block(nn.Module):
    def __init__(self, w: Dict[str, np.ndarray], nm: str, stride: int, downsample: bool):
        super().__init__()
        self.stride = stride
        self.c1_w, self.c1_b = tu.param(w[f"{nm}_c1_w"]), tu.param(w[f"{nm}_c1_b"])
        self.c2_w, self.c2_b = tu.param(w[f"{nm}_c2_w"]), tu.param(w[f"{nm}_c2_b"])
        if downsample:
            self.ds_w, self.ds_b = tu.param(w[f"{nm}_ds_w"]), tu.param(w[f"{nm}_ds_b"])
        else:
            self.ds_w = self.ds_b = None

    def forward(self, x):
        h = F.relu(F.conv2d(x, self.c1_w, self.c1_b, self.stride, 1))
        h = F.conv2d(h, self.c2_w, self.c2_b, 1, 1)
        if self.ds_w is not None:
            x = F.conv2d(x, self.ds_w, self.ds_b, self.stride, 0)
        return F.relu(h + x)


class ResNet(nn.Module):
    def __init__(self, w: Dict[str, np.ndarray]):
        super().__init__()
        self.stem_w, self.stem_b = tu.param(w["stem_w"]), tu.param(w["stem_b"])
        self.blocks = nn.ModuleList(_Block(w, nm, stride, ds) for nm, stride, ds in _BLOCKS)
        self.fc_w, self.fc_b = tu.param(w["fc_w"]), tu.param(w["fc_b"])

    def forward(self, x):
        h = F.relu(F.conv2d(x, self.stem_w, self.stem_b, 1, 1))
        for block in self.blocks:
            h = block(h)
        g = h.mean(dim=(2, 3))
        return g @ self.fc_w + self.fc_b


def build_model(w: Dict[str, np.ndarray]) -> nn.Module:
    return ResNet(w).eval()


def make_eager(w: Dict[str, np.ndarray]):
    from common import torch_utils as tu

    return tu.eager_runner(build_model(w))


def make_compiled(w: Dict[str, np.ndarray]):
    from common import torch_utils as tu

    return tu.compiled_runner(build_model(w))


def export_onnx(w: Dict[str, np.ndarray], x: np.ndarray, path):
    from common import torch_utils as tu

    return tu.export_onnx(build_model(w), x, path)
