"""TensorFlow implementation of the ``gru`` benchmark model (see README.md).

Explicit GRU cell loop with PyTorch gate equations (the reset gate is applied
to ``h @ W_hn + b_hn``, i.e. Keras ``reset_after=True`` semantics). The Python
loop over the fixed 16 time steps is unrolled when traced by tf.function.
"""

from __future__ import annotations

from pathlib import Path
from typing import Callable, Dict

import numpy as np
import tensorflow as tf

from common import tf_utils as tfu
from common.models import MODELS
from common.runner import Runner

INPUT_SHAPE = MODELS["gru"].input_shape


def build_model(w: Dict[str, np.ndarray]) -> Callable[[tf.Tensor], tf.Tensor]:
    c = tfu.constants(w)
    hidden = w["w_hr"].shape[0]
    steps = INPUT_SHAPE[1]

    def forward(x: tf.Tensor) -> tf.Tensor:
        h = tf.zeros([tf.shape(x)[0], hidden], dtype=tf.float32)
        for s in range(steps):
            xt = x[:, s, :]
            r = tf.sigmoid(xt @ c["w_ir"] + c["b_ir"] + h @ c["w_hr"] + c["b_hr"])
            z = tf.sigmoid(xt @ c["w_iz"] + c["b_iz"] + h @ c["w_hz"] + c["b_hz"])
            n = tf.tanh(xt @ c["w_in"] + c["b_in"] + r * (h @ c["w_hn"] + c["b_hn"]))
            h = (1.0 - z) * n + z * h
        return tfu.linear(h, c, "fc")

    return forward


def make_eager(w) -> Runner:
    return tfu.eager_runner(build_model(w))


def make_function(w) -> Runner:
    return tfu.function_runner(build_model(w), INPUT_SHAPE)


def make_xla(w) -> Runner:
    return tfu.xla_runner(build_model(w), INPUT_SHAPE)


def export_onnx(w, path: Path) -> Path:
    # Build inside the traced function so weights become graph constants
    # (tf2onnx turns eagerly captured tensors into extra graph inputs).
    return tfu.export_onnx(lambda x: build_model(w)(x), INPUT_SHAPE, path)
