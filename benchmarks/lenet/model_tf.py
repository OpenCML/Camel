"""TensorFlow implementation of the ``lenet`` benchmark model (see README.md).

The contract is NCHW with [out_c, in_c, kh, kw] kernels; TF CPU kernels require
NHWC, so the input is transposed once on entry, kernels are pre-transposed to
HWIO, and the activation is transposed back to NCHW before flattening so the
flatten order (C, H, W) matches the other frameworks.
"""

from __future__ import annotations

from pathlib import Path
from typing import Callable, Dict

import numpy as np
import tensorflow as tf

from common import tf_utils as tfu
from common.models import MODELS
from common.runner import Runner

INPUT_SHAPE = MODELS["lenet"].input_shape


def build_model(w: Dict[str, np.ndarray]) -> Callable[[tf.Tensor], tf.Tensor]:
    c = tfu.constants(w)
    k1, k2 = tfu.conv_kernel_hwio(w["conv1_w"]), tfu.conv_kernel_hwio(w["conv2_w"])

    def forward(x: tf.Tensor) -> tf.Tensor:
        h = tfu.nchw_to_nhwc(x)
        h = tf.nn.relu(tfu.conv2d_nhwc(h, k1, c["conv1_b"], pad=2))
        h = tf.nn.max_pool2d(h, ksize=2, strides=2, padding="VALID")
        h = tf.nn.relu(tfu.conv2d_nhwc(h, k2, c["conv2_b"], pad=0))
        h = tf.nn.max_pool2d(h, ksize=2, strides=2, padding="VALID")
        h = tf.reshape(tfu.nhwc_to_nchw(h), [tf.shape(h)[0], -1])  # NCHW flatten -> [N,400]
        h = tf.nn.relu(tfu.linear(h, c, "fc1"))
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
