# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""Measure the four implementations of one Transformer training step, at several sizes.

The Camel program writes its starting weights and data as .npy files; every PyTorch variant
loads exactly those, so all four start from the same numbers.  The reference is the same
mathematics in float64.

One command, one machine-readable file:

  python examples/models/naive-in-expert-out/measure.py
  python examples/models/naive-in-expert-out/measure.py --d-model 64,256,1024 --threads 1,32
  python examples/models/naive-in-expert-out/measure.py --from-json results/sweep.json --check-readme

The step time is the median of the timed steps, warmup excluded; it is measured inside each
program, around one forward, backward and Adam update.  The four implementations are measured in
interleaved rounds (Camel, naive, expert, compiled, then again) and each cell reports the median
of the round medians together with the spread, because one number from a shared machine is not a
comparison.  Peak memory is the whole process's maximum resident set, measured by /usr/bin/time,
so it includes interpreter startup and the allocator, not just the model.  The machine, the
thread count, the versions, the load that was running and the Camel build's fingerprint are
recorded per cell because a time without them is not a measurement.

The sweep holds the program fixed and varies one model width at a time.  `d_model` 64, 256 and
1024 are the sizes the review asks for; `batch` and `sequence` grow with the width by the same
factor as each other (x2 per 4x width), so the token count of a step grows with the width and
neither axis alone carries the scale-up.  See README.md, "The sweep", for why that is the honest
choice and what it does not cover.

The Camel source is generated from train_step.cml by substituting exactly the extents the cell
names; the program prints its own `naive config` line, which this harness checks against the cell
before accepting a timing, so a missed substitution is an error and not a quiet wrong shape.  The
generated file's SHA-256 and the substitution list are recorded in the result.

The thread budget is the command line's ``-t``, for every pool of the run.  The runtime no longer
reads ``CAMEL_NUM_THREADS``; this harness passes ``-t N`` to Camel and ``torch.set_num_threads(N)``
plus the usual OMP/MKL variables to PyTorch, so both sides run N threads by their own documented
mechanism.

Camel runs ``tensor::fuse`` before its scheduler.  That pass changes no operator (design principles
9.6): it records ``tensor.kernel`` on each matmul, and with the default ``CAMEL_TENSOR_BLAS=ON`` a
build that found a CBLAS says ``library`` there while safe mode and ``#strict`` say ``canonical``
(7.3/7.4).  ``--no-tensor-fuse`` omits it for a run that must use the canonical kernel everywhere,
which is the path of the results measured before the library became the default.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import math
import os
import platform
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
BENCH = REPO / "out" / "latest" / "bin" / "camel"
TORCH_IMPL = HERE / "torch_impl.py"
TRAIN_STEP = HERE / "train_step.cml"
RESULTS = HERE / "results"
ARTIFACTS = HERE / "artifacts" / "sweep"
README = HERE / "README.md"
README_CN = HERE / "README.cn.md"

# The single-size example the directory ships with; the sweep scales from it.
BASE_D_MODEL, BASE_BATCH, BASE_SEQUENCE = 64, 8, 16
BASE_D_FF, BASE_VOCAB, D_HEAD = 128, 32, 16
SWEEP_D_MODELS = (64, 256, 1024)
STEPS, WARMUP, LR = 16, 3, 0.003
TOL_LOSS = 1e-5       # absolute, final loss against the float64 reference
TOL_PARAM = 2e-4      # absolute, any single final parameter against float64
REPEATS = 3           # interleaved rounds; the spread across them is part of the result

CANONICAL = [
    f"blocks.{block}.{name}"
    for block in (0, 1)
    for name in ("norm1", "wq", "wk", "wv", "wo", "norm2", "w1", "b1", "w2", "b2")
] + ["norm_f", "head"]

VARIANTS = ("camel", "naive", "expert", "compiled")
ROWS = {
    "en": {
        "unit": "ms/step",
        "impl": "implementation",
        "headers": {"camel": "Camel (`tensor::fuse`, `std::jit`)", "naive": "PyTorch naive",
                    "expert": "PyTorch expert", "compiled": "`torch.compile(naive)`",
                    "reference": "float64 reference"},
        "single": "one thread",
        "all": "{threads} threads",
        "spread": "median of {repeats} interleaved rounds; parentheses: the round range",
        "reference": "one run",
        "begin": "<!-- sweep-table:begin -->",
        "end": "<!-- sweep-table:end -->",
    },
    "cn": {
        "unit": "ms/步",
        "impl": "实现",
        "headers": {"camel": "Camel（`tensor::fuse`、`std::jit`）", "naive": "PyTorch 朴素",
                    "expert": "PyTorch 专家", "compiled": "`torch.compile(朴素)`",
                    "reference": "float64 参考"},
        "single": "单线程",
        "all": "{threads} 线程",
        "spread": "{repeats} 轮交错的中位数；括号内为轮次范围",
        "reference": "单次运行",
        "begin": "<!-- sweep-table:begin -->",
        "end": "<!-- sweep-table:end -->",
    },
}


def camel_env(camel: Path) -> dict[str, str]:
    # The install root is the parent of bin/, whether the binary was built in this worktree or
    # named from another checkout with --camel.  Nothing here sets a thread count: the thread
    # budget is the command line's -t, and CAMEL_NUM_THREADS is no longer read by the runtime.
    install = camel.resolve().parents[1]
    env = os.environ.copy()
    env["CAMEL_HOME"] = str(install)
    env["PATH"] = str(install / "bin") + os.pathsep + env.get("PATH", "")
    return env


def torch_env(env: dict[str, str], threads: int) -> dict[str, str]:
    # PyTorch's own mechanism, the counterpart of -t N: torch_impl also calls set_num_threads,
    # and these keep the BLAS back ends from opening pools behind its back.
    out = env.copy()
    for name in ("OMP_NUM_THREADS", "MKL_NUM_THREADS", "OPENBLAS_NUM_THREADS"):
        out[name] = str(threads)
    return out


def sha256_text(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def run(command: list[str], env: dict[str, str], stats: Path | None = None) -> dict:
    """Run one process, optionally under /usr/bin/time for its peak resident set."""
    started = time.perf_counter()
    if stats is not None and Path("/usr/bin/time").exists():
        result = subprocess.run(
            ["/usr/bin/time", "-v", "-o", str(stats), *command],
            text=True, encoding="utf-8", errors="replace", env=env,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
    else:
        result = subprocess.run(
            command, text=True, encoding="utf-8", errors="replace", env=env,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
    wall = time.perf_counter() - started
    peak = 0
    if stats is not None and stats.exists():
        match = re.search(r"Maximum resident set size \(kbytes\): (\d+)", stats.read_text())
        if match:
            peak = int(match.group(1)) * 1024
    return {
        "command": command,
        "exit": result.returncode,
        "stdout": result.stdout,
        "stderr": result.stderr,
        "wall_seconds": wall,
        "peak_bytes": peak,
    }


# ---------------------------------------------------------------------------
# The shapes of a cell


def shape_for(d_model: int) -> dict:
    """The extents a cell measures: batch and sequence grow with the square root of the width.

    d_model 64 -> batch 8,  sequence 16  (128 tokens)
    d_model 256 -> batch 16, sequence 32 (512 tokens)
    d_model 1024 -> batch 32, sequence 64 (2048 tokens)

    batch and sequence scale by the same factor as each other, and the token count grows with the
    width: a wider model is trained on proportionally more tokens per step.  The step's arithmetic
    therefore grows as the cube of the width (x64 per 4x), which is what lets the fixed per-operator
    overhead stop dominating inside the range the review asked for.  Scaling both axes by the full
    width ratio would make the largest cell 65,536x the smallest step and would time a data center,
    not this question; the README states the choice.
    """
    ratio = d_model / BASE_D_MODEL
    scale = int(round(math.sqrt(ratio)))
    if scale < 1 or BASE_D_MODEL * scale * scale != d_model:
        raise SystemExit(
            f"d_model {d_model} is not {BASE_D_MODEL} times a square (64, 256, 576, 1024, ...)")
    heads = d_model // D_HEAD
    return {
        "d_model": d_model,
        "batch": BASE_BATCH * scale,
        "sequence": BASE_SEQUENCE * scale,
        "tokens": BASE_BATCH * scale * BASE_SEQUENCE * scale,
        "heads": heads,
        "d_head": D_HEAD,
        "d_ff": 2 * d_model,
        "vocab": BASE_VOCAB,
        # 2 blocks * (4 d^2 + 2 d d_ff + d_ff + 3 d) + d + d * vocab, with d_ff = 2 d.
        "params": 16 * d_model * d_model + 11 * d_model + d_model * BASE_VOCAB,
        "blocks": 2,
    }


def scaled_source(shape: dict, steps: int, warmup: int) -> tuple[str, list[tuple[str, str]]]:
    """train_step.cml with exactly its extents substituted; every substitution is counted."""
    text = TRAIN_STEP.read_text(encoding="utf-8")
    b, s, d = shape["batch"], shape["sequence"], shape["d_model"]
    h, dh, d_ff = shape["heads"], shape["d_head"], shape["d_ff"]
    vocab = shape["vocab"]
    substitutions = [
        ("let batch = 8", f"let batch = {b}", 1),
        ("let sequence = 16", f"let sequence = {s}", 1),
        ("let d_model = 64", f"let d_model = {d}", 1),
        ("let n_heads = 4", f"let n_heads = {h}", 1),
        ("let d_head = 16", f"let d_head = {dh}", 1),
        ("let d_ff = 128", f"let d_ff = {d_ff}", 1),
        ("let vocab = 32", f"let vocab = {vocab}", 1),
        ("let steps = 16", f"let steps = {steps}", 1),
        ("let warmup = 3", f"let warmup = {warmup}", 1),
        # The named shape aliases and every field/signature written with a literal extent.
        ("[8, 16, 64]", f"[{b}, {s}, {d}]", 1),
        ("type HeadStates = f32[8, 4, 16, 16]",
         f"type HeadStates = f32[{b}, {h}, {s}, {dh}]", 1),
        ("type Scores = f32[8, 4, 16, 16]",
         f"type Scores = f32[{b}, {h}, {s}, {s}]", 1),
        ("[8, 16, 128]", f"[{b}, {s}, {d_ff}]", 1),
        ("[8, 16, 32]", f"[{b}, {s}, {vocab}]", 1),
        ("f32[64, 64]", f"f32[{d}, {d}]", 8),
        ("f32[64, 128]", f"f32[{d}, {d_ff}]", 2),
        ("f32[128, 64]", f"f32[{d_ff}, {d}]", 2),
        ("f32[128]", f"f32[{d_ff}]", 2),
        ("f32[64]", f"f32[{d}]", 9),
        ("f32[64, 32]", f"f32[{d}, {vocab}]", 2),
        ("i64[8, 16]", f"i64[{b}, {s}]", 2),
    ]
    applied = []
    for old, new, expected in substitutions:
        count = text.count(old)
        if count != expected:
            raise SystemExit(
                f"train_step.cml changed shape: {old!r} occurs {count} times, expected {expected}")
        if old != new:
            text = text.replace(old, new)
            applied.append([old, new])
    return text, applied


# ---------------------------------------------------------------------------
# Parsing


def parse_camel(stdout: str, steps: int, warmup: int) -> dict:
    config = None
    init_loss = None
    final_loss = None
    losses: dict[int, float] = {}
    step_ms: dict[int, float] = {}
    for line in stdout.splitlines():
        match = re.match(
            r"naive config batch=(\d+) sequence=(\d+) d_model=(\d+) heads=(\d+) d_ff=(\d+) "
            r"vocab=(\d+) blocks=(\d+) params=(\d+)", line)
        if match:
            config = {
                "batch": int(match.group(1)), "sequence": int(match.group(2)),
                "d_model": int(match.group(3)), "heads": int(match.group(4)),
                "d_ff": int(match.group(5)), "vocab": int(match.group(6)),
                "blocks": int(match.group(7)), "params": int(match.group(8)),
            }
        match = re.match(r"naive init loss=([0-9.eE+-]+|nan|-?inf)", line)
        if match:
            init_loss = float(match.group(1))
        match = re.match(r"naive step=(\d+) loss=([0-9.eE+-]+|nan|-?inf)", line)
        if match:
            losses[int(match.group(1))] = float(match.group(2))
        match = re.match(r"naive bench step=(\d+) ms=([0-9.eE+-]+)", line)
        if match:
            step_ms[int(match.group(1))] = float(match.group(2))
        match = re.match(r"naive final steps=(\d+) loss=([0-9.eE+-]+|nan|-?inf)", line)
        if match:
            final_loss = float(match.group(2))
    if config is None:
        raise SystemExit(f"the Camel program printed no config line:\n{stdout}")
    if init_loss is None or final_loss is None or len(losses) != steps:
        raise SystemExit(f"could not parse the Camel output:\n{stdout}")
    ordered = [losses[i] for i in sorted(losses)]
    times = [step_ms[i] for i in sorted(step_ms) if i >= warmup]
    if len(times) != steps - warmup:
        raise SystemExit(f"expected {steps - warmup} timed steps, parsed {len(times)}:\n{stdout}")
    return {
        "config": config,
        "init_loss": init_loss,
        "final_loss": final_loss,
        "losses": ordered,
        "step_ms": [step_ms[i] for i in sorted(step_ms)],
        "median_ms": float(np.median(times)),
        "mean_ms": float(np.mean(times)),
        "timed_steps": len(times),
    }


def parse_torch(stdout: str) -> dict:
    for line in stdout.splitlines():
        if line.startswith("RESULT "):
            return json.loads(line[len("RESULT "):])
    raise SystemExit(f"torch_impl printed no RESULT line:\n{stdout}")


def load_npy_leaves(directory: Path, prefix: str = "") -> dict[str, np.ndarray]:
    out = {}
    for name in CANONICAL:
        out[name] = np.load(directory / f"{prefix}{name}.npy")
    return out


def load_npz(path: Path) -> dict[str, np.ndarray]:
    with np.load(path) as archive:
        return {name: archive[name] for name in CANONICAL}


def compare(params: dict[str, np.ndarray], reference: dict[str, np.ndarray]) -> dict:
    max_abs = max(float(np.max(np.abs(params[name].astype(np.float64) - reference[name])))
                  for name in CANONICAL)
    max_rel = max(float(np.max(np.abs(params[name].astype(np.float64) - reference[name]) /
                              np.maximum(np.abs(reference[name]), 1e-12)))
                  for name in CANONICAL)
    return {"max_abs": max_abs, "max_rel": max_rel}


def first_nonfinite(losses: list[float]) -> int | None:
    """The step at which the run stops being a number, or None."""
    for index, value in enumerate(losses):
        if not np.isfinite(value):
            return index
    return None


def json_number(value) -> float | None:
    """A JSON has no NaN; a non-finite measurement is written as null and named in the text."""
    return float(value) if value is not None and np.isfinite(value) else None


# ---------------------------------------------------------------------------
# Provenance


def git_state() -> dict:
    def git(*args: str) -> str:
        try:
            return subprocess.run(
                ["git", "-C", str(REPO), *args], capture_output=True, text=True,
                check=True).stdout.strip()
        except (OSError, subprocess.CalledProcessError):
            return ""

    commit = git("rev-parse", "HEAD")
    dirty = git("status", "--porcelain")
    return {
        "commit": commit or "unknown",
        "branch": git("rev-parse", "--abbrev-ref", "HEAD") or "unknown",
        "dirty": bool(dirty),
        "dirty_paths": dirty.splitlines()[:20],
    }


def build_stamp(camel: Path) -> dict:
    """The install's own stamp: the revision the running binary was built from.

    This is the commit to quote for a timing, not the checkout's HEAD: the harness and the
    documents can change without rebuilding anything.
    """
    path = camel.resolve().parents[1] / "build-info.json"
    try:
        info = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {"revision": "unknown", "dirty": None, "note": f"no readable {path}"}
    fingerprint = info.get("fingerprint") or {}
    return {
        "revision": info.get("revision") or "unknown",
        "dirty": info.get("dirty"),
        "dirtyCount": info.get("dirtyCount"),
        "builtAt": info.get("builtAt"),
        "config": info.get("config"),
        "fingerprint": fingerprint.get("hash"),
        "fingerprintFiles": fingerprint.get("files"),
    }


def machine() -> dict:
    cpu = ""
    try:
        for line in Path("/proc/cpuinfo").read_text().splitlines():
            if line.startswith("model name"):
                cpu = line.split(":", 1)[1].strip()
                break
    except OSError:
        pass
    memory = 0
    try:
        for line in Path("/proc/meminfo").read_text().splitlines():
            if line.startswith("MemTotal:"):
                memory = int(line.split()[1]) * 1024
                break
    except (OSError, ValueError, IndexError):
        pass
    return {
        "host": platform.node(),
        "platform": platform.platform(),
        "cpu": cpu,
        "hardware_threads": os.cpu_count(),
        "memory_bytes": memory,
        "python": sys.version.split()[0],
        "numpy": np.__version__,
        "torch": __import__("torch").__version__,
    }


def load_average() -> dict:
    """The host's load average, so a reader can see what else was running."""
    try:
        fields = Path("/proc/loadavg").read_text().split()
        return {"1m": float(fields[0]), "5m": float(fields[1]), "15m": float(fields[2]),
                "running": int(fields[3].split("/")[0]), "threads": int(fields[3].split("/")[1])}
    except (OSError, ValueError, IndexError):
        return {"1m": None, "5m": None, "15m": None, "running": None, "threads": None}


# ---------------------------------------------------------------------------
# One cell


def run_cell(shape: dict, threads: int, backend: str, repeats: int, steps: int, warmup: int,
             work: Path, camel: Path, environment: dict[str, str],
             block_limit_mb: int, tensor_fuse: bool) -> dict:
    d_model = shape["d_model"]
    cell_dir = work / f"d{d_model}-t{threads}"
    init, finals = cell_dir / "init", cell_dir / "final"
    for directory in (init, finals):
        if directory.exists():
            shutil.rmtree(directory)
        directory.mkdir(parents=True)

    source, substitutions = scaled_source(shape, steps, warmup)
    source_path = cell_dir / "train_step.cml"
    source_path.write_text(source, encoding="utf-8")
    torch_environment = torch_env(environment, threads)

    camel_base = [
        str(camel), "-t", str(threads), "--block-limit", str(block_limit_mb),
        str(source_path),
    ]
    # tensor::fuse records the implementation choice on the graph without changing an operator
    # (design principles 9.6): with a BLAS build the plain matmul says "library", and safe mode
    # or #strict says "canonical". Without the pass every matmul stays on the canonical kernel,
    # which is what a comparison against an older result needs.
    if tensor_fuse:
        camel_base.append("tensor::fuse")
    camel_base += [f"std::{backend}", "--arg", "bench=true", "--arg", f"init_dir={init}"]
    camel_command = camel_base + ["--arg", f"final_dir={finals / 'camel'}"]
    torch_commands = {
        variant: [
            sys.executable, str(TORCH_IMPL), "--variant", variant,
            "--init", str(init), "--steps", str(steps), "--warmup", str(warmup),
            "--threads", str(threads),
            "--batch", str(shape["batch"]), "--sequence", str(shape["sequence"]),
            "--d-model", str(d_model), "--n-heads", str(shape["heads"]),
            "--d-ff", str(shape["d_ff"]), "--vocab", str(shape["vocab"]),
            "--save-final", str(finals / f"{variant}.npz"),
        ]
        for variant in VARIANTS[1:] + ("reference",)
    }

    def run_variant(variant: str, round_index: int, save_final: bool) -> dict:
        directory = cell_dir / f"round{round_index}"
        directory.mkdir(parents=True, exist_ok=True)
        if variant == "camel":
            command = camel_base + [
                "--arg", f"final_dir={finals / 'camel' if save_final else ''}"]
            print(f"d={d_model} t={threads} camel: std::{backend} (round {round_index})", flush=True)
            result = run(command, environment, stats=directory / "camel.time")
            if result["exit"] != 0:
                sys.stderr.write(result["stdout"] + result["stderr"])
                raise SystemExit("the Camel program failed")
            parsed = parse_camel(result["stdout"], steps, warmup)
            parsed["backend"] = backend
            parsed["params"] = load_npy_leaves(finals / "camel") if save_final else None
        else:
            command = list(torch_commands[variant])
            if not save_final and variant != "reference":
                index = command.index("--save-final")
                del command[index:index + 2]
            print(f"d={d_model} t={threads} torch: {variant} (round {round_index})", flush=True)
            result = run(command, torch_environment, stats=directory / f"{variant}.time")
            if result["exit"] != 0:
                sys.stderr.write(result["stdout"] + result["stderr"])
                raise SystemExit(f"the {variant} counterpart failed")
            parsed = parse_torch(result["stdout"])
            parsed["timed_steps"] = steps - warmup
            parsed["params"] = load_npz(finals / f"{variant}.npz") if save_final else None
        parsed["peak_bytes"] = result["peak_bytes"]
        parsed["command"] = command
        parsed["round"] = round_index
        return parsed

    runs: dict[str, list[dict]] = {name: [] for name in VARIANTS}
    reference = None
    load_samples = [load_average()]
    for round_index in range(repeats):
        last = round_index == repeats - 1
        offset = round_index % len(VARIANTS)
        order = VARIANTS[offset:] + VARIANTS[:offset]
        for variant in order:
            runs[variant].append(run_variant(variant, round_index, last))
        if reference is None:
            reference = run_variant("reference", round_index, True)
        load_samples.append(load_average())
    load_end = load_average()

    # The generated source must be the cell it claims to be: the program prints its own extents,
    # and every PyTorch process echoes the shape it was told to use.
    expected = {key: shape[key] for key in
                ("batch", "sequence", "d_model", "heads", "d_ff", "vocab")}
    expected["blocks"] = shape["blocks"]
    expected["params"] = shape["params"]
    for run_row in runs["camel"]:
        if run_row["config"] != expected:
            raise SystemExit(
                f"the generated program ran {run_row['config']}, the cell is {expected}")
    torch_shape = {key: shape[key] for key in
                   ("batch", "sequence", "d_model", "heads", "d_head", "d_ff", "vocab")}
    for variant in VARIANTS[1:]:
        for run_row in runs[variant]:
            if run_row.get("shape") != torch_shape:
                raise SystemExit(
                    f"torch {variant} used {run_row.get('shape')}, the cell is {torch_shape}")

    # Determinism check: every round must reach the same losses, non-finite ones included (the
    # naive step can diverge; two runs of the same program diverge at the same step with the same
    # values, and that equality is what is checked here).
    for variant in VARIANTS:
        traces = {tuple(repr(value) for value in run["losses"]) for run in runs[variant]}
        if len(traces) != 1:
            raise SystemExit(f"d={d_model} t={threads}: {variant} is not deterministic: "
                             f"{[run['final_loss'] for run in runs[variant]]}")

    reference_params = reference["params"]
    summary = {}
    for variant in VARIANTS:
        last = runs[variant][-1]
        diverged_at = first_nonfinite(last["losses"])
        summary[variant] = {
            "backend": "std::" + backend if variant == "camel" else "torch",
            "init_loss": json_number(last["init_loss"]),
            "final_loss": json_number(last["final_loss"]),
            "losses": [json_number(value) for value in last["losses"]],
            "step_ms": last["step_ms"],
            "timed_steps": last["timed_steps"],
            "median_ms": median_ms(runs[variant]),
            "pooled_median_ms": float(np.median(
                [t for run in runs[variant] for t in run["step_ms"][warmup:]])),
            "mean_ms": float(np.mean([run["mean_ms"] for run in runs[variant]])),
            "spread": spread_ms(runs[variant]),
            "peak_bytes": max(run["peak_bytes"] for run in runs[variant]),
            "vs_reference": {key: json_number(value)
                             for key, value in compare(last["params"], reference_params).items()},
            "loss_vs_reference": json_number(abs(last["final_loss"] - reference["final_loss"])),
            "diverged_at_step": diverged_at,
            "accuracy": ("not compared: the run leaves the finite numbers at step "
                         f"{diverged_at}" if diverged_at is not None else "compared"),
            "within_tolerance": bool(
                diverged_at is None and np.isfinite(reference["final_loss"]) and
                abs(last["final_loss"] - reference["final_loss"]) <= TOL_LOSS and
                compare(last["params"], reference_params)["max_abs"] <= TOL_PARAM),
            "rounds": [
                {
                    "median_ms": run["median_ms"], "mean_ms": run["mean_ms"],
                    "final_loss": json_number(run["final_loss"]),
                    "peak_bytes": run["peak_bytes"],
                    "step_ms": run["step_ms"], "command": run["command"],
                }
                for run in runs[variant]
            ],
        }

    return {
        "d_model": d_model,
        "batch": shape["batch"],
        "sequence": shape["sequence"],
        "tokens": shape["tokens"],
        "heads": shape["heads"],
        "d_head": shape["d_head"],
        "d_ff": shape["d_ff"],
        "vocab": shape["vocab"],
        "blocks": shape["blocks"],
        "params": shape["params"],
        "threads": threads,
        "steps": steps,
        "warmup": warmup,
        "source": {
            "template": str(TRAIN_STEP.relative_to(REPO)),
            "template_sha256": sha256_text(TRAIN_STEP.read_text(encoding="utf-8")),
            "generated_sha256": sha256_text(source),
            "substitutions": substitutions,
            "note": "train_step.cml with exactly these extents substituted; the program's own "
                    "config line is checked against the cell",
        },
        "load_average": {"start": load_samples[0], "end": load_end, "samples": load_samples},
        "commands": {
            "camel": " ".join(camel_command),
            "torch": {name: " ".join(command) for name, command in torch_commands.items()},
        },
        "reference": {
            "variant": "float64 expert (stable softmax, log_softmax)",
            "init_loss": json_number(reference["init_loss"]),
            "final_loss": json_number(reference["final_loss"]),
            "diverged_at_step": first_nonfinite(reference["losses"]),
            "step_ms": reference["step_ms"], "median_ms": reference["median_ms"],
            "peak_bytes": reference["peak_bytes"], "command": reference["command"],
        },
        "variants": summary,
    }


def median_ms(runs: list[dict]) -> float:
    return float(np.median([run["median_ms"] for run in runs]))


def spread_ms(runs: list[dict]) -> dict:
    values = [run["median_ms"] for run in runs]
    return {"min": float(min(values)), "max": float(max(values)),
            "spread": float(max(values) - min(values))}


# ---------------------------------------------------------------------------
# The verdict: what these numbers can and cannot order


def order_groups(variants: dict) -> list[dict]:
    """Group implementations whose round ranges overlap, fastest first.

    A group is a chain: an entry joins the previous group when its fastest round is no slower
    than that group's slowest round.  Two entries in one group are inside each other's spread
    and this measurement cannot order them; entries in different groups are ordered because the
    gap is wider than both spreads.
    """
    entries = sorted(VARIANTS, key=lambda name: variants[name]["median_ms"])
    groups: list[dict] = []
    for name in entries:
        low = variants[name]["spread"]["min"]
        high = variants[name]["spread"]["max"]
        if groups and low <= groups[-1]["max"]:
            groups[-1]["names"].append(name)
            groups[-1]["max"] = max(groups[-1]["max"], high)
        else:
            groups.append({"names": [name], "min": low, "max": high})
    return groups


def verdict(cells: list[dict]) -> dict:
    per_cell = []
    for cell in cells:
        groups = order_groups(cell["variants"])
        per_cell.append({
            "d_model": cell["d_model"],
            "threads": cell["threads"],
            "groups": [group["names"] for group in groups],
            "ties": [group["names"] for group in groups if len(group["names"]) > 1],
            "camel_resolvably_fastest": groups[0]["names"] == ["camel"],
            "camel_in_first_group": "camel" in groups[0]["names"],
            "leader": groups[0]["names"],
        })

    crossover = {}
    for threads in sorted({cell["threads"] for cell in cells}, reverse=True):
        sequence = sorted((cell for cell in per_cell if cell["threads"] == threads),
                          key=lambda cell: cell["d_model"])
        first = next((cell for cell in sequence if not cell["camel_resolvably_fastest"]), None)
        if first is None:
            crossover[str(threads)] = {
                "none_in_range": True,
                "d_models": [cell["d_model"] for cell in sequence],
                "note": "Camel is the sole fastest entry in every cell of this thread count",
            }
        else:
            crossover[str(threads)] = {
                "none_in_range": False,
                "d_model": first["d_model"],
                "kind": "tied for fastest" if first["camel_in_first_group"] else "behind",
                "groups": first["groups"],
            }
    return {
        "definition": "two entries are tied when their round ranges overlap; the printed order "
                      "is only within groups wider than both spreads",
        "per_cell": per_cell,
        "crossover": crossover,
    }


# ---------------------------------------------------------------------------
# The README table, generated from the result file


def ms_cell(value: float) -> str:
    return f"{value:.3f}"


def render_block(result: dict, lang: str) -> str:
    labels = ROWS[lang]
    cells = {(cell["d_model"], cell["threads"]): cell for cell in result["cells"]}
    sizes = sorted({cell["d_model"] for cell in result["cells"]})
    threads = sorted({cell["threads"] for cell in result["cells"]})
    lines = [labels["begin"]]
    for thread_count in threads:
        title = labels["single"] if thread_count == 1 else labels["all"].format(threads=thread_count)
        lines.append(f"**{labels['unit']}, {title}** "
                     f"({labels['spread'].format(repeats=result['measurement']['repeats'])})")
        lines.append("")
        lines.append("| " + " | ".join([labels["impl"]] + [f"d={size}" for size in sizes]) + " |")
        lines.append("|" + "|".join("---" for _ in range(len(sizes) + 1)) + "|")
        for variant in VARIANTS + ("reference",):
            row = [labels["headers"][variant]]
            for size in sizes:
                cell = cells[(size, thread_count)]
                if variant == "reference":
                    row.append(f"{ms_cell(cell['reference']['median_ms'])} "
                               f"({labels['reference']})")
                else:
                    data = cell["variants"][variant]
                    row.append(f"{ms_cell(data['median_ms'])} "
                               f"({ms_cell(data['spread']['min'])}–{ms_cell(data['spread']['max'])})")
            lines.append("| " + " | ".join(row) + " |")
        lines.append("")
    lines.append(labels["end"])
    return "\n".join(lines)


def block_from(path: Path) -> str | None:
    try:
        text = path.read_text(encoding="utf-8")
    except OSError:
        return None
    begin = ROWS["en"]["begin"]
    end = ROWS["en"]["end"]
    start = text.find(begin)
    if start < 0:
        return None
    stop = text.find(end, start)
    if stop < 0:
        return None
    return text[start:stop + len(end)]


def check_readme(result: dict) -> int:
    failures = 0
    for path, lang in ((README, "en"), (README_CN, "cn")):
        expected = render_block(result, lang)
        actual = block_from(path)
        if actual is None:
            print(f"{path.name}: no sweep table markers; the README was not generated from this file")
            failures += 1
        elif actual != expected:
            failures += 1
            print(f"{path.name}: the sweep table does not match the result file")
            for expected_line, actual_line in zip(expected.splitlines(), actual.splitlines()):
                if expected_line != actual_line:
                    print(f"  result: {expected_line}")
                    print(f"  readme: {actual_line}")
                    break
        else:
            print(f"{path.name}: the sweep table matches the result file")
    return 0 if failures == 0 else 1


def write_readme(result: dict) -> None:
    for path, lang in ((README, "en"), (README_CN, "cn")):
        block = render_block(result, lang)
        begin, end = ROWS[lang]["begin"], ROWS[lang]["end"]
        text = path.read_text(encoding="utf-8")
        start = text.find(begin)
        if start < 0 or text.find(end, start) < 0:
            raise SystemExit(f"{path.name} has no sweep-table markers to fill")
        stop = text.find(end, start) + len(end)
        path.write_text(text[:start] + block + text[stop:], encoding="utf-8")
        print(f"updated {path.name}")


# ---------------------------------------------------------------------------


def table(rows: list[tuple], header: tuple) -> str:
    lines = ["| " + " | ".join(str(cell) for cell in header) + " |",
             "|" + "|".join("---" for _ in header) + "|"]
    for row in rows:
        lines.append("| " + " | ".join(str(cell) for cell in row) + " |")
    return "\n".join(lines)


def parse_sizes(value: str) -> list[int]:
    out = []
    for part in value.split(","):
        part = part.strip()
        if part:
            out.append(int(part))
    return out


def parse_threads(value: str) -> list[int]:
    out = []
    for part in value.split(","):
        part = part.strip().lower()
        if not part:
            continue
        out.append(os.cpu_count() if part in ("all", "cores") else int(part))
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", default=str(RESULTS / "sweep.json"))
    parser.add_argument("--camel", default=str(BENCH))
    parser.add_argument("--backend", default="jit", choices=("nvm", "fvm", "jit"))
    parser.add_argument("--d-model", default="64", type=parse_sizes,
                        help="comma-separated widths, each 64 times a square (default: 64)")
    parser.add_argument("--threads", default="1", type=parse_threads,
                        help="comma-separated thread counts, or 'all' (default: 1)")
    parser.add_argument("--repeats", type=int, default=REPEATS,
                        help="interleaved rounds of the four implementations (default: 3)")
    parser.add_argument("--steps", type=int, default=STEPS)
    parser.add_argument("--warmup", type=int, default=WARMUP)
    parser.add_argument("--work", default=str(ARTIFACTS), help="scratch directory")
    parser.add_argument("--block-limit", type=int, default=8192,
                        help="Camel's block space in MiB; the 2048 default is too small at "
                             "d_model 1024 with 2048 tokens (default: 8192)")
    parser.add_argument("--no-tensor-fuse", action="store_true",
                        help="run Camel without tensor::fuse: every matmul stays on the canonical "
                             "kernel (the path of results before the library default)")
    parser.add_argument("--from-json", default=None,
                        help="skip measuring; render or check a committed result file instead")
    parser.add_argument("--check-readme", action="store_true",
                        help="verify that README.md and README.cn.md tables match the result")
    parser.add_argument("--write-readme", action="store_true",
                        help="rewrite the README table blocks from the result")
    args = parser.parse_args()
    if args.repeats < 1:
        raise SystemExit("--repeats must be at least 1")
    if args.steps <= args.warmup:
        raise SystemExit("--steps must be greater than --warmup")

    if args.from_json:
        result = json.loads(Path(args.from_json).read_text(encoding="utf-8"))
        if args.check_readme:
            return check_readme(result)
        if args.write_readme:
            write_readme(result)
            return 0
        print(render_block(result, "en"))
        return 0

    if not Path(args.camel).exists():
        raise SystemExit(f"Camel binary not found: {args.camel} (build first, or pass --camel)")
    try:
        import torch  # noqa: F401
    except ImportError:
        raise SystemExit("PyTorch is required for the three counterparts (run in the torch venv)")

    shapes = [shape_for(size) for size in args.d_model]
    threads = args.threads
    work = Path(args.work)
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True, exist_ok=True)
    camel = Path(args.camel)
    environment = camel_env(camel)

    cells = []
    for shape in shapes:
        for thread_count in threads:
            cells.append(run_cell(shape, thread_count, args.backend, args.repeats,
                                  args.steps, args.warmup, work, camel, environment,
                                  args.block_limit, not args.no_tensor_fuse))

    result = {
        "schema": 3,
        "generated_at": dt.datetime.now().astimezone().isoformat(timespec="seconds"),
        "machine": machine(),
        "git": git_state(),
        "build": build_stamp(camel),
        "camel_version": subprocess.run(
            [str(camel), "--version"], capture_output=True, text=True,
            env=environment).stdout.strip(),
        "measurement": {
            "repeats": args.repeats,
            "interleaved": True,
            "order": "rotating: each round starts with the next implementation",
            "steps": args.steps,
            "warmup": args.warmup,
            "timed_steps_per_round": args.steps - args.warmup,
            "thread_counts": threads,
            "block_limit_mb": args.block_limit,
            "thread_control": "camel: -t on the command line; torch: set_num_threads and OMP/MKL",
            "load_average": "per cell: start, end and one sample per round",
            "absolute_values": "not portable: one shared host, the load is recorded per cell",
        },
        "sweep": {
            "d_models": [shape["d_model"] for shape in shapes],
            "scaling": "batch and sequence each x sqrt(d_model / 64), so the token count of a "
                       "step grows with the width; d_ff = 2 d_model; 16 heads per 16 features; "
                       "vocabulary 32 and two blocks at every size",
            "token_rule": "d=64: 8x16, d=256: 16x32, d=1024: 32x64",
        },
        "tolerance": {"loss_abs": TOL_LOSS, "param_abs": TOL_PARAM},
        "cells": cells,
    }
    result["verdict"] = verdict(cells)

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2, allow_nan=False) + "\n", encoding="utf-8")

    machine_info = result["machine"]
    print()
    print(f"machine: {machine_info['host']} | {machine_info['cpu']} | "
          f"{machine_info['hardware_threads']} hardware threads | torch {machine_info['torch']} | "
          f"Python {machine_info['python']}")
    print(f"camel: {result['git']['commit'][:12]}"
          f"{' (dirty)' if result['git']['dirty'] else ''}"
          f" | {result['camel_version'] or 'no version line'}")
    build = result["build"]
    print(f"binary: built at {build['revision'][:12]}"
          f"{' (dirty)' if build.get('dirty') else ' (clean)'}"
          f" | {build.get('builtAt') or 'no build time'}"
          f" | fingerprint {(build.get('fingerprint') or 'unknown')[:12]}")
    print(f"rounds: {args.repeats} interleaved, each a median of {args.steps - args.warmup} steps "
          f"after {args.warmup} warmup")
    print()
    print(render_block(result, "en"))
    print()
    for entry in result["verdict"]["per_cell"]:
        groups = " > ".join(" = ".join(group) for group in entry["groups"])
        print(f"d={entry['d_model']} t={entry['threads']}: {groups}"
              f" | Camel resolvably fastest: {'yes' if entry['camel_resolvably_fastest'] else 'NO'}")
    for thread_count, crossing in result["verdict"]["crossover"].items():
        if crossing["none_in_range"]:
            print(f"threads {thread_count}: no crossover in {crossing['d_models'][0]}–"
                  f"{crossing['d_models'][-1]}")
        else:
            print(f"threads {thread_count}: Camel stops being fastest at d={crossing['d_model']} "
                  f"({crossing['kind']})")
    print()
    print(f"result file: {output}")
    failures = [f"d={cell['d_model']} t={cell['threads']} {name}"
                for cell in cells for name in VARIANTS
                if cell["variants"][name]["diverged_at_step"] is None
                and not cell["variants"][name]["within_tolerance"]]
    for cell in cells:
        for name in VARIANTS:
            step = cell["variants"][name]["diverged_at_step"]
            if step is not None:
                print(f"NOTE: d={cell['d_model']} t={cell['threads']} {name}: the loss leaves "
                      f"the finite numbers at step {step}; accuracy is not compared there")
    reference_step = {cell["d_model"]: cell["reference"]["diverged_at_step"] for cell in cells}
    for size, step in reference_step.items():
        if step is not None:
            print(f"NOTE: d={size} float64 reference: non-finite from step {step}")
    if failures:
        print(f"FAIL: above tolerance: {', '.join(failures)}")
        return 1
    print("PASS: every finite implementation is within tolerance of the float64 reference in "
          "every cell")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())