"""Deterministic weight and input generation plus artifact loading.

Writes, for every model in ``common.models.MODELS``:

  artifacts/<model>/weights/<name>.npy   one float32 file per weight tensor
  artifacts/<model>/input.npy            float32 input batch
  artifacts/<model>/manifest.json        names, shapes, dtype, seed, file paths

Generation uses ``numpy.random.default_rng(spec.seed)``; tensors are drawn in
manifest order with ``rng.standard_normal(shape, dtype=float64)`` and scaled,
then the input is drawn last as a standard normal. The result is identical on
every platform for a given NumPy major version.

Usage:  python common/weights.py [--models mlp,lenet] [--force]
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Dict, Tuple

import numpy as np

BENCH_ROOT = Path(__file__).resolve().parent.parent
if str(BENCH_ROOT) not in sys.path:
    sys.path.insert(0, str(BENCH_ROOT))

from common.models import BIAS_STD, MODELS, NORM_STD, ModelSpec  # noqa: E402

ARTIFACTS = BENCH_ROOT / "artifacts"


def model_dir(model: str) -> Path:
    return ARTIFACTS / model


def _draw(rng: np.random.Generator, spec) -> np.ndarray:
    z = rng.standard_normal(spec.shape)
    if spec.kind == "weight":
        z = z / np.sqrt(spec.fan_in)
    elif spec.kind == "bias":
        z = z * BIAS_STD
    elif spec.kind == "gamma":
        z = 1.0 + z * NORM_STD
    elif spec.kind == "beta":
        z = z * NORM_STD
    else:
        raise ValueError(f"unknown weight kind {spec.kind!r}")
    return z.astype(np.float32)


def generate(spec: ModelSpec, force: bool = False) -> Path:
    """Generate weights/input/manifest for one model; returns the model dir."""
    out = model_dir(spec.name)
    manifest_path = out / "manifest.json"
    if manifest_path.exists() and not force:
        return out
    wdir = out / "weights"
    wdir.mkdir(parents=True, exist_ok=True)

    rng = np.random.default_rng(spec.seed)
    entries = []
    for w in spec.weights:
        arr = _draw(rng, w)
        np.save(wdir / f"{w.name}.npy", arr)
        entries.append(
            {"name": w.name, "shape": list(w.shape), "dtype": "float32", "file": f"weights/{w.name}.npy"}
        )
    x = rng.standard_normal(spec.input_shape).astype(np.float32)
    np.save(out / "input.npy", x)

    manifest = {
        "model": spec.name,
        "seed": spec.seed,
        "input": {"shape": list(spec.input_shape), "dtype": "float32", "file": "input.npy"},
        "output": {"shape": list(spec.output_shape), "dtype": "float32"},
        "weights": entries,
    }
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    return out


def load(model: str) -> Tuple[Dict[str, np.ndarray], np.ndarray]:
    """Load (weights dict, input array) for a model, generating on first use."""
    spec = MODELS[model]
    out = generate(spec)
    weights = {w.name: np.load(out / "weights" / f"{w.name}.npy") for w in spec.weights}
    x = np.load(out / "input.npy")
    return weights, x


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--models", default=",".join(MODELS), help="comma-separated model names")
    ap.add_argument("--force", action="store_true", help="regenerate even if artifacts exist")
    args = ap.parse_args()
    for name in args.models.split(","):
        out = generate(MODELS[name], force=args.force)
        print(f"[weights] {name}: {out.relative_to(BENCH_ROOT)}")


if __name__ == "__main__":
    main()
