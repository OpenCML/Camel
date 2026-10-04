"""Pipeline sets for the pipeline-invariance experiments (X1).

A pipeline is the list of pass names given to `camel <file> <pass>...`.

  fixed    P0-P12 of the experiment plan (P0, zero passes, is the reference).
  random   a frozen set drawn once from a seed: a random subset of the rewrite passes in a
           random order (length 0..N, no pass repeated, `std::macro` neither forced first nor
           forced present), followed by one terminal scheduling pass. The same set is used
           for every program.

Run `python pipelines.py` to print the set as JSON.
"""

from __future__ import annotations

import json
import random
from typing import Dict, List

FIXED: Dict[str, List[str]] = {
    "P0": [],
    "P1": ["std::nvm"],
    "P2": ["std::fvm"],
    "P3": ["std::jit"],
    "P4": ["std::macro", "std::nvm"],
    "P5": ["std::macro", "std::fvm"],
    "P6": ["std::macro", "std::opt", "std::nvm"],
    "P7": ["std::macro", "std::opt::simplify", "std::nvm"],
    "P8": ["std::macro", "std::opt::simplify", "std::fvm"],
    "P9": ["std::macro", "std::inline", "std::nvm"],
    "P10": ["std::macro", "tensor::fuse", "std::nvm"],
    "P11": ["std::opt::simplify", "std::nvm"],
    "P12": ["std::macro", "std::opt::simplify", "std::tf"],
}

REFERENCE = "P0"
# Reference for programs whose macro bodies have effects, on pipelines that contain std::macro.
MACRO_REFERENCE = "P4"

REWRITE_PASSES: List[str] = [
    "std::macro",
    "std::devirtualize",
    "std::specialize",
    "std::inline",
    "std::opt::fold",
    "std::opt::cse",
    "std::opt::dce",
    "tensor::fuse",
    "std::opt::simplify",
]

TERMINALS: List[str] = ["std::nvm", "std::fvm", "std::jit"]

# Passes contributed by a module: they exist only in programs that import the module, so a
# pipeline naming one is "not applicable" to other programs rather than a refusal.
MODULE_PASSES = {"tensor::fuse": "tensor"}

SCHEDULING_PASSES = {"std::nvm", "std::fvm", "std::jit", "std::tf", "std::nodevm", "std::fastvm",
                     "std::taskflow", "std::default", "std::linear", "std::parallel"}

RANDOM_SEED = 20261004
RANDOM_PER_TERMINAL = 20


def random_pipelines(seed: int = RANDOM_SEED, per_terminal: int = RANDOM_PER_TERMINAL) -> Dict[str, List[str]]:
    rng = random.Random(seed)
    out: Dict[str, List[str]] = {}
    seen = {tuple(p) for p in FIXED.values()}
    index = 1
    for terminal in TERMINALS:
        made = 0
        while made < per_terminal:
            length = rng.randint(0, len(REWRITE_PASSES))
            passes = rng.sample(REWRITE_PASSES, length) + [terminal]
            if tuple(passes) in seen:
                continue
            seen.add(tuple(passes))
            out[f"R{index:02d}"] = passes
            index += 1
            made += 1
    return out


def all_pipelines() -> Dict[str, List[str]]:
    pipelines = dict(FIXED)
    pipelines.update(random_pipelines())
    return pipelines


if __name__ == "__main__":
    print(json.dumps(all_pipelines(), indent=1))
