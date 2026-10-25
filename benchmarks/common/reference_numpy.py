# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""NumPy reference implementations of every benchmark model.

These are the ground truth for numerical agreement checks. They follow the
contract in README.md literally (float64 accumulation for accuracy, cast back
to float32 at the end) and favour clarity over speed; they are never timed.
"""

from __future__ import annotations

from typing import Callable, Dict

import numpy as np

from common.models import D_MODEL, LN_EPS, N_HEADS

W = Dict[str, np.ndarray]


def _f64(w: W) -> W:
    return {k: v.astype(np.float64) for k, v in w.items()}


def _linear(x: np.ndarray, w: W, name: str) -> np.ndarray:
    return x @ w[f"{name}_w"] + w[f"{name}_b"]


def _relu(x: np.ndarray) -> np.ndarray:
    return np.maximum(x, 0.0)


def _sigmoid(x: np.ndarray) -> np.ndarray:
    return 1.0 / (1.0 + np.exp(-x))


def _conv2d_nchw(x: np.ndarray, weight: np.ndarray, bias: np.ndarray, pad: int, stride: int = 1) -> np.ndarray:
    """Cross-correlation (PyTorch semantics) via im2col; unit or uniform stride."""
    n, c, h, wd = x.shape
    oc, ic, kh, kw = weight.shape
    assert c == ic
    xp = np.pad(x, ((0, 0), (0, 0), (pad, pad), (pad, pad)))
    oh, ow = (h + 2 * pad - kh) // stride + 1, (wd + 2 * pad - kw) // stride + 1
    # cols[n, c, i, j, oh, ow] = xp[n, c, oh*stride + i, ow*stride + j]
    cols = np.empty((n, c, kh, kw, oh, ow), dtype=x.dtype)
    for i in range(kh):
        for j in range(kw):
            cols[:, :, i, j] = xp[:, :, i : i + stride * oh : stride, j : j + stride * ow : stride]
    out = np.einsum("ncijhw,ocij->nohw", cols, weight, optimize=True)
    return out + bias[None, :, None, None]


def _maxpool2(x: np.ndarray) -> np.ndarray:
    n, c, h, w = x.shape
    return x.reshape(n, c, h // 2, 2, w // 2, 2).max(axis=(3, 5))


def mlp(w: W, x: np.ndarray) -> np.ndarray:
    w, x = _f64(w), x.astype(np.float64)
    h = _relu(_linear(x, w, "fc1"))
    h = _relu(_linear(h, w, "fc2"))
    return _linear(h, w, "fc3").astype(np.float32)


def lenet(w: W, x: np.ndarray) -> np.ndarray:
    w, x = _f64(w), x.astype(np.float64)
    h = _maxpool2(_relu(_conv2d_nchw(x, w["conv1_w"], w["conv1_b"], pad=2)))  # [N,6,14,14]
    h = _maxpool2(_relu(_conv2d_nchw(h, w["conv2_w"], w["conv2_b"], pad=0)))  # [N,16,5,5]
    h = h.reshape(h.shape[0], -1)  # NCHW flatten -> [N,400]
    h = _relu(_linear(h, w, "fc1"))
    h = _relu(_linear(h, w, "fc2"))
    return _linear(h, w, "fc3").astype(np.float32)


def gru(w: W, x: np.ndarray) -> np.ndarray:
    w, x = _f64(w), x.astype(np.float64)
    b, t, _ = x.shape
    h = np.zeros((b, w["w_hr"].shape[0]))
    for s in range(t):
        xt = x[:, s, :]
        r = _sigmoid(xt @ w["w_ir"] + w["b_ir"] + h @ w["w_hr"] + w["b_hr"])
        z = _sigmoid(xt @ w["w_iz"] + w["b_iz"] + h @ w["w_hz"] + w["b_hz"])
        n = np.tanh(xt @ w["w_in"] + w["b_in"] + r * (h @ w["w_hn"] + w["b_hn"]))
        h = (1.0 - z) * n + z * h
    return _linear(h, w, "fc").astype(np.float32)


def _layer_norm(x: np.ndarray, gamma: np.ndarray, beta: np.ndarray) -> np.ndarray:
    mean = x.mean(axis=-1, keepdims=True)
    var = ((x - mean) ** 2).mean(axis=-1, keepdims=True)  # biased variance
    return (x - mean) / np.sqrt(var + LN_EPS) * gamma + beta


def _gelu_tanh(x: np.ndarray) -> np.ndarray:
    return 0.5 * x * (1.0 + np.tanh(np.sqrt(2.0 / np.pi) * (x + 0.044715 * x**3)))


def _softmax(x: np.ndarray) -> np.ndarray:
    e = np.exp(x - x.max(axis=-1, keepdims=True))
    return e / e.sum(axis=-1, keepdims=True)


def _tf_block(w: W, x: np.ndarray, prefix: str) -> np.ndarray:
    """One pre-LN encoder block; `prefix` selects the weight-name prefix ("" or b1..b4)."""
    b, s, d = x.shape
    dh = D_MODEL // N_HEADS

    def heads(t: np.ndarray) -> np.ndarray:  # [B,S,D] -> [B,H,S,Dh]
        return t.reshape(b, s, N_HEADS, dh).transpose(0, 2, 1, 3)

    def lin(t: np.ndarray, name: str) -> np.ndarray:
        return t @ w[f"{prefix}{name}_w"] + w[f"{prefix}{name}_b"]

    a = _layer_norm(x, w[f"{prefix}ln1_gamma"], w[f"{prefix}ln1_beta"])
    q, k, v = (heads(lin(a, n)) for n in ("q", "k", "v"))
    att = _softmax(q @ k.transpose(0, 1, 3, 2) / np.sqrt(dh))  # [B,H,S,S]
    ctx = (att @ v).transpose(0, 2, 1, 3).reshape(b, s, d)
    h = x + lin(ctx, "o")
    f = _layer_norm(h, w[f"{prefix}ln2_gamma"], w[f"{prefix}ln2_beta"])
    f = lin(_gelu_tanh(lin(f, "ffn1")), "ffn2")
    return h + f


def _transformer_h(w: W, x: np.ndarray, n_heads: int) -> np.ndarray:
    b, s, d = x.shape
    dh = d // n_heads

    def heads(t: np.ndarray) -> np.ndarray:  # [B,S,D] -> [B,H,S,Dh]
        return t.reshape(b, s, n_heads, dh).transpose(0, 2, 1, 3)

    a = _layer_norm(x, w["ln1_gamma"], w["ln1_beta"])
    q, k, v = (heads(_linear(a, w, n)) for n in ("q", "k", "v"))
    att = _softmax(q @ k.transpose(0, 1, 3, 2) / np.sqrt(dh))  # [B,H,S,S]
    ctx = (att @ v).transpose(0, 2, 1, 3).reshape(b, s, d)
    h = x + _linear(ctx, w, "o")
    f = _layer_norm(h, w["ln2_gamma"], w["ln2_beta"])
    f = _linear(_gelu_tanh(_linear(f, w, "ffn1")), w, "ffn2")
    return (h + f).astype(np.float32)


def transformer(w: W, x: np.ndarray) -> np.ndarray:
    w, x = _f64(w), x.astype(np.float64)
    return _transformer_h(w, x, N_HEADS)


def transformer_big(w: W, x: np.ndarray) -> np.ndarray:
    """E5-W1 upscaled single block (d_model 256, 8 heads, ffn 1024)."""
    w, x = _f64(w), x.astype(np.float64)
    return _transformer_h(w, x, 8)


def transformer4(w: W, x: np.ndarray) -> np.ndarray:
    """E6: four stacked pre-LN encoder blocks (see common.models)."""
    w, x = _f64(w), x.astype(np.float64)
    h = x
    for block in ("b1.", "b2.", "b3.", "b4."):
        h = _tf_block(w, h, block)
    return h.astype(np.float32)


def resnet(w: W, x: np.ndarray) -> np.ndarray:
    """E6 ResNet-18-depth CNN (no batch_norm; see common.models)."""
    w, x = _f64(w), x.astype(np.float64)

    def block(h: np.ndarray, nm: str) -> np.ndarray:
        a = _relu(_conv2d_nchw(h, w[f"{nm}_c1_w"], w[f"{nm}_c1_b"], pad=1))
        return _relu(_conv2d_nchw(a, w[f"{nm}_c2_w"], w[f"{nm}_c2_b"], pad=1) + h)

    def block_ds(h: np.ndarray, nm: str) -> np.ndarray:
        a = _relu(_conv2d_nchw(h, w[f"{nm}_c1_w"], w[f"{nm}_c1_b"], pad=1, stride=2))
        b = _conv2d_nchw(a, w[f"{nm}_c2_w"], w[f"{nm}_c2_b"], pad=1)
        s = _conv2d_nchw(h, w[f"{nm}_ds_w"], w[f"{nm}_ds_b"], pad=0, stride=2)
        return _relu(b + s)

    h = _relu(_conv2d_nchw(x, w["stem_w"], w["stem_b"], pad=1))
    h = block(block(h, "b11"), "b12")
    h = block(block_ds(h, "b21"), "b22")
    h = block(block_ds(h, "b31"), "b32")
    h = block(block_ds(h, "b41"), "b42")
    g = h.mean(axis=(2, 3))  # global average pool -> [N,512]
    return _linear(g, w, "fc").astype(np.float32)


BN_MOMENTUM = 0.1
BN_EPS = 1e-5


def _bn_train(x: np.ndarray, w: W, nm: str) -> np.ndarray:
    """Training-mode batch_norm (PyTorch semantics): the normalize uses the BIASED batch
    variance; the running variance blends in the UNBIASED one (see resnetbn/tools/bn_semantics.py)."""
    mu = x.mean(axis=(0, 2, 3))
    var = x.var(axis=(0, 2, 3))  # biased (divide by M)
    xhat = (x - mu[None, :, None, None]) / np.sqrt(var[None, :, None, None] + BN_EPS)
    return w[f"{nm}_bn_g"][None, :, None, None] * xhat + w[f"{nm}_bn_b"][None, :, None, None]


def resnetbn_stats(w: W, x: np.ndarray) -> Dict[str, np.ndarray]:
    """The running statistics after one training-mode forward (T4): rm <- .9 rm + .1 batch mean,
    rv <- .9 rv + .1 UNBIASED batch variance. Used to check the exported step's stats outputs."""
    w, x = _f64(w), x.astype(np.float64)
    stats: Dict[str, np.ndarray] = {}

    def bn(t: np.ndarray, nm: str) -> np.ndarray:
        mu = t.mean(axis=(0, 2, 3))
        var = t.var(axis=(0, 2, 3))
        m = t.size // t.shape[1]
        stats[f"{nm}_bn_rm"] = (1 - BN_MOMENTUM) * w[f"{nm}_bn_rm"] + BN_MOMENTUM * mu
        stats[f"{nm}_bn_rv"] = (1 - BN_MOMENTUM) * w[f"{nm}_bn_rv"] + BN_MOMENTUM * var * (m / (m - 1))
        return _bn_train(t, w, nm)

    def block(h: np.ndarray, nm: str) -> np.ndarray:
        a = _relu(bn(_conv2d_nchw(h, w[f"{nm}_c1_w"], w[f"{nm}_c1_b"], pad=1), f"{nm}_c1"))
        return _relu(bn(_conv2d_nchw(a, w[f"{nm}_c2_w"], w[f"{nm}_c2_b"], pad=1), f"{nm}_c2") + h)

    def block_ds(h: np.ndarray, nm: str) -> np.ndarray:
        a = _relu(bn(_conv2d_nchw(h, w[f"{nm}_c1_w"], w[f"{nm}_c1_b"], pad=1, stride=2), f"{nm}_c1"))
        b = bn(_conv2d_nchw(a, w[f"{nm}_c2_w"], w[f"{nm}_c2_b"], pad=1), f"{nm}_c2")
        s = bn(_conv2d_nchw(h, w[f"{nm}_ds_w"], w[f"{nm}_ds_b"], pad=0, stride=2), f"{nm}_ds")
        return _relu(b + s)

    h = _relu(bn(_conv2d_nchw(x, w["stem_w"], w["stem_b"], pad=1), "stem"))
    h = block(block(h, "b11"), "b12")
    h = block(block_ds(h, "b21"), "b22")
    h = block(block_ds(h, "b31"), "b32")
    h = block(block_ds(h, "b41"), "b42")
    return stats


def resnetbn(w: W, x: np.ndarray) -> np.ndarray:
    """T4 literal ResNet-18: RESNET with training-mode batch_norm after every conv."""
    w, x = _f64(w), x.astype(np.float64)

    def block(h: np.ndarray, nm: str) -> np.ndarray:
        a = _relu(_bn_train(_conv2d_nchw(h, w[f"{nm}_c1_w"], w[f"{nm}_c1_b"], pad=1), w, f"{nm}_c1"))
        return _relu(_bn_train(_conv2d_nchw(a, w[f"{nm}_c2_w"], w[f"{nm}_c2_b"], pad=1), w, f"{nm}_c2") + h)

    def block_ds(h: np.ndarray, nm: str) -> np.ndarray:
        a = _relu(_bn_train(_conv2d_nchw(h, w[f"{nm}_c1_w"], w[f"{nm}_c1_b"], pad=1, stride=2), w, f"{nm}_c1"))
        b = _bn_train(_conv2d_nchw(a, w[f"{nm}_c2_w"], w[f"{nm}_c2_b"], pad=1), w, f"{nm}_c2")
        s = _bn_train(_conv2d_nchw(h, w[f"{nm}_ds_w"], w[f"{nm}_ds_b"], pad=0, stride=2), w, f"{nm}_ds")
        return _relu(b + s)

    h = _relu(_bn_train(_conv2d_nchw(x, w["stem_w"], w["stem_b"], pad=1), w, "stem"))
    h = block(block(h, "b11"), "b12")
    h = block(block_ds(h, "b21"), "b22")
    h = block(block_ds(h, "b31"), "b32")
    h = block(block_ds(h, "b41"), "b42")
    g = h.mean(axis=(2, 3))  # global average pool -> [N,512]
    return _linear(g, w, "fc").astype(np.float32)


FORWARD: Dict[str, Callable[[W, np.ndarray], np.ndarray]] = {
    "mlp": mlp,
    "lenet": lenet,
    "gru": gru,
    "transformer": transformer,
    "resnet": resnet,
    "resnetbn": resnetbn,
    "transformer4": transformer4,
    "mlp_big": mlp,
    "lenet_big": lenet,
    "gru_big": gru,
    "transformer_big": transformer_big,
}
