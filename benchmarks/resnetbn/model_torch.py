"""PyTorch implementation of the ``resnetbn`` benchmark model (T4, see common.models).

The literal ResNet-18: RESNET (E6) with training-mode batch_norm after every conv,
including the 1x1 shortcut convs (torchvision's layout). BN affine weight/bias are
trainable parameters; the running mean/variance are buffers loaded from the benchmark
weights, updated by PyTorch's momentum blend in train mode (they take no gradient).
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


def _bn(w: Dict[str, np.ndarray], nm: str, channels: int) -> nn.BatchNorm2d:
    bn = nn.BatchNorm2d(channels, eps=1e-5, momentum=0.1)
    with torch.no_grad():
        bn.weight.copy_(torch.from_numpy(w[f"{nm}_bn_g"]))
        bn.bias.copy_(torch.from_numpy(w[f"{nm}_bn_b"]))
        bn.running_mean.copy_(torch.from_numpy(w[f"{nm}_bn_rm"]))
        bn.running_var.copy_(torch.from_numpy(w[f"{nm}_bn_rv"]))
    return bn


class _Block(nn.Module):
    def __init__(self, w: Dict[str, np.ndarray], nm: str, stride: int, downsample: bool):
        super().__init__()
        self.stride = stride
        c_out = w[f"{nm}_c1_w"].shape[0]
        self.c1_w, self.c1_b = tu.param(w[f"{nm}_c1_w"]), tu.param(w[f"{nm}_c1_b"])
        self.bn1 = _bn(w, f"{nm}_c1", c_out)
        self.c2_w, self.c2_b = tu.param(w[f"{nm}_c2_w"]), tu.param(w[f"{nm}_c2_b"])
        self.bn2 = _bn(w, f"{nm}_c2", c_out)
        if downsample:
            self.ds_w, self.ds_b = tu.param(w[f"{nm}_ds_w"]), tu.param(w[f"{nm}_ds_b"])
            self.bn_ds = _bn(w, f"{nm}_ds", c_out)
        else:
            self.ds_w = self.ds_b = self.bn_ds = None

    def forward(self, x):
        h = F.relu(self.bn1(F.conv2d(x, self.c1_w, self.c1_b, self.stride, 1)))
        h = self.bn2(F.conv2d(h, self.c2_w, self.c2_b, 1, 1))
        if self.ds_w is not None:
            x = self.bn_ds(F.conv2d(x, self.ds_w, self.ds_b, self.stride, 0))
        return F.relu(h + x)


class ResNetBn(nn.Module):
    def __init__(self, w: Dict[str, np.ndarray]):
        super().__init__()
        self.stem_w, self.stem_b = tu.param(w["stem_w"]), tu.param(w["stem_b"])
        self.stem_bn = _bn(w, "stem", 64)
        self.blocks = nn.ModuleList(_Block(w, nm, stride, ds) for nm, stride, ds in _BLOCKS)
        self.fc_w, self.fc_b = tu.param(w["fc_w"]), tu.param(w["fc_b"])

    def forward(self, x):
        h = F.relu(self.stem_bn(F.conv2d(x, self.stem_w, self.stem_b, 1, 1)))
        for block in self.blocks:
            h = block(h)
        g = h.mean(dim=(2, 3))
        return g @ self.fc_w + self.fc_b


def build_model(w: Dict[str, np.ndarray]) -> nn.Module:
    # No .eval()/ .train() here: the caller picks the mode. train.py's torch_model sets
    # .train() (training-mode BN, running stats updated by the momentum blend, no gradient).
    return ResNetBn(w)


def make_eager(w: Dict[str, np.ndarray]):
    # run.py forward rows are NOT part of the T4 acceptance (the train-mode reference and
    # an eval-mode torch forward answer different BN semantics; the training-step timing in
    # baselines/run_all.py drives this model in train mode instead). Eval mode also sidesteps
    # the in-place buffer update that inference_mode forbids.
    from common import torch_utils as tu

    return tu.eager_runner(build_model(w).eval())


def make_compiled(w: Dict[str, np.ndarray]):
    from common import torch_utils as tu

    return tu.compiled_runner(build_model(w).eval())


def export_onnx(w: Dict[str, np.ndarray], x: np.ndarray, path):
    from common import torch_utils as tu

    return tu.export_onnx(build_model(w).eval(), x, path)
