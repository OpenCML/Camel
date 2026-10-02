"""E5-W1 upscaled LeNet: Conv(1->16,k5,p2) -> pool -> Conv(16->48,k5) -> pool ->
Linear(1200,120) -> ReLU -> Linear(120,84) -> ReLU -> Linear(84,10), batch 128.
Same code as ``lenet`` (weights drive every shape); re-exported for the harness."""

from lenet.model_torch import LeNet as LeNetBig  # noqa: F401
from lenet.model_torch import build_model, export_onnx, make_compiled, make_eager  # noqa: F401
