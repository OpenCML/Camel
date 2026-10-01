"""PyTorch helpers shared by the ``<model>/model_torch.py`` modules.

Responsibilities: thread pinning, weight loading into ``nn.Linear`` / ``nn.Conv2d``
under the harness weight convention (linear W is [in, out], so PyTorch gets W.T),
and generic eager / ``torch.compile`` / ONNX-export wrappers around a module.
"""

from __future__ import annotations

from pathlib import Path
from typing import Dict

import numpy as np
import torch
from torch import nn

from common.runner import Runner

ONNX_OPSET = 17


def set_threads(intra: int, inter: int = 1) -> None:
    torch.set_num_threads(intra)
    try:
        torch.set_num_interop_threads(inter)
    except RuntimeError:
        pass  # can only be set once per process, before any parallel work


def t(a: np.ndarray) -> torch.Tensor:
    """Copy a NumPy array into a contiguous float32 tensor."""
    return torch.from_numpy(np.ascontiguousarray(a, dtype=np.float32))


def linear(w: Dict[str, np.ndarray], prefix: str) -> nn.Linear:
    """Build nn.Linear from W [in, out] / b [out] (PyTorch stores W as [out, in])."""
    weight, bias = w[f"{prefix}_w"], w[f"{prefix}_b"]
    layer = nn.Linear(weight.shape[0], weight.shape[1])
    with torch.no_grad():
        layer.weight.copy_(t(weight.T))
        layer.bias.copy_(t(bias))
    return layer


def conv2d(w: Dict[str, np.ndarray], prefix: str, padding: int) -> nn.Conv2d:
    """Build nn.Conv2d from weight [out_c, in_c, kh, kw] (native PyTorch layout)."""
    weight, bias = w[f"{prefix}_w"], w[f"{prefix}_b"]
    oc, ic, kh, kw = weight.shape
    layer = nn.Conv2d(ic, oc, (kh, kw), stride=1, padding=padding)
    with torch.no_grad():
        layer.weight.copy_(t(weight))
        layer.bias.copy_(t(bias))
    return layer


def param(a: np.ndarray) -> nn.Parameter:
    return nn.Parameter(t(a), requires_grad=False)


def _runner(fn) -> Runner:
    def call(x: torch.Tensor) -> np.ndarray:
        with torch.inference_mode():
            return fn(x).numpy()

    return Runner(prepare=t, call=call)


def eager_runner(module: nn.Module) -> Runner:
    return _runner(module.eval())


def compiled_runner(module: nn.Module) -> Runner:
    """torch.compile with the default (inductor) backend; compiles on first call."""
    return _runner(torch.compile(module.eval()))


def export_onnx(module: nn.Module, example: np.ndarray, path: Path) -> Path:
    """Export with torch.onnx.export at opset 17 (TorchScript exporter, fixed shapes)."""
    path.parent.mkdir(parents=True, exist_ok=True)
    x = t(example)
    kwargs = dict(
        input_names=["input"],
        output_names=["output"],
        opset_version=ONNX_OPSET,
        do_constant_folding=True,
    )
    with torch.no_grad():
        try:
            torch.onnx.export(module.eval(), (x,), str(path), dynamo=False, **kwargs)
        except TypeError:  # older torch without the ``dynamo`` switch
            torch.onnx.export(module.eval(), (x,), str(path), **kwargs)
    return path
