"""TensorFlow implementation of the ``mlp`` benchmark model (see README.md)."""

from __future__ import annotations

from pathlib import Path
from typing import Callable, Dict

import numpy as np
import tensorflow as tf

from common import tf_utils as tfu
from common.models import MODELS
from common.runner import Runner

INPUT_SHAPE = MODELS["mlp"].input_shape


def build_model(w: Dict[str, np.ndarray]) -> Callable[[tf.Tensor], tf.Tensor]:
    c = tfu.constants(w)

    def forward(x: tf.Tensor) -> tf.Tensor:
        h = tf.nn.relu(tfu.linear(x, c, "fc1"))
        h = tf.nn.relu(tfu.linear(h, c, "fc2"))
        return tfu.linear(h, c, "fc3")

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
