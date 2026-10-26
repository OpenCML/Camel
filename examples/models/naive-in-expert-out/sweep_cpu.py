# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""The same sweep, timed in CPU time, with the kernel the run actually used.

`measure.py` is the sweep, and it reports the median wall time of the timed steps inside each
process; on the shared reference host that number carries the neighbours (the same d=256 step
reported 78-159 ms from one run to the next while the load moved between 12 and 135 on 32 hardware
threads).  This script asks the same question in the currency the host does not touch, on the *same
workload, at the same sizes, measured the same way*:

  * the same program text: `measure.py`'s `scaled_source` generates it from `train_step.cml` with
    exactly the extents of `shape_for(d)`, and the program's own `naive config` line is checked
    against the cell before any number is accepted;
  * the same sizes and the same cell definition (d_model 64, 256, 1024; `batch`/`sequence` growing
    with the width; `d_ff = 2 d`; 16 heads per 16 features; vocabulary 32; two blocks);
  * the same in-process wall number, kept and labelled as a wall number;
  * plus the step's CPU time by the method `test/cases/perf/step_budget.py` committed: the slope of
    the process's CPU seconds (user + system, from `/usr/bin/time -v`) against its step count over
    two run lengths, so the start-up and the host's preemptions cancel;
  * plus the step split by kernel from `std::profile`, whose operator rows are shares of the
    profiled round's own wall mean: the absolute value of a row is that share of the cell's
    measured CPU step, which is the committed "share inside one run, absolute value from the CPU
    step" normalisation.

It measures the two kernels the current tree can select, in the same window, in one interleaved
order per round:

  * `camel-library`    the tree's default: `tensor::fuse` records `tensor.kernel="library"` and,
                       with `CAMEL_TENSOR_BLAS` on and a CBLAS found, a matmul large enough runs on
                       the system CBLAS;
  * `camel-canonical`  the same build without the pass (`measure.py --no-tensor-fuse`): every
                       matmul stays on the built-in kernel, which is the path the recorded "before"
                       table measured.

`torchtrace.py` does the same for the torch counterpart at the sizes named by `--torch-d-model`, so
the d=1024 decomposition of the gap is measured in one window rather than taken from a table.

Usage (on the reference host, from the checkout root):

    python examples/models/naive-in-expert-out/sweep_cpu.py \
        --d-model 64,256,1024 --threads 1 --output /tmp/scale-cpu.json

`measure.py` is not modified: this file imports it, so the workload is the committed one.  What this
file does not do is build or test anything; that stays with `camel-remote.ps1`, which is the only
path that builds and runs the plans.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
sys.path.insert(0, str(HERE))

import measure  # noqa: E402  (the committed harness: shape_for, scaled_source, env, parsing)

# The step split the way test/cases/perf/step_budget.py committed it: the same generated program
# with its timed body replaced by the forward alone and by the forward plus the backward, so the
# backward and the update are the differences of three runs of the same text.  It is what makes a
# "how much of this step is the matrix multiply" claim a measurement rather than a subtraction.
def generate_phase(shape: dict, steps: int, warmup: int, phase: str) -> str:
    text = measure.scaled_source(shape, steps, warmup)[0]
    if phase == "forward":
        old = ("            let (value, gradient) = loss_grad<m>(x, labels)\n"
               "            let next = adam(m, gradient, st, lr)")
        if text.count(old) != 1:
            raise SystemExit("train_step.cml changed: the timed body is not the expected one")
        return text.replace(old, "            let value = loss<m>(x, labels)\n"
                                 "            let next = (m, st)")
    if phase == "forward-backward":
        old = "            let next = adam(m, gradient, st, lr)"
        if text.count(old) != 1:
            raise SystemExit("train_step.cml changed: the update is not the expected one")
        return text.replace(old, "            let next = (m, st)")
    return text

IMPLS = ("camel-library", "camel-canonical")

# The two run lengths per cell.  The difference is what the slope divides by: wider at the cheap
# cells, where start-up would otherwise be a large share of it, and narrower at d=1024, where a step
# costs seconds.  Both lengths run the same program text.
LENGTHS = {64: (64, 512), 256: (64, 256), 1024: (24, 56)}
# The two run lengths of the phase split.  Short on purpose: a phase is a difference of two CPU
# totals, and the profit comes from the ratio of two long runs, not from the length itself.
PHASE_LENGTHS = {64: (32, 128), 256: (16, 48), 1024: (8, 20)}
PROFILE_STEPS = 8
PROFILE_WARMUP = 3
TIMEOUT_S = 7200

MATMUL_URIS = ("tensor:matmul", "tensor:matmul_grad_lhs", "tensor:matmul_grad_rhs")
ELEMENTWISE_URIS = ("tensor:multiply", "tensor:add", "tensor:neg", "tensor:divide",
                    "tensor:gelu", "tensor:gelu_grad", "tensor:exp", "tensor:log", "tensor:sqrt")
REDUCE_URIS = ("tensor:sum_to", "tensor:mean_axis", "tensor:sum", "tensor:sum_axis")
LAYOUT_URIS = ("tensor:expand_axis", "tensor:permute", "tensor:permute_inverse",
               "tensor:transpose", "tensor:reshape", "tensor:broadcast_like", "tensor:one_hot",
               "tensor:shape", "tensor:dim", "tensor:numel", ":op/slice_sub", "tensor:argmax_axis")
CATEGORY_MEMBERS = {
    "matmul": MATMUL_URIS,
    "elementwise": ELEMENTWISE_URIS,
    "reductions": REDUCE_URIS,
    "layout/copy/creation": LAYOUT_URIS,
    "optimizer (Adam)": ("nn:adam",),
    "gradient assembly": (":tangent/scatter_add_arr", "tensor:zeros_arr"),
}


# ---------------------------------------------------------------------------
# Running


def cpu_of(stats: Path) -> float | None:
    """The process's own CPU seconds (user + system) from /usr/bin/time -v."""
    try:
        text = stats.read_text()
    except OSError:
        return None
    user = re.search(r"User time \(seconds\): ([0-9.]+)", text)
    system = re.search(r"System time \(seconds\): ([0-9.]+)", text)
    if not user or not system:
        return None
    return float(user.group(1)) + float(system.group(1))


def timed_run(command: list[str], env: dict, stats: Path) -> dict:
    """One process under /usr/bin/time -v: its CPU seconds, its wall, and the load it saw."""
    load_before = measure.load_average()
    started = time.perf_counter()
    result = subprocess.run(["/usr/bin/time", "-v", "-o", str(stats), *command], env=env,
                            text=True, encoding="utf-8", errors="replace",
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=TIMEOUT_S)
    wall = time.perf_counter() - started
    load_after = measure.load_average()
    if result.returncode != 0:
        raise SystemExit(f"the run failed ({result.returncode}): {command}\n"
                         f"{result.stdout[-2000:]}\n{result.stderr[-2000:]}")
    return {"command": command, "exit": result.returncode, "stdout": result.stdout,
            "stderr": result.stderr, "wall_seconds": wall, "cpu_seconds": cpu_of(stats),
            "load": {"before": load_before, "after": load_after}}


def median(values):
    ordered = sorted(values)
    if not ordered:
        return None
    return ordered[len(ordered) // 2]


def categories_ns(per_step: dict[str, float]) -> dict[str, float]:
    out = {}
    named: set[str] = set()
    for category, members in CATEGORY_MEMBERS.items():
        total = 0.0
        for uri in members:
            named.add(uri)
            if uri in per_step:
                total += per_step[uri]
        out[category] = total
    out["other autodiff"] = sum(ns for uri, ns in per_step.items() if uri not in named)
    return out


def check_config(row: dict, shape: dict) -> None:
    expected = {key: shape[key] for key in
                ("batch", "sequence", "d_model", "heads", "d_ff", "vocab")}
    expected["blocks"] = shape["blocks"]
    expected["params"] = shape["params"]
    if row["config"] != expected:
        raise SystemExit(f"the generated program ran {row['config']}, the cell is {expected}")


# ---------------------------------------------------------------------------
# One cell


def measure_cell(shape: dict, threads: int, work: Path, camel: Path, env: dict, repeats: int,
                 lengths: tuple[int, int], with_profiles: bool = True,
                 with_phases: bool = True) -> dict:
    d_model = shape["d_model"]
    tag = f"d{d_model}-t{threads}"
    cell_dir = work / tag
    init = cell_dir / "init"
    init.mkdir(parents=True, exist_ok=True)

    short, long = lengths
    phase_short, phase_long = PHASE_LENGTHS[d_model]
    programs: dict[str, Path] = {}
    generated: dict[str, dict] = {}

    def generate(key: str, text: str, substitutions: list, note: str) -> None:
        path = cell_dir / f"train_step_{key}.cml"
        path.write_text(text, encoding="utf-8")
        programs[key] = path
        generated[key] = {"note": note, "sha256": measure.sha256_text(text),
                          "substitutions": substitutions}

    for steps in (short, long, PROFILE_STEPS):
        source, substitutions = measure.scaled_source(shape, steps, PROFILE_WARMUP)
        generate(f"s{steps}_w{PROFILE_WARMUP}", source, substitutions,
                 f"the full step, {steps} steps, warmup {PROFILE_WARMUP}")
    for phase in ("forward", "forward-backward"):
        for steps in (phase_short, phase_long):
            source, substitutions = measure.scaled_source(shape, steps, PROFILE_WARMUP)
            generate(f"{phase}_s{steps}_w{PROFILE_WARMUP}",
                     generate_phase(shape, steps, PROFILE_WARMUP, phase), substitutions,
                     f"{phase} only, {steps} steps, warmup {PROFILE_WARMUP}")

    def command_for(key: str, impl: str, final: str) -> list[str]:
        passes = [] if impl == "camel-canonical" else ["tensor::fuse"]
        return [str(camel), "-t", str(threads), "--block-limit", "8192", str(programs[key]),
                *passes, "std::jit", "--arg", "bench=true", "--arg", f"init_dir={init}",
                "--arg", f"final_dir={final}"]

    # The initial parameters and data, written once by the default path and then read by every run
    # of both kernels, so the two kernels start from the same numbers (measure.py's protocol).
    print(f"  d={d_model} t={threads} init", flush=True)
    init_run = timed_run(command_for(f"s{PROFILE_STEPS}_w{PROFILE_WARMUP}", "camel-library",
                                     str(cell_dir / "init-final")),
                         env, cell_dir / "init-time.txt")

    runs: dict[str, dict[int, list[dict]]] = {impl: {short: [], long: []} for impl in IMPLS}
    for round_index in range(repeats):
        order = [(short, "camel-library"), (short, "camel-canonical"),
                 (long, "camel-library"), (long, "camel-canonical")]
        if round_index % 2:
            order = order[::-1]
        for steps, impl in order:
            directory = cell_dir / f"{impl}-s{steps}-r{round_index}"
            directory.mkdir(parents=True, exist_ok=True)
            # Neither kernel writes its parameters during the measurement: writing them is disk
            # work the wall table did not carry either, and the run starts from `init` every time.
            print(f"  d={d_model} t={threads} {impl} steps={steps} round {round_index}", flush=True)
            raw = timed_run(command_for(f"s{steps}_w{PROFILE_WARMUP}", impl, ""), env,
                            directory / "time.txt")
            if raw["cpu_seconds"] is None:
                raise SystemExit(f"/usr/bin/time gave no CPU time for {tag} {impl}")
            row = measure.parse_camel(raw["stdout"], steps, PROFILE_WARMUP)
            check_config(row, shape)
            row["cpu_seconds"] = raw["cpu_seconds"]
            row["wall_seconds"] = raw["wall_seconds"]
            row["load"] = raw["load"]
            runs[impl][steps].append(row)

    kernels = {}
    for impl in IMPLS:
        slopes, pairs = [], []
        for round_index in range(repeats):
            a = runs[impl][short][round_index]
            b = runs[impl][long][round_index]
            slope = (b["cpu_seconds"] - a["cpu_seconds"]) / (long - short) * 1000.0
            slopes.append(slope)
            pairs.append({"round": round_index, "short_cpu_s": a["cpu_seconds"],
                          "long_cpu_s": b["cpu_seconds"], "short_wall_s": a["wall_seconds"],
                          "long_wall_s": b["wall_seconds"], "slope_ms": slope})
        kernels[impl] = {
            "impl": impl,
            "cpu_ms_per_step": median(slopes),
            "cpu_ms_per_step_samples": slopes,
            "slope_pairs": pairs,
            "wall_ms_per_step": {
                "short": [row["median_ms"] for row in runs[impl][short]],
                "long": [row["median_ms"] for row in runs[impl][long]],
                "median_short": median([row["median_ms"] for row in runs[impl][short]]),
                "median_long": median([row["median_ms"] for row in runs[impl][long]]),
            },
            "long_run_wall_s": [row["wall_seconds"] for row in runs[impl][long]],
            "load": [row["load"] for row in runs[impl][short] + runs[impl][long]],
            "command": runs[impl][short][0].get("command"),
        }

    def profile_of(key: str, impl: str, round_index: int) -> dict:
        directory = cell_dir / f"profile-{impl}-{key}-r{round_index}"
        directory.mkdir(parents=True, exist_ok=True)
        report = directory / "profile.json"
        passes = ["tensor::fuse", "std::profile", "report=true", f'json="{report.as_posix()}"',
                  "top=0", "std::jit"]
        if impl == "camel-canonical":
            passes = passes[1:]
        command = [str(camel), "-t", str(threads), "--block-limit", "8192", str(programs[key]),
                   *passes, "--arg", "bench=false", "--arg", f"init_dir={init}",
                   "--arg", "final_dir="]
        print(f"  d={d_model} t={threads} profile {impl} {key} round {round_index}", flush=True)
        raw = timed_run(command, env, directory / "time.txt")
        if not report.exists():
            raise SystemExit(f"std::profile wrote no report for {tag} {impl} {key}")
        doc = json.loads(report.read_text(encoding="utf-8"))
        report.unlink()
        rounds = doc["step"]["rounds"]
        rows = {row["uri"]: row for row in doc["operator_rows"]}
        per_step = {uri: row["ns"] / rounds for uri, row in rows.items()}
        round_mean = doc["step"]["wall_per_round_ns"] / 1e6
        return {"rounds": rounds,
                "round_mean_ms": round_mean,
                "row_sum_ms": sum(per_step.values()) / 1e6,
                "rows_over_round": sum(per_step.values()) / 1e6 / round_mean,
                "categories_ns": categories_ns(per_step),
                "matmul_rows_ns": {uri: per_step[uri] for uri in MATMUL_URIS if uri in per_step},
                "operators_per_round": doc["operators"]["calls"] / rounds,
                "cpu_seconds": raw["cpu_seconds"], "wall_seconds": raw["wall_seconds"],
                "load": raw["load"]}

    for impl in IMPLS:
        profiles = ([profile_of(f"s{PROFILE_STEPS}_w{PROFILE_WARMUP}", impl, index)
                     for index in range(repeats)] if with_profiles else [])
        kernels[impl]["profile"] = profiles
        if not profiles:
            continue
        # The first round's rows, kept alone: the kernel the run used, observed rather than
        # assumed.  tensor::fuse writes tensor.kernel on a product the 7.3 rule permits, and a run
        # without the pass has no such attribute at all, so the attribute's presence is the
        # difference the two columns of this table measure.
        kernels[impl]["kernels_used"] = {"operators_per_round": profiles[0]["operators_per_round"],
                                         "matmul_rows_ns": profiles[0]["matmul_rows_ns"],
                                         "round_mean_ms": profiles[0]["round_mean_ms"],
                                         "row_sum_ms": profiles[0]["row_sum_ms"],
                                         "rows_over_round": profiles[0]["rows_over_round"]}
        round_mean = median([entry["round_mean_ms"] for entry in profiles])
        shares = {category: median([entry["categories_ns"][category] / 1e6 /
                                    entry["round_mean_ms"] for entry in profiles])
                  for category in profiles[0]["categories_ns"]}
        kernels[impl]["profile_round_mean_ms"] = round_mean
        kernels[impl]["profile_over_cpu_ms"] = round_mean - kernels[impl]["cpu_ms_per_step"]
        kernels[impl]["shares"] = shares
        kernels[impl]["share_sum"] = sum(shares.values())
        kernels[impl]["category_ms_per_step"] = {
            category: share * kernels[impl]["cpu_ms_per_step"] for category, share in shares.items()}

    # The phase split: the same program text with the timed body cut after the forward / after the
    # backward, so the backward and the update are differences of three runs of one text.
    for impl in IMPLS:
        phases = {}
        if not with_phases:
            kernels[impl]["phases"] = {}
            kernels[impl]["phase_split_ms"] = None
            continue
        for phase in ("forward", "forward-backward"):
            rows = {phase_short: [], phase_long: []}
            for steps in (phase_short, phase_long):
                key = f"{phase}_s{steps}_w{PROFILE_WARMUP}"
                for round_index in range(repeats):
                    directory = cell_dir / f"phase-{phase}-{impl}-s{steps}-r{round_index}"
                    directory.mkdir(parents=True, exist_ok=True)
                    print(f"  d={d_model} t={threads} phase {phase} {impl} steps={steps} "
                          f"round {round_index}", flush=True)
                    raw = timed_run(command_for(key, impl, ""), env, directory / "time.txt")
                    row = measure.parse_camel(raw["stdout"], steps, PROFILE_WARMUP)
                    check_config(row, shape)
                    row["cpu_seconds"] = raw["cpu_seconds"]
                    row["load"] = raw["load"]
                    row["wall_seconds"] = raw["wall_seconds"]
                    rows[steps].append(row)
            slopes = [(rows[phase_long][index]["cpu_seconds"] - rows[phase_short][index]["cpu_seconds"])
                      / (phase_long - phase_short) * 1000.0 for index in range(repeats)]
            phases[phase] = {"cpu_ms_per_step": median(slopes), "samples": slopes,
                             "wall_ms_per_step": {
                                 "short": [row["median_ms"] for row in rows[phase_short]],
                                 "long": [row["median_ms"] for row in rows[phase_long]]},
                             "load": [row["load"] for row in rows[phase_short] + rows[phase_long]]}
        forward = phases["forward"]["cpu_ms_per_step"]
        through_backward = phases["forward-backward"]["cpu_ms_per_step"]
        # The full step is the cell's own headline number: the same two run lengths, already
        # measured above, so the split is the difference of three slopes with no fourth run.
        full = kernels[impl]["cpu_ms_per_step"]
        phases["full"] = {"cpu_ms_per_step": full,
                          "samples": kernels[impl]["cpu_ms_per_step_samples"],
                          "note": "the cell's full-step slope above, at steps "
                                  f"{short}/{long}"}
        kernels[impl]["phases"] = phases
        kernels[impl]["phase_split_ms"] = {
            "forward": forward, "backward": through_backward - forward,
            "optimizer_and_rest": full - through_backward, "full": full,
            "residual_of_split": full - through_backward - (through_backward - forward) - forward,
            "note": "the full step is the slope at the cell's own run lengths "
                    f"({short}/{long}); the phases are the slopes at "
                    f"{phase_short}/{phase_long}.  The three are differences of separate runs, so "
                    "the residual is the noise of that subtraction, not a missing phase.",
        }

    return {
        "d_model": d_model, "threads": threads, "batch": shape["batch"],
        "sequence": shape["sequence"], "tokens": shape["tokens"], "heads": shape["heads"],
        "d_ff": shape["d_ff"], "vocab": shape["vocab"], "blocks": shape["blocks"],
        "params": shape["params"], "generated": generated, "kernels": kernels,
        "init": {"command": init_run["command"], "cpu_seconds": init_run["cpu_seconds"],
                 "wall_seconds": init_run["wall_seconds"], "load": init_run["load"]},
        "load_at_measurement": measure.load_average(),
    }


# ---------------------------------------------------------------------------
# The torch counterpart, in the same two currencies


def measure_torch_python() -> str:
    override = os.environ.get("CAMEL_TORCH_PYTHON")
    if override:
        return override
    candidate = Path.home() / "camel-eval/venv-bench/bin/python"
    return str(candidate) if candidate.exists() else sys.executable


def torch_cell(shape: dict, threads: int, work: Path, env: dict, init: Path, repeats: int,
               variants: tuple[str, ...], trace_steps: int) -> dict:
    d_model = shape["d_model"]
    short, long = LENGTHS[d_model]
    python = measure_torch_python()
    shape_args = ["--threads", str(threads), "--batch", str(shape["batch"]),
                  "--sequence", str(shape["sequence"]), "--d-model", str(d_model),
                  "--n-heads", str(shape["heads"]), "--d-ff", str(shape["d_ff"]),
                  "--vocab", str(shape["vocab"])]
    out = {}
    for variant in variants:
        rows = []
        for steps in (short, long):
            for round_index in range(repeats):
                directory = (work / f"d{d_model}-t{threads}" /
                             f"torch-{variant}-s{steps}-r{round_index}")
                directory.mkdir(parents=True, exist_ok=True)
                command = [python, str(measure.TORCH_IMPL), "--variant", variant,
                           "--init", str(init), "--steps", str(steps),
                           "--warmup", str(PROFILE_WARMUP), *shape_args]
                print(f"  d={d_model} t={threads} torch {variant} steps={steps} "
                      f"round {round_index}", flush=True)
                raw = timed_run(command, measure.torch_env(env, threads), directory / "time.txt")
                if raw["cpu_seconds"] is None:
                    raise SystemExit(f"/usr/bin/time gave no CPU time for torch {variant}")
                parsed = measure.parse_torch(raw["stdout"])
                parsed["cpu_seconds"] = raw["cpu_seconds"]
                parsed["wall_seconds"] = raw["wall_seconds"]
                parsed["load"] = raw["load"]
                rows.append((steps, parsed))
        slopes = []
        for round_index in range(repeats):
            a = [row for step, row in rows if step == short][round_index]
            b = [row for step, row in rows if step == long][round_index]
            slopes.append((b["cpu_seconds"] - a["cpu_seconds"]) / (long - short) * 1000.0)
        out[variant] = {
            "cpu_ms_per_step": median(slopes), "cpu_ms_per_step_samples": slopes,
            "wall_ms_per_step": {
                "short": [row["median_ms"] for step, row in rows if step == short],
                "long": [row["median_ms"] for step, row in rows if step == long]},
            "cpu_seconds": {str(step): [row["cpu_seconds"] for s, row in rows if s == step]
                            for step in (short, long)},
            "wall_seconds": {str(step): [row["wall_seconds"] for s, row in rows if s == step]
                             for step in (short, long)},
            "load": [row["load"] for _, row in rows],
        }

        # The profile of the same variant at the same cell: torch.profiler's self times grouped
        # into the committed categories, plus the process's own CPU seconds for the same run, so a
        # row's share is carried onto the CPU step exactly as the Camel rows are.
        trace_dir = work / f"d{d_model}-t{threads}" / f"torchtrace-{variant}"
        trace_dir.mkdir(parents=True, exist_ok=True)
        command = [python, str(HERE / "torchtrace.py"), "--variant", variant,
                   "--init", str(init), "--steps", str(trace_steps),
                   "--warmup", str(PROFILE_WARMUP), *shape_args]
        print(f"  d={d_model} t={threads} torchtrace {variant} steps={trace_steps}", flush=True)
        raw = timed_run(command, measure.torch_env(env, threads), trace_dir / "time.txt")
        parsed = measure.parse_torch(raw["stdout"])
        out[variant]["trace"] = {
            "steps": trace_steps, "warmup": PROFILE_WARMUP,
            "median_ms": parsed["median_ms"],
            "cpu_seconds": raw["cpu_seconds"], "wall_seconds": raw["wall_seconds"],
            "load": raw["load"], "categories_us": parsed["profiled_us"]["categories"],
            "rows": parsed["profiled_us"]["rows"],
            "shape": parsed["shape"],
        }
    return {"d_model": d_model, "threads": threads, "variants": out,
            "shape": {key: shape[key] for key in ("batch", "sequence", "d_model", "heads",
                                                  "d_ff", "vocab", "blocks")},
            "load_at_measurement": measure.load_average()}


# ---------------------------------------------------------------------------
# The gap and its decomposition


def torch_step_cpu(row: dict) -> dict:
    """A torch variant's profiled categories, carried onto its measured CPU step.

    The instrument's own overhead is the factor: the trace run's CPU seconds per step over what the
    profiler reports per step.  Every category is scaled by the same factor, which is what keeps a
    share comparable between two profilers with different overhead (the committed d=1024 profile's
    method).
    """
    trace = row["trace"]
    cand = trace["categories_us"]
    per_step = {category: us / 1000.0 / trace["steps"] for category, us in cand.items()}
    profiled_total = sum(per_step.values())
    cpu_total = trace["cpu_seconds"] * 1000.0 / trace["steps"]
    factor = cpu_total / profiled_total if profiled_total else None
    return {
        "trace_steps": trace["steps"],
        "cpu_ms_per_step": cpu_total,
        "profiled_ms_per_step": profiled_total,
        "cpu_over_profiled": factor,
        "categories_ms_per_step": {category: ms * factor for category, ms in per_step.items()}
        if factor else per_step,
        "load": trace["load"],
    }


def gap_analysis(result: dict) -> dict:
    out = {}
    by_cell = {(cell["d_model"], cell["threads"]): cell for cell in result["cells"]}
    for entry in result["torch"]:
        key = f"d={entry['d_model']} t={entry['threads']}"
        cell = by_cell.get((entry["d_model"], entry["threads"]))
        if cell is None:
            continue
        torch_side = {variant: torch_step_cpu(data) for variant, data in entry["variants"].items()}
        camel_side = {}
        for impl in cell["kernels"]:
            camel_side[impl] = {
                "cpu_ms_per_step": cell["kernels"][impl]["cpu_ms_per_step"],
                "categories_ms_per_step": cell["kernels"][impl]["category_ms_per_step"],
                "shares": cell["kernels"][impl]["shares"],
            }
        decomposition = {}
        for impl, camel in camel_side.items():
            for variant, torch_row in torch_side.items():
                gap = camel["cpu_ms_per_step"] - torch_row["cpu_ms_per_step"]
                matmul_gap = (camel["categories_ms_per_step"]["matmul"] -
                              torch_row["categories_ms_per_step"]["matmul"])
                decomposition[f"{impl} vs {variant}"] = {
                    "camel_cpu_ms": camel["cpu_ms_per_step"],
                    "torch_cpu_ms": torch_row["cpu_ms_per_step"],
                    "gap_ms": gap,
                    "camel_matmul_ms": camel["categories_ms_per_step"]["matmul"],
                    "torch_matmul_ms": torch_row["categories_ms_per_step"]["matmul"],
                    "matmul_gap_ms": matmul_gap,
                    "matmul_share_of_gap": matmul_gap / gap if gap else None,
                    "camel_categories_ms": camel["categories_ms_per_step"],
                    "torch_categories_ms": torch_row["categories_ms_per_step"],
                }
        out[key] = {"torch": torch_side, "camel": camel_side, "decomposition": decomposition}
    return out


def write(result: dict, path: str) -> None:
    Path(path).write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")


def print_summary(result: dict) -> None:
    print()
    machine = result["machine"]
    print(f"machine: {machine['host']} | {machine['cpu']} | {machine['hardware_threads']} threads "
          f"| torch {machine['torch']} | load now {measure.load_average()}")
    build = result["build"]
    print(f"binary: {str(build.get('revision'))[:12]} | {build.get('builtAt')} | fingerprint "
          f"{str(build.get('fingerprint'))[:12]}")
    print()
    print("| d | t | kernel | CPU ms/step (slope) | samples | wall ms/step short/long | matmul ms "
          "(share) |")
    print("|---|---|---|---|---|---|---|")
    for cell in result["cells"]:
        for impl in IMPLS:
            entry = cell["kernels"][impl]
            wall = entry["wall_ms_per_step"]
            print(f"| {cell['d_model']} | {cell['threads']} | {impl} | "
                  f"{entry['cpu_ms_per_step']:.3f} | "
                  f"{[round(value, 2) for value in entry['cpu_ms_per_step_samples']]} | "
                  f"{wall['median_short']:.3f} / {wall['median_long']:.3f} | "
                  f"{entry['category_ms_per_step']['matmul']:.3f} "
                  f"({entry['shares']['matmul']:.4f}) |")
    print()
    print("| d | t | kernel | forward | backward | optimizer+rest | full | profiled round mean "
          "| profiler excess | profiler excess % of round |")
    print("|---|---|---|---|---|---|---|---|---|---|")
    for cell in result["cells"]:
        for impl in IMPLS:
            entry = cell["kernels"][impl]
            if not entry.get("phase_split_ms"):
                continue
            split = entry["phase_split_ms"]
            round_mean = entry["profile_round_mean_ms"]
            excess = entry["profile_over_cpu_ms"]
            print(f"| {cell['d_model']} | {cell['threads']} | {impl} | {split['forward']:.3f} | "
                  f"{split['backward']:.3f} | {split['optimizer_and_rest']:.3f} | "
                  f"{split['full']:.3f} | {round_mean:.3f} | {excess:.3f} | "
                  f"{100.0 * excess / round_mean:.1f}% |")
    for key, entry in result.get("gap", {}).items():
        print(f"\ngap {key}:")
        for name, row in entry["decomposition"].items():
            print(f"  {name}: camel {row['camel_cpu_ms']:.3f} vs torch {row['torch_cpu_ms']:.3f} "
                  f"=> gap {row['gap_ms']:.3f} ms; matmul {row['camel_matmul_ms']:.3f} vs "
                  f"{row['torch_matmul_ms']:.3f} => {row['matmul_gap_ms']:.3f} ms "
                  f"({row['matmul_share_of_gap']:.4f} of the gap)")
    print("\nload average at each cell (1/5/15):")
    for cell in result["cells"]:
        print(f"  d={cell['d_model']} t={cell['threads']}: {cell['load_at_measurement']}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--d-model", default="64,256,1024")
    parser.add_argument("--threads", default="1")
    parser.add_argument("--repeats", type=int, default=2,
                        help="runs of each length per kernel (the slope samples)")
    parser.add_argument("--output", default="/tmp/camel-scale-cpu.json")
    parser.add_argument("--work", default="/tmp/camel-scale-cpu-work")
    parser.add_argument("--camel", default=str(measure.BENCH))
    parser.add_argument("--torch-variants", default="compiled",
                        help="comma-separated torch variants measured beside Camel, or 'none'")
    parser.add_argument("--torch-d-model", default="1024")
    parser.add_argument("--trace-steps", type=int, default=6)
    parser.add_argument("--no-profiles", action="store_true",
                        help="skip the std::profile rounds (the CPU and wall slopes only)")
    parser.add_argument("--no-phases", action="store_true",
                        help="skip the forward / forward+backward phase runs")
    args = parser.parse_args()

    sizes = [int(part) for part in args.d_model.split(",") if part.strip()]
    threads = [int(part) for part in args.threads.split(",") if part.strip()]
    torch_variants = tuple(part.strip() for part in args.torch_variants.split(",")
                           if part.strip() and part.strip() != "none")
    torch_sizes = {int(part) for part in args.torch_d_model.split(",") if part.strip()}
    camel = Path(args.camel)
    if not camel.exists():
        raise SystemExit(f"Camel binary not found: {camel}")
    work = Path(args.work)
    work.mkdir(parents=True, exist_ok=True)
    env = measure.camel_env(camel)

    result = {
        "schema": 1,
        "what": "measure.py's sweep timed in CPU time as well as wall, with the kernel the run "
                "used and the decomposition of the d=1024 gap, on the shared reference host",
        "generated_at": dt.datetime.now().astimezone().isoformat(timespec="seconds"),
        "machine": measure.machine(), "build": measure.build_stamp(camel),
        "git": measure.git_state(),
        "protocol": {
            "harness": "measure.py's shape_for, scaled_source and config check, plus "
                       "/usr/bin/time -v and std::profile",
            "cpu_method": "slope of the process's CPU seconds (user + system) against the step "
                          "count over two run lengths; the start-up cancels",
            "wall_method": "the median of the timed steps inside the process (measure.py's own "
                           "number), kept and labelled as wall",
            "shares": "std::profile operator ns over the profiled round's own wall mean; a row's "
                      "absolute value is its share of the cell's measured CPU step",
            "lengths": LENGTHS, "profile_steps": PROFILE_STEPS, "repeats": args.repeats,
            "sizes": sizes, "threads": threads, "torch_variants": list(torch_variants),
            "torch_sizes": sorted(torch_sizes), "trace_steps": args.trace_steps,
        },
        "cells": [], "torch": [],
    }

    for d_model in sizes:
        shape = measure.shape_for(d_model)
        for thread_count in threads:
            print(f"cell d={d_model} t={thread_count}", flush=True)
            result["cells"].append(measure_cell(shape, thread_count, work, camel, env,
                                                args.repeats, LENGTHS[d_model],
                                                with_profiles=not args.no_profiles,
                                                with_phases=not args.no_phases))
            write(result, args.output)
            if torch_variants and d_model in torch_sizes:
                init = work / f"d{d_model}-t{thread_count}" / "init"
                result["torch"].append(torch_cell(shape, thread_count, work, env, init,
                                                  args.repeats, torch_variants, args.trace_steps))
                write(result, args.output)

    result["gap"] = gap_analysis(result)
    write(result, args.output)
    print_summary(result)
    print(f"\nresult file: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())