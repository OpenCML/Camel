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


def _conv2d_nchw(x: np.ndarray, weight: np.ndarray, bias: np.ndarray, pad: int) -> np.ndarray:
    """Stride-1 cross-correlation (PyTorch semantics) via im2col."""
    n, c, h, wd = x.shape
    oc, ic, kh, kw = weight.shape
    assert c == ic
    xp = np.pad(x, ((0, 0), (0, 0), (pad, pad), (pad, pad)))
    oh, ow = h + 2 * pad - kh + 1, wd + 2 * pad - kw + 1
    # cols[n, c, i, j, oh, ow] = xp[n, c, oh + i, ow + j]
    cols = np.empty((n, c, kh, kw, oh, ow), dtype=x.dtype)
    for i in range(kh):
        for j in range(kw):
            cols[:, :, i, j] = xp[:, :, i : i + oh, j : j + ow]
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


def transformer(w: W, x: np.ndarray) -> np.ndarray:
    w, x = _f64(w), x.astype(np.float64)
    b, s, d = x.shape
    dh = D_MODEL // N_HEADS

    def heads(t: np.ndarray) -> np.ndarray:  # [B,S,D] -> [B,H,S,Dh]
        return t.reshape(b, s, N_HEADS, dh).transpose(0, 2, 1, 3)

    a = _layer_norm(x, w["ln1_gamma"], w["ln1_beta"])
    q, k, v = (heads(_linear(a, w, n)) for n in ("q", "k", "v"))
    att = _softmax(q @ k.transpose(0, 1, 3, 2) / np.sqrt(dh))  # [B,H,S,S]
    ctx = (att @ v).transpose(0, 2, 1, 3).reshape(b, s, d)
    h = x + _linear(ctx, w, "o")
    f = _layer_norm(h, w["ln2_gamma"], w["ln2_beta"])
    f = _linear(_gelu_tanh(_linear(f, w, "ffn1")), w, "ffn2")
    return (h + f).astype(np.float32)


FORWARD: Dict[str, Callable[[W, np.ndarray], np.ndarray]] = {
    "mlp": mlp,
    "lenet": lenet,
    "gru": gru,
    "transformer": transformer,
}
