"""Pipeline sets for the pipeline-invariance experiments (X1).

A pipeline is the list of pass names given to `camel <file> <pass>...`.

  fixed     P0-P12 of the experiment plan (P0, zero passes, is the reference).
  random    a frozen set drawn once from a seed: a random subset of the rewrite passes in a
            random order (length 0..N, no pass repeated, `std::macro` neither forced first nor
            forced present), followed by one terminal. R01-R60 end in a scheduling pass
            (`std::nvm`, `std::fvm`, `std::jit`, 20 each) and are the same 60 pipelines as in
            the first run; R61-R80 end in the translation pass `onnx::export` (drawn from the
            same generator after the first 60, so the first 60 are unchanged).
  onnx      E1 (`std::macro std::opt::simplify onnx::export`, the documented export pipeline)
            and R61-R80. They are run only on programs with an export entry (their own
            `onnx.export_model` call); the written model is then run on ONNX Runtime.
  refuse    pipelines that every program must refuse: an unknown pass name (XU1, XU2) and,
            for programs with an export entry, `onnx::export` without the passes it needs
            (XO1). They exercise the "refuse" half of the contract.

Run `python pipelines.py` to print the sets as JSON.
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
ONNX_TERMINAL = "onnx::export"

# Passes contributed by a module: they exist only in programs that import the module, so a
# pipeline naming one is "not applicable" to other programs rather than a refusal.
MODULE_PASSES = {"tensor::fuse": "tensor", "onnx::export": "onnx"}

SCHEDULING_PASSES = {"std::nvm", "std::fvm", "std::jit", "std::tf", "std::nodevm", "std::fastvm",
                     "std::taskflow", "std::default", "std::linear", "std::parallel"}

RANDOM_SEED = 20261004
RANDOM_PER_TERMINAL = 20

# The pipeline the documentation gives for the export pass (docs/cli.md).
ONNX_FIXED: Dict[str, List[str]] = {
    "E1": ["std::macro", "std::opt::simplify", ONNX_TERMINAL],
}

UNKNOWN_PASS = "x1a::no_such_pass"
# Pipelines that must be refused, with what they are expected to do and where that comes from.
EXPECT_REFUSE: Dict[str, dict] = {
    "XU1": {"passes": ["std::macro", UNKNOWN_PASS, "std::nvm"], "applies": "all",
            "source": "docs/cli.md: an unknown pass name is a diagnostic of stage pass:<name> "
                      "without a source position"},
    "XU2": {"passes": ["std::nvm", UNKNOWN_PASS], "applies": "all",
            "source": "same; the unknown name comes after a scheduling pass, so a refusal that "
                      "is not raised before execution shows as output/effects before the "
                      "diagnostic"},
    "XO1": {"passes": [ONNX_TERMINAL], "applies": "onnx",
            "source": "docs/cli.md, 'Refusal': without std::opt::simplify the entry graph still "
                      "calls main, so onnx::export refuses with TranslationRefused"},
}

# Deviation D5: what a command line of the must-refuse set is expected to do, decided from what
# its test plan or manifest states (corpus.py, `expectation_kind`). Only the first two kinds
# are in the must-refuse set; the others are listed in corpus.json and not run.
#   front_end          rejected by the parser or the type checker (stage parse / typecheck)
#   pipeline_refusal   rejected by a pass of the pipeline (stage pass:<name>)
#   run_time           the plan expects a failure while the program runs (stage run)
#   not_stated         the plan states neither a diagnostic name nor a stage
#   ambiguous          the plan names diagnostics of more than one kind
MUST_REFUSE_KINDS = {"front_end", "pipeline_refusal"}
# Diagnostics of the RuntimeDiag family that a pass raises before anything executes.
PASS_STAGE_DIAGNOSTICS = {"RuntimeDiag::TranslationRefused", "RuntimeDiag::UnrecognizedGraphPass",
                          "RuntimeDiag::PassInternalError"}


def _draw(rng: random.Random, terminals: List[str], per_terminal: int, seen: set, first_index: int
          ) -> Dict[str, List[str]]:
    out: Dict[str, List[str]] = {}
    index = first_index
    for terminal in terminals:
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


def _random_all(seed: int = RANDOM_SEED, per_terminal: int = RANDOM_PER_TERMINAL) -> Dict[str, List[str]]:
    rng = random.Random(seed)
    seen = {tuple(p) for p in FIXED.values()}
    out = _draw(rng, TERMINALS, per_terminal, seen, 1)
    seen |= {tuple(p) for p in ONNX_FIXED.values()}
    seen |= {tuple(v["passes"]) for v in EXPECT_REFUSE.values()}
    out.update(_draw(rng, [ONNX_TERMINAL], per_terminal, seen, len(out) + 1))
    return out


def random_pipelines(seed: int = RANDOM_SEED, per_terminal: int = RANDOM_PER_TERMINAL) -> Dict[str, List[str]]:
    """R01-R60: the random pipelines that end in a scheduling pass."""
    return {k: v for k, v in _random_all(seed, per_terminal).items() if v[-1] != ONNX_TERMINAL}


def native_pipelines() -> Dict[str, List[str]]:
    pipelines = dict(FIXED)
    pipelines.update(random_pipelines())
    return pipelines


def onnx_pipelines() -> Dict[str, List[str]]:
    pipelines = dict(ONNX_FIXED)
    pipelines.update({k: v for k, v in _random_all().items() if v[-1] == ONNX_TERMINAL})
    return pipelines


def refuse_pipelines() -> Dict[str, List[str]]:
    return {k: list(v["passes"]) for k, v in EXPECT_REFUSE.items()}


def all_pipelines() -> Dict[str, List[str]]:
    pipelines = native_pipelines()
    pipelines.update(onnx_pipelines())
    pipelines.update(refuse_pipelines())
    return pipelines


def kind(pipeline_id: str, passes: List[str]) -> str:
    """native | onnx | refuse | plan (a command line taken from a test plan)."""
    if pipeline_id in EXPECT_REFUSE:
        return "refuse"
    if pipeline_id == "PLAN":
        return "plan"
    if passes and passes[-1] == ONNX_TERMINAL:
        return "onnx"
    return "native"


def applies(pipeline_id: str, passes: List[str], program: dict) -> bool:
    """Whether a pipeline is run on a program of the agree set. Native pipelines run on every
    program; the ONNX pipelines and XO1 only on programs with an export entry."""
    k = kind(pipeline_id, passes)
    if k == "onnx":
        return bool(program.get("onnx"))
    if k == "refuse" and EXPECT_REFUSE[pipeline_id]["applies"] == "onnx":
        return bool(program.get("onnx"))
    return True


if __name__ == "__main__":
    print(json.dumps({"native": native_pipelines(), "onnx": onnx_pipelines(),
                      "refuse": EXPECT_REFUSE}, indent=1))
