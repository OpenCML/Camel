"""E5-W1 upscaled GRU: explicit-cell GRU (in 128, hidden 256, time 32, batch 64) +
Linear(256,10). Same code as ``gru`` (weights drive every shape); re-exported."""

from gru.model_torch import ExplicitGRU as ExplicitGRUBig  # noqa: F401
from gru.model_torch import build_model, export_onnx, make_compiled, make_eager  # noqa: F401
