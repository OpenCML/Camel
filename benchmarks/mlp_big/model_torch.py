# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""E5-W1 upscaled MLP: Linear(784,1024)->ReLU->Linear(1024,512)->ReLU->Linear(512,10),
batch 256. Same code as ``mlp`` (weights drive every shape); re-exported here so the
harness resolves `mlp_big.model_torch`."""

from mlp.model_torch import MLP as MLPBig  # noqa: F401
from mlp.model_torch import build_model, export_onnx, make_compiled, make_eager  # noqa: F401
