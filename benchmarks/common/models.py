"""Model registry: the single source of truth for benchmark model contracts.

Each entry fixes the RNG seed, input shape, and the ordered list of weight
tensors (name, shape, initialization kind, fan_in). Weight generation, the
NumPy reference, every framework implementation, and the Camel side all agree
on these names and shapes. The order of ``weights`` is also the order in which
tensors are drawn from the RNG, so it must never be reordered.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import List, Tuple


@dataclass(frozen=True)
class WeightSpec:
    """One weight tensor.

    kind:
      "weight" -> N(0, 1) * (1 / sqrt(fan_in))
      "bias"   -> N(0, 1) * BIAS_STD
      "gamma"  -> 1 + N(0, 1) * NORM_STD   (LayerNorm scale)
      "beta"   -> N(0, 1) * NORM_STD       (LayerNorm shift)
    """

    name: str
    shape: Tuple[int, ...]
    kind: str
    fan_in: int = 0


@dataclass(frozen=True)
class ModelSpec:
    name: str
    seed: int
    input_shape: Tuple[int, ...]
    output_shape: Tuple[int, ...]
    weights: List[WeightSpec] = field(default_factory=list)

    @property
    def batch(self) -> int:
        """Samples per forward call (the leading input dimension)."""
        return self.input_shape[0]


BIAS_STD = 0.05
NORM_STD = 0.1


def _linear(prefix: str, n_in: int, n_out: int) -> List[WeightSpec]:
    """Linear layer y = x @ W + b with W [in, out] and b [out]."""
    return [
        WeightSpec(f"{prefix}_w", (n_in, n_out), "weight", n_in),
        WeightSpec(f"{prefix}_b", (n_out,), "bias"),
    ]


def _conv(prefix: str, c_in: int, c_out: int, k: int) -> List[WeightSpec]:
    """Conv layer with weight [out_c, in_c, kh, kw] and bias [out_c]."""
    return [
        WeightSpec(f"{prefix}_w", (c_out, c_in, k, k), "weight", c_in * k * k),
        WeightSpec(f"{prefix}_b", (c_out,), "bias"),
    ]


MLP = ModelSpec(
    name="mlp",
    seed=1001,
    input_shape=(64, 784),
    output_shape=(64, 10),
    weights=_linear("fc1", 784, 256) + _linear("fc2", 256, 128) + _linear("fc3", 128, 10),
)

LENET = ModelSpec(
    name="lenet",
    seed=1002,
    input_shape=(64, 1, 28, 28),
    output_shape=(64, 10),
    weights=(
        _conv("conv1", 1, 6, 5)
        + _conv("conv2", 6, 16, 5)
        + _linear("fc1", 400, 120)
        + _linear("fc2", 120, 84)
        + _linear("fc3", 84, 10)
    ),
)

GRU_IN, GRU_H = 64, 128
GRU = ModelSpec(
    name="gru",
    seed=1003,
    input_shape=(32, 16, GRU_IN),
    output_shape=(32, 10),
    weights=(
        [WeightSpec(f"w_i{g}", (GRU_IN, GRU_H), "weight", GRU_IN) for g in "rzn"]
        + [WeightSpec(f"w_h{g}", (GRU_H, GRU_H), "weight", GRU_H) for g in "rzn"]
        + [WeightSpec(f"b_i{g}", (GRU_H,), "bias") for g in "rzn"]
        + [WeightSpec(f"b_h{g}", (GRU_H,), "bias") for g in "rzn"]
        + _linear("fc", GRU_H, 10)
    ),
)

D_MODEL, N_HEADS, D_FF = 128, 4, 512
TRANSFORMER = ModelSpec(
    name="transformer",
    seed=1004,
    input_shape=(8, 64, D_MODEL),
    output_shape=(8, 64, D_MODEL),
    weights=(
        [WeightSpec("ln1_gamma", (D_MODEL,), "gamma"), WeightSpec("ln1_beta", (D_MODEL,), "beta")]
        + _linear("q", D_MODEL, D_MODEL)
        + _linear("k", D_MODEL, D_MODEL)
        + _linear("v", D_MODEL, D_MODEL)
        + _linear("o", D_MODEL, D_MODEL)
        + [WeightSpec("ln2_gamma", (D_MODEL,), "gamma"), WeightSpec("ln2_beta", (D_MODEL,), "beta")]
        + _linear("ffn1", D_MODEL, D_FF)
        + _linear("ffn2", D_FF, D_MODEL)
    ),
)

MODELS = {m.name: m for m in (MLP, LENET, GRU, TRANSFORMER)}

LN_EPS = 1e-5
