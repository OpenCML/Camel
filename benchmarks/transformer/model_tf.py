"""TensorFlow implementation of the ``transformer`` benchmark model (see README.md).

Pre-LN encoder block with explicit LayerNorm (biased variance, eps=1e-5),
explicit 4-head scaled dot-product attention, and tanh-approximated GELU.
"""

from __future__ import annotations

import math
from pathlib import Path
from typing import Callable, Dict

import numpy as np
import tensorflow as tf

from common import tf_utils as tfu
from common.models import LN_EPS, MODELS, N_HEADS
from common.runner import Runner

INPUT_SHAPE = MODELS["transformer"].input_shape


def build_model(w: Dict[str, np.ndarray]) -> Callable[[tf.Tensor], tf.Tensor]:
    c = tfu.constants(w)
    b, s, d = INPUT_SHAPE
    dh = d // N_HEADS
    scale = 1.0 / math.sqrt(dh)
    gelu_k = math.sqrt(2.0 / math.pi)

    def layer_norm(x, prefix):
        mean, var = tf.nn.moments(x, axes=[-1], keepdims=True)  # biased variance
        return (x - mean) * tf.math.rsqrt(var + LN_EPS) * c[f"{prefix}_gamma"] + c[f"{prefix}_beta"]

    def split(t):  # [B,S,D] -> [B,H,S,Dh]
        return tf.transpose(tf.reshape(t, [b, s, N_HEADS, dh]), [0, 2, 1, 3])

    def forward(x: tf.Tensor) -> tf.Tensor:
        a = layer_norm(x, "ln1")
        q, k, v = (split(tfu.linear(a, c, n)) for n in ("q", "k", "v"))
        att = tf.nn.softmax(tf.matmul(q, k, transpose_b=True) * scale, axis=-1)
        ctx = tf.reshape(tf.transpose(tf.matmul(att, v), [0, 2, 1, 3]), [b, s, d])
        h = x + tfu.linear(ctx, c, "o")
        f = tfu.linear(layer_norm(h, "ln2"), c, "ffn1")
        f = 0.5 * f * (1.0 + tf.tanh(gelu_k * (f + 0.044715 * f * f * f)))
        return h + tfu.linear(f, c, "ffn2")

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
