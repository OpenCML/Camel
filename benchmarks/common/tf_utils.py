"""TensorFlow helpers shared by the ``<model>/model_tf.py`` modules.

Responsibilities: thread pinning, NCHW <-> NHWC conversions for convolution and
pooling (TF CPU kernels only support NHWC; the harness contract is NCHW with
[out_c, in_c, kh, kw] kernels), and generic eager / tf.function / XLA / tf2onnx
wrappers around a model callable.
"""

from __future__ import annotations

from pathlib import Path
from typing import Callable, Dict

import numpy as np
import tensorflow as tf

from common.runner import Runner

ONNX_OPSET = 17


def set_threads(intra: int, inter: int = 1) -> None:
    tf.config.threading.set_intra_op_parallelism_threads(intra)
    tf.config.threading.set_inter_op_parallelism_threads(inter)


def constants(w: Dict[str, np.ndarray]) -> Dict[str, tf.Tensor]:
    return {k: tf.constant(v, dtype=tf.float32) for k, v in w.items()}


def linear(x: tf.Tensor, c: Dict[str, tf.Tensor], prefix: str) -> tf.Tensor:
    """y = x @ W + b with W [in, out] (the harness layout is TF's native one)."""
    return tf.matmul(x, c[f"{prefix}_w"]) + c[f"{prefix}_b"]


def conv_kernel_hwio(weight: np.ndarray) -> tf.Tensor:
    """[out_c, in_c, kh, kw] -> [kh, kw, in_c, out_c] as TF expects."""
    return tf.constant(np.ascontiguousarray(weight.transpose(2, 3, 1, 0)), dtype=tf.float32)


def conv2d_nhwc(x: tf.Tensor, kernel_hwio: tf.Tensor, bias: tf.Tensor, pad: int) -> tf.Tensor:
    """Stride-1 conv with symmetric zero padding on an NHWC tensor."""
    padding = [[0, 0], [pad, pad], [pad, pad], [0, 0]] if pad else "VALID"
    return tf.nn.conv2d(x, kernel_hwio, strides=1, padding=padding) + bias


def nchw_to_nhwc(x: tf.Tensor) -> tf.Tensor:
    return tf.transpose(x, [0, 2, 3, 1])


def nhwc_to_nchw(x: tf.Tensor) -> tf.Tensor:
    return tf.transpose(x, [0, 3, 1, 2])


def _runner(fn: Callable[[tf.Tensor], tf.Tensor]) -> Runner:
    return Runner(
        prepare=lambda x: tf.constant(x, dtype=tf.float32),
        call=lambda x: fn(x).numpy(),
    )


def signature(input_shape) -> tf.TensorSpec:
    return tf.TensorSpec(shape=tuple(input_shape), dtype=tf.float32, name="input")


def eager_runner(forward: Callable) -> Runner:
    return _runner(forward)


def function_runner(forward: Callable, input_shape) -> Runner:
    return _runner(tf.function(forward, input_signature=[signature(input_shape)]))


def xla_runner(forward: Callable, input_shape) -> Runner:
    return _runner(tf.function(forward, input_signature=[signature(input_shape)], jit_compile=True))


def export_onnx(forward: Callable, input_shape, path: Path) -> Path:
    """Convert a tf.function to ONNX with tf2onnx at opset 17 (fixed shapes)."""
    import tf2onnx  # imported lazily: heavy and only needed for export

    path.parent.mkdir(parents=True, exist_ok=True)
    fn = tf.function(forward, input_signature=[signature(input_shape)])
    tf2onnx.convert.from_function(
        fn,
        input_signature=[signature(input_shape)],
        opset=ONNX_OPSET,
        output_path=str(path),
    )
    return path
