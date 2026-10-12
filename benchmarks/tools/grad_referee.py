"""Three-way referee for a benchmark model's training gradient: Camel (float32), PyTorch
float32 and PyTorch float64, with the evidence that tells a wrong derivative rule from a
reference that computes something else and from a gradient float32 cannot pin down.

``train.py --check`` compares Camel's gradient with PyTorch's float32 autograd. When that
fails, three explanations are possible, and each predicts something different here:

  * Camel's rule is wrong        -> Camel is far from float64 while PyTorch float32 is close,
                                    on every draw, and also under a smooth activation;
  * the reference computes       -> the losses differ, or (batch_norm) the running statistics
    something else                  the two forwards leave behind differ;
  * float32 cannot pin the       -> the float64 gradient itself moves as much when weights and
    gradient down                   input move by one float32 rounding, the gap comes and goes
                                    with the draw on either side, and it is gone under a
                                    smooth activation.

What it found for the two models train.py --check rejects (README, "Gradient agreement"):
`resnet` (3.0e-03) and `resnetbn` (9.5e-02) are the third case, and the mechanism is the relu
kink, not accumulated rounding. Of 8.9 million relu inputs a handful lie within float32
rounding of zero; each float32 forward puts some of them on the other side, and the gradient
is discontinuous there. Camel's gradient equals the float64 gradient with 2 (resnet) or 3
(resnetbn) named relu decisions reversed, to 3.9e-07 and 5.4e-06.

Sections (on the benchmark weights and input, or with ``--seed`` on others drawn the same way):

  same computation?  the loss of one native Camel step against PyTorch's in both precisions,
                and for models with batch_norm the running statistics after the forward;
  three-way     per weight tensor, max|a - b| / max(max|t64|, floor) with train.py's floor
                (1e-3 of the largest gradient entry of the model), for the pairs
                Camel~t64, t32~t64 and Camel~t32. Weights whose float64 gradient is zero (a
                conv bias in front of a batch_norm) are reported apart: see ``judge``;
  fd            central differences of the float64 loss along random directions (is the
                float64 referee itself right), and central and one-sided differences along
                Camel - t64 on the worst tensor: at a kink the two one-sided slopes are the
                two gradients and the central difference is their mean;
  relu          how many relu inputs lie within float32 rounding of zero, and how many relu
                decisions differ between the float32 and the float64 forward;
  kinks         (``--kinks``) the float64 gradient recomputed with chosen relu decisions
                reversed: all those of the float32 forward (must reproduce t32), then the
                near-zero units a least-squares fit picks to explain Camel - t64, verified
                by an exact float64 backward;
  sensitivity   the float64 gradient at weights and input perturbed by a relative 6e-8 (one
                float32 rounding) against the float64 gradient at the benchmark point.

Verdict per run (``--require`` makes it the exit code): PASS when Camel is within ``--rtol``
of float64 on every weight; PASS-KINK when it is within ``--rtol`` of the float64 gradient
with the named near-zero relu decisions reversed; FAIL otherwise.

``--activation tanh|softplus`` replaces relu on both sides (a generated copy of model.cml;
PyTorch's F.relu patched): the same derivative rules for everything but relu, without kinks.

Usage (from benchmarks/):
  python tools/grad_referee.py --model resnetbn --kinks
  python tools/grad_referee.py --model resnetbn --seed 1 --seed 2 --seed 3 --no-fd
  python tools/grad_referee.py --model resnetbn --activation tanh --require
"""

from __future__ import annotations

import argparse
import contextlib
import importlib
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Dict, List, Optional, Tuple

import numpy as np

BENCH_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(BENCH_ROOT))

import train  # noqa: E402
from common.camel import _camel_env  # noqa: E402
from common.configs import camel_binary, camel_model  # noqa: E402
from common.models import MODELS  # noqa: E402
from common.weights import load  # noqa: E402

F32_EPS = float(np.finfo(np.float32).eps) / 2  # relative size of one float32 rounding

ACTIVATIONS = ("relu", "tanh", "softplus")


# ---------------------------------------------------------------- data


def draw(model: str, seed: Optional[int]) -> Tuple[Dict[str, np.ndarray], np.ndarray]:
    """The benchmark weights and input, or (seed given) others drawn the same way."""
    if seed is None:
        return load(model)
    from common.weights import _draw

    spec = MODELS[model]
    rng = np.random.default_rng(seed)
    weights = {w.name: _draw(rng, w) for w in spec.weights}
    x = rng.standard_normal(spec.input_shape).astype(np.float32)
    load(model)  # the manifest write_artifacts copies exists once the benchmark artifacts do
    return weights, x


def write_artifacts(root: Path, model: str, weights: Dict[str, np.ndarray], x: np.ndarray) -> Path:
    """Lays weights and input out as common/weights.py does, under `root`/<model>."""
    out = root / model
    (out / "weights").mkdir(parents=True, exist_ok=True)
    for name, value in weights.items():
        np.save(out / "weights" / f"{name}.npy", value)
    np.save(out / "input.npy", x)
    src = BENCH_ROOT / "artifacts" / model / "manifest.json"
    (out / "manifest.json").write_text(src.read_text())
    return out


# ---------------------------------------------------------------- camel


def make_variant(model: str, activation: str, out_dir: Path) -> Path:
    """A copy of the model with relu replaced by `activation` (tensor.tanh, or softplus written
    as log(1 + exp(x)))."""
    text = camel_model(model).read_text()
    if activation == "tanh":
        text = re.sub(r"import \{ Tensor, relu,", "import { Tensor, tanh,", text)
        text = re.sub(r"\brelu\(", "tanh(", text)
    elif activation == "softplus":
        text = re.sub(r"import \{ Tensor, relu,", "import { Tensor, exp, log,", text)
        text = text.replace("\nfunc block(", "\nfunc softplus(x: Tensor): Tensor {\n    return log(exp(x) + 1.0)\n}\n\nfunc block(", 1)
        text = re.sub(r"\brelu\(", "softplus(", text)
    out_dir.mkdir(parents=True, exist_ok=True)
    path = out_dir / "model.cml"
    path.write_text(text)
    return path


def camel_run(model: str, artifacts: Optional[Path], model_file: Optional[Path], passes: Tuple[str, ...], extra: Dict[str, str]) -> str:
    file = model_file or camel_model(model)
    env = _camel_env(model, {"CAMEL_BENCH_WORKLOAD": "train", "CAMEL_BENCH_WARMUP": "1", "CAMEL_BENCH_REPS": "1", **extra})
    env.setdefault("CAMEL_BENCH_THREADS", str(os.cpu_count() or 1))
    if artifacts is not None:
        env["CAMEL_BENCH_WEIGHTS"], env["CAMEL_BENCH_INPUT"] = str(artifacts / "weights"), str(artifacts / "input.npy")
    cmd = [str(camel_binary()), str(file), *passes]
    proc = subprocess.run(cmd, cwd=camel_model(model).parent, env=env, capture_output=True, text=True, timeout=1800)
    if proc.returncode != 0:
        raise RuntimeError(f"{' '.join(cmd)} failed ({proc.returncode}):\n{proc.stdout}\n{proc.stderr}")
    return proc.stdout


def camel_gradients(
    model: str, artifacts: Optional[Path], model_file: Optional[Path], passes: Tuple[str, ...]
) -> Tuple[Dict[str, np.ndarray], Optional[float]]:
    """Everything the model's gradient dump saves (nn.save_params of grad(loss), and for models
    with running statistics the statistics its forward produced), and the loss of one native
    training step from the same weights."""
    with tempfile.TemporaryDirectory() as tmp:
        camel_run(model, artifacts, model_file, passes, {"CAMEL_BENCH_GRADS": tmp})
        grads = {p.stem: np.load(p) for p in Path(tmp).glob("*.npy")}
    out = camel_run(model, artifacts, model_file, passes, {"CAMEL_BENCH_TRAIN_PROTOCOL": "stateless"})
    found = re.search(r"loss=([-+0-9.eE]+)", out)
    return grads, (float(found.group(1)) if found else None)


# ---------------------------------------------------------------- torch


@contextlib.contextmanager
def activation_patch(activation: str, record: Optional[List] = None, masks: Optional[List] = None):
    """Replaces torch.nn.functional.relu (the references call F.relu) and optionally records
    every input it sees. With `masks` (one boolean array per call, in call order) relu's
    decision is not taken from its input: call i returns input * masks[i]."""
    import torch
    import torch.nn.functional as F

    original = F.relu
    replacement = {"relu": original, "tanh": torch.tanh, "softplus": F.softplus}[activation]

    calls = [0]

    def patched(value, *args, **kwargs):
        if record is not None:
            record.append(value.detach().numpy().copy())
        if masks is not None:
            mask = torch.from_numpy(masks[calls[0]]).to(value.dtype)
            calls[0] += 1
            return value * mask
        return replacement(value) if activation != "relu" else original(value, *args, **kwargs)

    F.relu = patched
    try:
        yield
    finally:
        F.relu = original


def torch_net(model: str, weights: Dict[str, np.ndarray], dtype):
    module = importlib.import_module(f"{model}.model_torch")
    net = module.build_model({k: v.copy() for k, v in weights.items()})
    net.train()
    for p in net.parameters():
        p.requires_grad_(True)
    return net.to(dtype)


def torch_run(model, weights, x, dtype, activation="relu", record=None, names=None, masks=None):
    """(loss, weight name -> gradient as float64) of PyTorch autograd in `dtype`."""
    import torch

    net32 = torch_net(model, weights, torch.float32)
    matched = names if names is not None else train._match_parameters(net32, weights)
    # Buffers (batch_norm running statistics) are matched by value too, before the forward
    # updates them: what they hold afterwards is a by-product of the forward at every layer.
    initial = {n: b.detach().numpy().copy() for n, b in net32.named_buffers() if b.dtype.is_floating_point}
    buffers = {}
    for wname, w in weights.items():
        if wname in matched:
            continue
        for bname, value in initial.items():
            if bname not in buffers.values() and value.shape == w.shape and np.array_equal(value, w):
                buffers[wname] = bname
                break
    net = net32.to(dtype)
    bufs = dict(net.named_buffers())
    with activation_patch(activation, record, masks):
        loss = train.torch_loss(net, torch.from_numpy(x).to(dtype))
    loss.backward()
    by_name = dict(net.named_parameters())
    grads = {}
    for wname, (pname, transposed) in matched.items():
        if by_name[pname].grad is None:
            continue
        g = by_name[pname].grad.numpy().astype(np.float64)
        grads[wname] = g.T if transposed else g
    return float(loss.detach()), grads, matched, {w: bufs[b].numpy().astype(np.float64) for w, b in buffers.items()}


def torch_loss64(model, weights64: Dict[str, np.ndarray], matched, x, activation="relu") -> float:
    """The float64 loss at float64 weights (the object central differences differentiate)."""
    import torch

    net = torch_net(model, {k: v.astype(np.float32) for k, v in weights64.items()}, torch.float64)
    by_name = dict(net.named_parameters())
    with torch.no_grad():
        for wname, (pname, transposed) in matched.items():
            value = torch.from_numpy(np.ascontiguousarray(weights64[wname].T if transposed else weights64[wname]))
            by_name[pname].copy_(value)
        with activation_patch(activation):
            return float(train.torch_loss(net, torch.from_numpy(x).to(torch.float64)))


# ---------------------------------------------------------------- sections


def err(a: np.ndarray, b: np.ndarray, scale: float) -> float:
    return float(np.max(np.abs(a - b))) / scale


ZERO_GRADIENT = 1e-9  # of the model's largest gradient entry: below it a float64 gradient is "exactly zero"


def judge(rows: List[dict], rtol: float, key: str = "camel_t64") -> Tuple[float, List[str]]:
    """(worst error over the weights whose float64 gradient is not zero, the zero-gradient
    weights that fail). A zero-gradient weight has no relative error: what float32 computes
    there is the rounding of a sum that cancels exactly, measured against the floor. It is
    accepted within rtol like the others, or when it is no farther from float64 than PyTorch's
    own float32 gradient of the same weight."""
    worst = max((r[key] for r in rows if not r["zero"]), default=0.0)
    failed = [r["name"] for r in rows if r["zero"] and r[key] > rtol and r[key] > r["t32_t64"]]
    return worst, failed


def three_way(camel, g32, g64) -> Tuple[List[dict], float]:
    largest = max(float(np.max(np.abs(g))) for g in g32.values())
    floor = 1e-3 * largest
    rows = []
    for name in sorted(g64):
        scale = max(float(np.max(np.abs(g64[name]))), floor) or 1.0
        rows.append(
            {
                "name": name,
                "zero": float(np.max(np.abs(g64[name]))) <= ZERO_GRADIENT * largest,
                "shape": list(g64[name].shape),
                "max_ref": float(np.max(np.abs(g64[name]))),
                "camel_t64": err(camel[name], g64[name], scale) if name in camel else float("inf"),
                "t32_t64": err(g32[name], g64[name], scale),
                "camel_t32": err(camel[name], g32[name], scale) if name in camel else float("inf"),
            }
        )
    return rows, floor


def print_three_way(rows: List[dict], floor: float, rtol: float, every: bool) -> None:
    print(f"three-way, max|a-b| / max(max|t64|, floor={floor:.2e}); '*' marks > {rtol:g}")
    print(f"  {'tensor':14s} {'max|t64|':>9s} {'camel~t64':>10s} {'t32~t64':>10s} {'camel~t32':>10s}")
    for row in rows:
        flags = "".join("*" if row[k] > rtol else " " for k in ("camel_t64", "t32_t64", "camel_t32"))
        if every or flags.strip():
            print(
                f"  {row['name']:14s} {row['max_ref']:9.2e} {row['camel_t64']:10.2e} {row['t32_t64']:10.2e}"
                f" {row['camel_t32']:10.2e}  {flags}"
            )
    for key in ("camel_t64", "t32_t64", "camel_t32"):
        values = [row[key] for row in rows]
        worst = max(rows, key=lambda r: r[key])
        over = sum(v > rtol for v in values)
        print(f"  {key:10s} worst {max(values):.2e} ({worst['name']})  median {np.median(values):.2e}  over {rtol:g}: {over}/{len(rows)}")
    zero = [row for row in rows if row["zero"]]
    if zero:
        # floor = 1e-3 of the largest gradient entry, so these errors are 1e-3 of that in absolute terms.
        c, t = max(r["camel_t64"] for r in zero), max(r["t32_t64"] for r in zero)
        print(
            f"  zero-gradient weights (float64 gradient <= {ZERO_GRADIENT:g} of the model's largest entry): {len(zero)};"
            f" worst camel~t64 {c:.2e} of the floor = {c * 1e-3:.2e} of the largest entry, t32~t64 {t:.2e} = {t * 1e-3:.2e};"
            f" over {rtol:g}: camel {sum(r['camel_t64'] > rtol for r in zero)}, t32 {sum(r['t32_t64'] > rtol for r in zero)}"
        )
        nonzero = [row for row in rows if not row["zero"]]
        for key in ("camel_t64", "t32_t64"):
            values = [row[key] for row in nonzero]
            print(f"  {key:10s} over the other {len(nonzero)}: worst {max(values):.2e} ({max(nonzero, key=lambda r: r[key])['name']})  over {rtol:g}: {sum(v > rtol for v in values)}")


def finite_differences(model, weights, matched, x, camel, g64, worst: str, activation, h: float, count: int) -> None:
    """Central differences of the float64 loss along unit directions: random ones over all
    weights, and Camel - t64 restricted to the worst tensor."""
    w64 = {k: v.astype(np.float64) for k, v in weights.items()}
    rng = np.random.default_rng(0)

    def directional(direction: Dict[str, np.ndarray]) -> float:
        plus = {k: v + h * direction[k] if k in direction else v for k, v in w64.items()}
        minus = {k: v - h * direction[k] if k in direction else v for k, v in w64.items()}
        return (torch_loss64(model, plus, matched, x, activation) - torch_loss64(model, minus, matched, x, activation)) / (2 * h)

    def dot(grads, direction) -> float:
        return float(sum(np.sum(grads[k] * d) for k, d in direction.items()))

    print(f"finite differences (float64 loss, central, h={h:g}) against the directional derivative g.d:")
    print(f"  {'direction':28s} {'fd':>14s} {'t64.d':>14s} {'camel.d':>14s} {'|fd-t64|/|fd|':>14s} {'|fd-camel|/|fd|':>16s}")
    directions = []
    for i in range(count):
        d = {k: rng.standard_normal(g64[k].shape) for k in g64}
        norm = np.sqrt(sum(float(np.sum(v * v)) for v in d.values()))
        directions.append((f"random#{i} (all weights)", {k: v / norm for k, v in d.items()}))
    for name in dict.fromkeys([worst]):
        if name in camel:
            delta = camel[name].astype(np.float64) - g64[name]
            norm = float(np.linalg.norm(delta))
            if norm > 0:
                directions.append((f"camel-t64 on {name}", {name: delta / norm}))
            unit = g64[name] / (float(np.linalg.norm(g64[name])) or 1.0)
            directions.append((f"t64 gradient on {name}", {name: unit}))
    for label, direction in directions:
        fd = directional(direction)
        a, c = dot(g64, direction), dot({k: camel[k].astype(np.float64) for k in direction}, direction)
        denom = abs(fd) or 1.0
        print(f"  {label:28s} {fd:14.6e} {a:14.6e} {c:14.6e} {abs(fd - a) / denom:14.2e} {abs(fd - c) / denom:16.2e}")
    # One-sided slopes along Camel - t64: where the two gradients are the two sides of a relu
    # kink, one side reproduces t64.d and the other Camel.d (and the central difference above
    # is their mean).
    for label, direction in directions:
        if not label.startswith("camel-t64"):
            continue
        base = torch_loss64(model, w64, matched, x, activation)
        a, c = dot(g64, direction), dot({k: camel[k].astype(np.float64) for k in direction}, direction)
        print(f"  one-sided slopes along {label}  (t64.d {a:.6e}, camel.d {c:.6e}):")
        for step in (h, h / 10, h / 100):
            plus = {k: v + step * direction[k] if k in direction else v for k, v in w64.items()}
            minus = {k: v - step * direction[k] if k in direction else v for k, v in w64.items()}
            right = (torch_loss64(model, plus, matched, x, activation) - base) / step
            left = (base - torch_loss64(model, minus, matched, x, activation)) / step
            print(f"    h={step:.0e}: left {left:.6e}  right {right:.6e}")


def relu_section(model, weights, x, matched) -> None:
    import torch

    rec32: List[np.ndarray] = []
    rec64: List[np.ndarray] = []
    torch_run(model, weights, x, torch.float32, "relu", rec32, matched)
    torch_run(model, weights, x, torch.float64, "relu", rec64, matched)
    total = sum(a.size for a in rec64)
    exact = sum(int(np.sum(a == 0)) for a in rec32)
    flips = sum(int(np.sum((a > 0) != (b > 0))) for a, b in zip(rec32, rec64))
    print(f"relu inputs: {len(rec64)} calls, {total} values")
    print(f"  exactly zero in the float32 forward:            {exact}")
    print(f"  relu decision differs, float32 vs float64:      {flips}  ({flips / total:.2e} of all)")
    for k in (1, 16, 256, 4096):
        near = sum(int(np.sum(np.abs(b) <= k * F32_EPS * float(np.max(np.abs(b))))) for b in rec64)
        print(f"  |input| <= {k:4d} float32 roundings of the layer max: {near}  ({near / total:.2e} of all)")
    worst_layer = max(range(len(rec64)), key=lambda i: float(np.max(np.abs(rec32[i].astype(np.float64) - rec64[i])) / np.max(np.abs(rec64[i]))))
    drift = [float(np.max(np.abs(a.astype(np.float64) - b)) / np.max(np.abs(b))) for a, b in zip(rec32, rec64)]
    print(f"  forward drift f32 vs f64 (max|diff|/max|f64|) per relu input: first {drift[0]:.2e}, last {drift[-1]:.2e}, worst {drift[worst_layer]:.2e} (call {worst_layer})")


def kink_attribution(model, weights, x, matched, camel, g32, g64, floor, rtol, width: float, limit: int, rows: List[dict]) -> Optional[Tuple[float, List[str], int]]:
    """Explains the gaps by named relu units. The float64 gradient is recomputed with the relu
    decisions of chosen units reversed: first with every decision of the float32 forward (must
    reproduce t32), then with the subset of near-zero units that best explains Camel - t64
    (least squares on a sample of gradient entries, verified by an exact float64 backward)."""
    import torch

    rec32: List[np.ndarray] = []
    rec64: List[np.ndarray] = []
    torch_run(model, weights, x, torch.float32, "relu", rec32, matched)
    torch_run(model, weights, x, torch.float64, "relu", rec64, matched)
    mask64 = [a > 0 for a in rec64]
    mask32 = [a > 0 for a in rec32]

    def worst(a, b):
        errs = {k: err(np.asarray(a[k], dtype=np.float64), np.asarray(b[k], dtype=np.float64), max(float(np.max(np.abs(g64[k]))), floor) or 1.0) for k in g64 if k in a and k in b}
        name = max(errs, key=errs.get)
        return errs[name], name, sum(v > rtol for v in errs.values()), len(errs)

    def describe(tag, a, b):
        e, name, over, n = worst(a, b)
        print(f"  {tag:62s} worst {e:.2e} ({name})  over {rtol:g}: {over}/{n}")
        return e

    def grad_with(masks):
        return torch_run(model, weights, x, torch.float64, "relu", None, matched, masks)[1]

    print("kink attribution (float64 autograd with chosen relu decisions):")
    base = grad_with(mask64)
    describe("t64[own decisions, as masks] ~ t64  (the masking is exact)", base, g64)
    flips32 = sum(int(np.sum(a != b)) for a, b in zip(mask32, mask64))
    describe(f"t64[decisions of the float32 forward: {flips32} reversed] ~ t32", grad_with(mask32), g32)
    describe("for comparison: t64 ~ t32", g64, g32)

    # Candidates: units whose float64 input is within `width` times the layer's float32-vs-
    # float64 forward drift of zero, plus every unit the float32 forward decided differently.
    candidates = []
    for i, (a32, a64) in enumerate(zip(rec32, rec64)):
        drift = float(np.max(np.abs(a32.astype(np.float64) - a64)))
        near = (np.abs(a64) <= width * drift) | (mask32[i] != mask64[i])
        for flat in np.flatnonzero(near):
            candidates.append((abs(float(a64.flat[flat])) / drift, i, int(flat)))
    candidates.sort()
    total = len(candidates)
    candidates = candidates[:limit]
    print(f"  candidates: {total} relu units with |input| <= {width:g} x the layer's f32/f64 forward drift; using the nearest {len(candidates)}")
    if not candidates:
        return None

    rng = np.random.default_rng(2)
    names = sorted(k for k in g64 if k in camel)
    picks = {k: rng.choice(g64[k].size, size=min(g64[k].size, 20000), replace=False) for k in names}

    def sample(g):
        return np.concatenate([np.asarray(g[k], dtype=np.float64).ravel()[picks[k]] for k in names])

    base_vec = sample(base)
    columns = []
    for _, call, flat in candidates:
        masks = [m.copy() if j == call else m for j, m in enumerate(mask64)]
        masks[call].flat[flat] = not masks[call].flat[flat]
        columns.append(sample(grad_with(masks)) - base_vec)
    A = np.stack(columns, axis=1)
    target = sample(camel) - base_vec
    coef, *_ = np.linalg.lstsq(A, target, rcond=None)
    chosen = [i for i, c in enumerate(coef) if c > 0.5]
    others = max([abs(c) for i, c in enumerate(coef) if i not in chosen] or [0.0])
    print(f"  least squares of camel - t64 over {A.shape[1]} single-unit reversals: {len(chosen)} coefficients above 0.5, largest other |coef| {others:.2e}")
    masks = [m.copy() for m in mask64]
    for i in chosen:
        ratio, call, flat = candidates[i]
        index = tuple(int(v) for v in np.unravel_index(flat, mask64[call].shape))
        masks[call].flat[flat] = not masks[call].flat[flat]
        also32 = "also reversed in t32" if mask32[call].flat[flat] != mask64[call].flat[flat] else "not reversed in t32"
        print(
            f"    relu call {call:2d} unit {index}: float64 input {rec64[call].flat[flat]:+.3e} = {ratio:.2f} x drift"
            f" (layer max {np.max(np.abs(rec64[call])):.2e}), coef {coef[i]:.4f}, {also32}"
        )
    explained = grad_with(masks)
    describe(f"t64[those {len(chosen)} units reversed] ~ camel", explained, camel)
    describe("for comparison: t64 ~ camel", g64, camel)
    scale = {r["name"]: max(r["max_ref"], floor) or 1.0 for r in rows}
    residual = [{**r, "camel_t64": err(camel[r["name"]].astype(np.float64), explained[r["name"]], scale[r["name"]])} for r in rows]
    return (*judge(residual, rtol), len(chosen))


def sensitivity(model, weights, x, matched, g64, floor, activation, rtol, trials: int) -> None:
    """How far the exact gradient moves when every weight and input moves by one float32
    rounding: the error no float32 evaluation can be expected to stay under."""
    import torch

    print(f"sensitivity: float64 gradient at weights and input perturbed by relative {F32_EPS:.1e} (one float32 rounding)")
    rng = np.random.default_rng(1)
    worst_all = []
    for t in range(trials):
        # Perturbed weights are float64 values, so they are set after the parameters are matched.
        net = torch_net(model, weights, torch.float64)
        by_name = dict(net.named_parameters())
        with torch.no_grad():
            for wname, (pname, transposed) in matched.items():
                value = weights[wname].astype(np.float64) * (1.0 + F32_EPS * rng.uniform(-1, 1, weights[wname].shape))
                by_name[pname].copy_(torch.from_numpy(np.ascontiguousarray(value.T if transposed else value)))
        xp = x.astype(np.float64) * (1.0 + F32_EPS * rng.uniform(-1, 1, x.shape))
        with activation_patch(activation):
            loss = train.torch_loss(net, torch.from_numpy(xp))
        loss.backward()
        errs = {}
        for wname, (pname, transposed) in matched.items():
            if by_name[pname].grad is None or wname not in g64:
                continue
            g = by_name[pname].grad.numpy()
            g = g.T if transposed else g
            errs[wname] = err(g, g64[wname], max(float(np.max(np.abs(g64[wname]))), floor) or 1.0)
        worst = max(errs, key=errs.get)
        worst_all.append(errs[worst])
        print(f"  trial {t}: worst {errs[worst]:.2e} ({worst})  median {np.median(list(errs.values())):.2e}  over {rtol:g}: {sum(v > rtol for v in errs.values())}/{len(errs)}")
    print(f"  worst over trials: {max(worst_all):.2e}")


# ---------------------------------------------------------------- main


def run(args, seed: Optional[int]) -> dict:
    import torch

    model = args.model
    weights, x = draw(model, seed)
    print(f"\n==== {model}  seed={'benchmark' if seed is None else seed}  activation={args.activation}  torch {torch.__version__}")
    with tempfile.TemporaryDirectory() as tmp:
        artifacts = write_artifacts(Path(tmp), model, weights, x) if seed is not None else None
        model_file = args.camel_model
        if model_file is None and args.activation != "relu":
            model_file = make_variant(model, args.activation, Path(tmp) / "variant")
        camel, camel_loss = camel_gradients(model, artifacts, model_file, tuple(args.passes.split(",")))

    loss32, g32, matched, stats32 = torch_run(model, weights, x, torch.float32, args.activation)
    loss64, g64, _, stats64 = torch_run(model, weights, x, torch.float64, args.activation, names=matched)
    camel_stats = {k: v for k, v in camel.items() if k in stats64}
    camel = {k: v for k, v in camel.items() if k in g64}
    missing = sorted(set(g64) - set(camel))
    if missing:
        print(f"  MISSING in Camel's gradient: {missing}")
    print(f"same computation? loss: camel {camel_loss!r}  torch32 {loss32:.9e}  torch64 {loss64:.9e}")
    if camel_loss is not None:
        print(f"  |camel - t64| / |t64| = {abs(camel_loss - loss64) / abs(loss64):.2e}   |t32 - t64| / |t64| = {abs(loss32 - loss64) / abs(loss64):.2e}")
    print(f"  weights compared: {len(g64)} of {len(weights)} (the rest take no gradient in PyTorch: buffers)")
    if camel_stats:
        rel = lambda a, b: float(np.max(np.abs(a - b)) / np.max(np.abs(b)))  # noqa: E731
        c = {k: rel(v, stats64[k]) for k, v in camel_stats.items()}
        t = {k: rel(stats32[k], stats64[k]) for k in camel_stats}
        print(f"  running statistics after the forward ({len(c)} tensors, a by-product of every batch_norm's input):")
        print(f"    camel~t64 worst {max(c.values()):.2e} ({max(c, key=c.get)})   t32~t64 worst {max(t.values()):.2e} ({max(t, key=t.get)})")

    rows, floor = three_way(camel, g32, g64)
    print_three_way(rows, floor, args.rtol, args.all)
    worst = max((r for r in rows if not r["zero"]), key=lambda r: r["camel_t64"])["name"]
    if not args.no_fd:
        finite_differences(model, weights, matched, x, camel, g64, worst, args.activation, args.h, args.fd_directions)
    if args.activation == "relu":
        relu_section(model, weights, x, matched)
    direct, zero_failed = judge(rows, args.rtol)
    attributed = None
    if args.activation == "relu" and args.kinks and (direct > args.rtol or zero_failed):
        attributed = kink_attribution(model, weights, x, matched, camel, g32, g64, floor, args.rtol, args.kink_width, args.kink_limit, rows)
    if not args.no_sensitivity:
        sensitivity(model, weights, x, matched, g64, floor, args.activation, args.rtol, args.trials)
    if missing:
        verdict = "FAIL (weights missing from Camel's gradient)"
    elif direct <= args.rtol and not zero_failed:
        verdict = f"PASS (camel~t64 worst {direct:.2e} <= {args.rtol:g} over the weights with a gradient)"
        noisy = [r for r in rows if r["zero"] and r["camel_t64"] > args.rtol]
        if noisy:
            verdict += (
                f"; {len(noisy)} zero-gradient weights are over {args.rtol:g} of the floor (worst {max(r['camel_t64'] for r in noisy):.2e}),"
                " accepted as no farther from float64 than PyTorch float32 on the same weight"
            )
    elif attributed is not None and attributed[0] <= args.rtol and not attributed[1]:
        verdict = (
            f"PASS-KINK (camel~t64 worst {direct:.2e}; {attributed[0]:.2e} <= {args.rtol:g} against the float64 gradient"
            f" with {attributed[2]} relu decisions reversed, each within {args.kink_width:g} forward drifts of zero)"
        )
    else:
        why = f"camel~t64 worst {direct:.2e} over the weights with a gradient (limit {args.rtol:g})"
        if zero_failed:
            why += f"; zero-gradient weights over {args.rtol:g} and farther from float64 than PyTorch float32: {zero_failed}"
        if attributed is not None:
            why += f"; after reversing {attributed[2]} relu decisions still {attributed[0]:.2e}, zero-gradient failures {attributed[1]}"
        verdict = f"FAIL ({why})"
    print(f"verdict {model} seed={'benchmark' if seed is None else seed} activation={args.activation}: {verdict}")
    return {
        "model": model,
        "seed": seed,
        "verdict": verdict,
        "activation": args.activation,
        "rows": rows,
        "camel_t64": max(r["camel_t64"] for r in rows),
        "t32_t64": max(r["t32_t64"] for r in rows),
        "camel_t32": max(r["camel_t32"] for r in rows),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--model", required=True)
    parser.add_argument("--seed", type=int, action="append", help="draw weights and input from this seed (repeatable); default: the benchmark's")
    parser.add_argument("--activation", choices=ACTIVATIONS, default="relu")
    parser.add_argument("--camel-model", type=Path, help="Camel model file to run instead of the benchmark's")
    parser.add_argument("--passes", default="std::macro,std::nvm")
    parser.add_argument("--rtol", type=float, default=1e-4)
    parser.add_argument("--all", action="store_true", help="print every tensor, not only those over --rtol")
    parser.add_argument("--h", type=float, default=1e-6, help="finite-difference step")
    parser.add_argument("--fd-directions", type=int, default=3)
    parser.add_argument("--trials", type=int, default=3, help="sensitivity trials")
    parser.add_argument("--kinks", action="store_true", help="attribute the gaps to named relu units (one float64 backward per candidate)")
    parser.add_argument("--kink-width", type=float, default=0.5, help="candidate units: |input| <= this many forward drifts")
    parser.add_argument("--kink-limit", type=int, default=60, help="at most this many candidate units")
    parser.add_argument("--no-fd", action="store_true")
    parser.add_argument("--no-sensitivity", action="store_true")
    parser.add_argument("--json", type=Path, help="write the per-tensor numbers there")
    parser.add_argument("--threads", type=int, default=min(os.cpu_count() or 1, 8), help="PyTorch and Camel threads")
    parser.add_argument("--require", action="store_true", help="exit 1 unless every run's verdict is PASS or PASS-KINK")
    args = parser.parse_args()
    import torch

    torch.set_num_threads(args.threads)
    os.environ["CAMEL_BENCH_THREADS"] = str(args.threads)
    results = [run(args, seed) for seed in (args.seed or [None])]
    if args.json:
        args.json.write_text(json.dumps(results, indent=1))
    ok = all(r["verdict"].startswith("PASS") for r in results)
    print(f"gradient referee: {'PASS' if ok else 'FAIL'}")
    return 0 if ok or not args.require else 1


if __name__ == "__main__":
    sys.exit(main())
