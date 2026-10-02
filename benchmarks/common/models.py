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

# E6 scale probe: ResNet-18-depth CNN (8 residual blocks, 64->128->256->512 with
# stride-2 downsampling, ~11.2M parameters) on CIFAR-shaped input. Deliberately
# NO batch_norm: nn:batch_norm has a forward but no VJP (see modules/nn/tensor_ops.cpp),
# so the architecture keeps the residual structure and scale of ResNet-18 while
# staying inside the differentiable-and-exportable operator set. Batch is 16 to
# keep one training step within a second-scale CPU budget.


def _resnet_weights() -> List[WeightSpec]:
    specs = _conv("stem", 3, 64, 3)
    # (block name, c_in, c_out, stride of the first conv; stride 2 gets a 1x1 shortcut)
    blocks = [
        ("b11", 64, 64, 1),
        ("b12", 64, 64, 1),
        ("b21", 64, 128, 2),
        ("b22", 128, 128, 1),
        ("b31", 128, 256, 2),
        ("b32", 256, 256, 1),
        ("b41", 256, 512, 2),
        ("b42", 512, 512, 1),
    ]
    for name, c_in, c_out, stride in blocks:
        specs += _conv(f"{name}_c1", c_in, c_out, 3) + _conv(f"{name}_c2", c_out, c_out, 3)
        if stride == 2:
            specs += _conv(f"{name}_ds", c_in, c_out, 1)
    return specs + _linear("fc", 512, 10)


RESNET = ModelSpec(
    name="resnet",
    seed=1005,
    input_shape=(16, 3, 32, 32),
    output_shape=(16, 10),
    weights=_resnet_weights(),
)

def _tf_block_weights(prefix: str) -> List[WeightSpec]:
    """One pre-LN encoder block (same layout as TRANSFORMER) under ``<prefix>.`` —
    dotted so Camel's nested-struct gradient dump names match the harness names."""
    return (
        [WeightSpec(f"{prefix}.ln1_gamma", (D_MODEL,), "gamma"), WeightSpec(f"{prefix}.ln1_beta", (D_MODEL,), "beta")]
        + _linear(f"{prefix}.q", D_MODEL, D_MODEL)
        + _linear(f"{prefix}.k", D_MODEL, D_MODEL)
        + _linear(f"{prefix}.v", D_MODEL, D_MODEL)
        + _linear(f"{prefix}.o", D_MODEL, D_MODEL)
        + [WeightSpec(f"{prefix}.ln2_gamma", (D_MODEL,), "gamma"), WeightSpec(f"{prefix}.ln2_beta", (D_MODEL,), "beta")]
        + _linear(f"{prefix}.ffn1", D_MODEL, D_FF)
        + _linear(f"{prefix}.ffn2", D_FF, D_MODEL)
    )


# E6 scale probe: four stacked pre-LN encoder blocks (the single-block
# `transformer` is the unit; this checks the machinery at block-composition depth).
TRANSFORMER4 = ModelSpec(
    name="transformer4",
    seed=1006,
    input_shape=(8, 64, D_MODEL),
    output_shape=(8, 64, D_MODEL),
    weights=[spec for block in ("b1", "b2", "b3", "b4") for spec in _tf_block_weights(block)],
)

# E5-W1 upscaled variants (2026-10-02, second review): same model families scaled
# into the kernel-dominated regime (single step >= 5-10 ms on the reference CPU
# setup), to check whether the Camel-vs-PyTorch ratio from the small, dispatch-
# overhead-dominated workloads holds at kernel scale.
MLP_BIG = ModelSpec(
    name="mlp_big",
    seed=1007,
    input_shape=(256, 784),
    output_shape=(256, 10),
    weights=_linear("fc1", 784, 1024) + _linear("fc2", 1024, 512) + _linear("fc3", 512, 10),
)

LENET_BIG = ModelSpec(
    name="lenet_big",
    seed=1008,
    input_shape=(128, 1, 28, 28),
    output_shape=(128, 10),
    weights=(
        _conv("conv1", 1, 16, 5)
        + _conv("conv2", 16, 48, 5)
        + _linear("fc1", 1200, 120)
        + _linear("fc2", 120, 84)
        + _linear("fc3", 84, 10)
    ),
)

GRU_BIG_IN, GRU_BIG_H = 128, 256
GRU_BIG = ModelSpec(
    name="gru_big",
    seed=1009,
    input_shape=(64, 32, GRU_BIG_IN),
    output_shape=(64, 10),
    weights=(
        [WeightSpec(f"w_i{g}", (GRU_BIG_IN, GRU_BIG_H), "weight", GRU_BIG_IN) for g in "rzn"]
        + [WeightSpec(f"w_h{g}", (GRU_BIG_H, GRU_BIG_H), "weight", GRU_BIG_H) for g in "rzn"]
        + [WeightSpec(f"b_i{g}", (GRU_BIG_H,), "bias") for g in "rzn"]
        + [WeightSpec(f"b_h{g}", (GRU_BIG_H,), "bias") for g in "rzn"]
        + _linear("fc", GRU_BIG_H, 10)
    ),
)

D_MODEL_BIG, N_HEADS_BIG, D_FF_BIG = 256, 8, 1024
TRANSFORMER_BIG = ModelSpec(
    name="transformer_big",
    seed=1010,
    input_shape=(16, 64, D_MODEL_BIG),
    output_shape=(16, 64, D_MODEL_BIG),
    weights=(
        [WeightSpec("ln1_gamma", (D_MODEL_BIG,), "gamma"), WeightSpec("ln1_beta", (D_MODEL_BIG,), "beta")]
        + _linear("q", D_MODEL_BIG, D_MODEL_BIG)
        + _linear("k", D_MODEL_BIG, D_MODEL_BIG)
        + _linear("v", D_MODEL_BIG, D_MODEL_BIG)
        + _linear("o", D_MODEL_BIG, D_MODEL_BIG)
        + [WeightSpec("ln2_gamma", (D_MODEL_BIG,), "gamma"), WeightSpec("ln2_beta", (D_MODEL_BIG,), "beta")]
        + _linear("ffn1", D_MODEL_BIG, D_FF_BIG)
        + _linear("ffn2", D_FF_BIG, D_MODEL_BIG)
    ),
)

MODELS = {m.name: m for m in (MLP, LENET, GRU, TRANSFORMER, RESNET, TRANSFORMER4, MLP_BIG, LENET_BIG, GRU_BIG, TRANSFORMER_BIG)}

LN_EPS = 1e-5
