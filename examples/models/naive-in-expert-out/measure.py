# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""Measure the four implementations of one Transformer training step.

The Camel program writes its starting weights and data as .npy files; every PyTorch variant
loads exactly those, so all four start from the same numbers.  The reference is the same
mathematics in float64.

One command, one machine-readable file:

  python examples/models/naive-in-expert-out/measure.py
  python examples/models/naive-in-expert-out/measure.py --output /tmp/results.json --repeats 3

The step time is the median of the timed steps, warmup excluded; it is measured inside each
program, around one forward, backward and Adam update.  The four implementations are measured
in interleaved rounds (Camel, naive, expert, compiled, then again) and the table reports the
median of the round medians together with the spread, because one number from a shared machine
is not a comparison.  Peak memory is the whole process's maximum resident set, measured by
/usr/bin/time, so it includes interpreter startup and the allocator, not just the model.  The
machine, the thread count, the versions and the Camel commit are recorded in the output because
a time without them is not a measurement.

The thread budget is the command line's ``-t``, for every pool of the run.  The runtime no
longer reads ``CAMEL_NUM_THREADS``; this harness passes ``-t 1`` to Camel and
``torch.set_num_threads(1)`` plus the usual OMP/MKL variables to PyTorch, so both sides run one
thread by their own documented mechanism.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
BENCH = REPO / "out" / "latest" / "bin" / "camel"
TORCH_IMPL = HERE / "torch_impl.py"

BATCH, SEQUENCE, D_MODEL, N_HEADS, D_FF, VOCAB = 8, 16, 64, 4, 128, 32
STEPS, WARMUP, LR = 16, 3, 0.003
PARAMS = 68288
TOL_LOSS = 1e-5       # absolute, final loss against the float64 reference
TOL_PARAM = 2e-4      # absolute, any single final parameter against float64
THREADS = 1
REPEATS = 3           # interleaved rounds; the spread across them is part of the result

CANONICAL = [
    f"blocks.{block}.{name}"
    for block in (0, 1)
    for name in ("norm1", "wq", "wk", "wv", "wo", "norm2", "w1", "b1", "w2", "b2")
] + ["norm_f", "head"]

VARIANTS = ("camel", "naive", "expert", "compiled")


def camel_env(camel: Path) -> dict[str, str]:
    # The install root is the parent of bin/, whether the binary was built in this worktree or
    # named from another checkout with --camel.  Nothing here sets a thread count: the thread
    # budget is the command line's -t, and CAMEL_NUM_THREADS is no longer read by the runtime.
    install = camel.resolve().parents[1]
    env = os.environ.copy()
    env["CAMEL_HOME"] = str(install)
    env["PATH"] = str(install / "bin") + os.pathsep + env.get("PATH", "")
    return env


def torch_env(env: dict[str, str]) -> dict[str, str]:
    # PyTorch's own mechanism, the counterpart of -t 1: torch_impl also calls set_num_threads,
    # and these keep the BLAS back ends from opening pools behind its back.
    out = env.copy()
    for name in ("OMP_NUM_THREADS", "MKL_NUM_THREADS", "OPENBLAS_NUM_THREADS"):
        out[name] = str(THREADS)
    return out


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


def parse_camel(stdout: str) -> dict:
    init_loss = None
    final_loss = None
    losses: dict[int, float] = {}
    step_ms: dict[int, float] = {}
    for line in stdout.splitlines():
        match = re.match(r"naive init loss=([0-9.eE+-]+)", line)
        if match:
            init_loss = float(match.group(1))
        match = re.match(r"naive step=(\d+) loss=([0-9.eE+-]+)", line)
        if match:
            losses[int(match.group(1))] = float(match.group(2))
        match = re.match(r"naive bench step=(\d+) ms=([0-9.eE+-]+)", line)
        if match:
            step_ms[int(match.group(1))] = float(match.group(2))
        match = re.match(r"naive final steps=(\d+) loss=([0-9.eE+-]+)", line)
        if match:
            final_loss = float(match.group(2))
    if init_loss is None or final_loss is None or len(losses) != STEPS:
        raise SystemExit(f"could not parse the Camel output:\n{stdout}")
    ordered = [losses[i] for i in sorted(losses)]
    times = [step_ms[i] for i in sorted(step_ms) if i >= WARMUP]
    return {
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
        "cpus": os.cpu_count(),
        "memory_bytes": memory,
        "threads": THREADS,
        "python": sys.version.split()[0],
        "numpy": np.__version__,
        "torch": __import__("torch").__version__,
    }


def median_ms(runs: list[dict]) -> float:
    return float(np.median([run["median_ms"] for run in runs]))


def spread_ms(runs: list[dict]) -> dict:
    values = [run["median_ms"] for run in runs]
    return {"min": float(min(values)), "max": float(max(values)),
            "spread": float(max(values) - min(values))}


def load_average() -> dict:
    """The host's load average, so a reader can see what else was running."""
    try:
        fields = Path("/proc/loadavg").read_text().split()
        return {"1m": float(fields[0]), "5m": float(fields[1]), "15m": float(fields[2])}
    except (OSError, ValueError, IndexError):
        return {"1m": None, "5m": None, "15m": None}


def table(rows: list[tuple], header: tuple) -> str:
    lines = ["| " + " | ".join(str(cell) for cell in header) + " |",
             "|" + "|".join("---" for _ in header) + "|"]
    for row in rows:
        lines.append("| " + " | ".join(str(cell) for cell in row) + " |")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", default=str(HERE / "results.json"))
    parser.add_argument("--camel", default=str(BENCH))
    parser.add_argument("--backend", default="jit", choices=("nvm", "fvm", "jit"))
    parser.add_argument("--repeats", type=int, default=REPEATS,
                        help="interleaved rounds of the four implementations (default: 3)")
    parser.add_argument("--work", default=None, help="scratch directory (default: a temp dir)")
    args = parser.parse_args()
    if args.repeats < 1:
        raise SystemExit("--repeats must be at least 1")

    if not Path(args.camel).exists():
        raise SystemExit(f"Camel binary not found: {args.camel} (build first, or pass --camel)")
    try:
        import torch  # noqa: F401
    except ImportError:
        raise SystemExit("PyTorch is required for the three counterparts (run in the torch venv)")

    work = Path(args.work) if args.work else Path(tempfile.mkdtemp(prefix="naive-in-out-"))
    init, finals = work / "init", work / "final"
    for directory in (init, finals):
        if directory.exists():
            shutil.rmtree(directory)
        directory.mkdir(parents=True)
    camel_environment = camel_env(Path(args.camel))
    torch_environment = torch_env(camel_environment)

    # The exact command lines, so the result file is reproducible without reading this script.
    camel_command = [
        args.camel, "-t", str(THREADS), str(HERE / "train_step.cml"),
        f"std::{args.backend}",
        "--arg", "bench=true",
        "--arg", f"init_dir={init}",
        "--arg", f"final_dir={finals / 'camel'}",
    ]
    torch_commands = {
        variant: [
            sys.executable, str(TORCH_IMPL), "--variant", variant,
            "--init", str(init), "--steps", str(STEPS), "--warmup", str(WARMUP),
            "--threads", str(THREADS), "--save-final", str(finals / f"{variant}.npz"),
        ]
        for variant in VARIANTS[1:] + ("reference",)
    }

    def run_variant(variant: str, round_index: int, save_final: bool) -> dict:
        directory = work / f"round{round_index}"
        directory.mkdir(parents=True, exist_ok=True)
        if variant == "camel":
            command = [
                args.camel, "-t", str(THREADS), str(HERE / "train_step.cml"),
                f"std::{args.backend}",
                "--arg", "bench=true", "--arg", f"init_dir={init}",
                "--arg", f"final_dir={finals / 'camel' if save_final else ''}",
            ]
            print(f"camel: std::{args.backend} -t {THREADS} (round {round_index})")
            result = run(command, camel_environment, stats=directory / "camel.time")
            if result["exit"] != 0:
                sys.stderr.write(result["stdout"] + result["stderr"])
                raise SystemExit("the Camel program failed")
            parsed = parse_camel(result["stdout"])
            parsed["backend"] = args.backend
            parsed["params"] = load_npy_leaves(finals / "camel") if save_final else None
        else:
            command = list(torch_commands[variant])
            if not save_final and variant != "reference":
                index = command.index("--save-final")
                del command[index:index + 2]
            print(f"torch: {variant} (round {round_index})")
            result = run(command, torch_environment, stats=directory / f"{variant}.time")
            if result["exit"] != 0:
                sys.stderr.write(result["stdout"] + result["stderr"])
                raise SystemExit(f"the {variant} counterpart failed")
            parsed = parse_torch(result["stdout"])
            parsed["timed_steps"] = STEPS - WARMUP
            parsed["params"] = load_npz(finals / f"{variant}.npz") if save_final else None
        parsed["peak_bytes"] = result["peak_bytes"]
        parsed["command"] = command
        parsed["round"] = round_index
        return parsed

    # Interleaved rounds with a rotating order: each round measures the four in a different
    # order, so neither a warm cache nor a load spike that lasts seconds belongs to one row.
    runs: dict[str, list[dict]] = {name: [] for name in VARIANTS}
    reference = None
    load_start = load_average()
    for round_index in range(args.repeats):
        last = round_index == args.repeats - 1
        offset = round_index % len(VARIANTS)
        order = VARIANTS[offset:] + VARIANTS[:offset]
        for variant in order:
            runs[variant].append(run_variant(variant, round_index, last))
        if reference is None:
            reference = run_variant("reference", round_index, True)
    load_end = load_average()

    # Determinism check: every round must reach the same loss and the same parameters.
    for variant in VARIANTS:
        losses = [run["final_loss"] for run in runs[variant]]
        if max(losses) - min(losses) > 1e-12:
            raise SystemExit(f"{variant} is not deterministic across rounds: {losses}")

    reference_params = reference["params"]
    summary = {}
    for variant in VARIANTS:
        last = runs[variant][-1]
        summary[variant] = {
            "backend": "std::" + args.backend if variant == "camel" else "torch",
            "init_loss": last["init_loss"],
            "final_loss": last["final_loss"],
            "losses": last["losses"],
            "step_ms": last["step_ms"],
            "timed_steps": last["timed_steps"],
            "median_ms": median_ms(runs[variant]),
            "pooled_median_ms": float(np.median(
                [t for run in runs[variant] for t in run["step_ms"][WARMUP:]])),
            "mean_ms": float(np.mean([run["mean_ms"] for run in runs[variant]])),
            "spread": spread_ms(runs[variant]),
            "peak_bytes": max(run["peak_bytes"] for run in runs[variant]),
            "vs_reference": compare(last["params"], reference_params),
            "loss_vs_reference": abs(last["final_loss"] - reference["final_loss"]),
            "rounds": [
                {
                    "median_ms": run["median_ms"], "mean_ms": run["mean_ms"],
                    "final_loss": run["final_loss"], "peak_bytes": run["peak_bytes"],
                    "step_ms": run["step_ms"], "command": run["command"],
                }
                for run in runs[variant]
            ],
        }

    pairwise = {}
    for left in VARIANTS:
        for right in VARIANTS:
            if left >= right:
                continue
            pairwise[f"{left}:{right}"] = {
                "max_abs_param": compare(runs[left][-1]["params"],
                                         runs[right][-1]["params"])["max_abs"],
                "loss": abs(summary[left]["final_loss"] - summary[right]["final_loss"]),
            }

    result = {
        "schema": 2,
        "generated_at": dt.datetime.now().astimezone().isoformat(timespec="seconds"),
        "machine": machine(),
        "git": git_state(),
        "build": build_stamp(Path(args.camel)),
        "camel_version": subprocess.run(
            [args.camel, "--version"], capture_output=True, text=True,
            env=camel_environment).stdout.strip(),
        "commands": {
            "camel": " ".join(camel_command),
            "torch": {name: " ".join(command) for name, command in torch_commands.items()},
        },
        "measurement": {
            "repeats": args.repeats,
            "interleaved": True,
            "order": "rotating: each round starts with the next implementation",
            "steps": STEPS,
            "warmup": WARMUP,
            "timed_steps_per_round": STEPS - WARMUP,
            "threads": THREADS,
            "thread_control": "camel: -t on the command line; torch: set_num_threads and OMP/MKL",
            "load_average": {"start": load_start, "end": load_end},
        },
        "model": {
            "batch": BATCH, "sequence": SEQUENCE, "d_model": D_MODEL, "heads": N_HEADS,
            "d_ff": D_FF, "vocab": VOCAB, "blocks": 2, "params": PARAMS,
            "steps": STEPS, "warmup": WARMUP, "lr": LR,
        },
        "tolerance": {"loss_abs": TOL_LOSS, "param_abs": TOL_PARAM},
        "reference": {
            "variant": "float64 expert (stable softmax, log_softmax)",
            "init_loss": reference["init_loss"], "final_loss": reference["final_loss"],
            "median_ms": reference["median_ms"], "step_ms": reference["step_ms"],
            "peak_bytes": reference["peak_bytes"], "command": reference["command"],
        },
        "variants": summary,
        "pairwise": pairwise,
    }
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")

    machine_info = result["machine"]
    print()
    print(f"model: batch {BATCH}, sequence {SEQUENCE}, d_model {D_MODEL}, heads {N_HEADS}, "
          f"ffn {D_FF}, vocab {VOCAB}, 2 blocks, {PARAMS} parameters, {STEPS} steps, "
          f"lr {LR}, threads {THREADS}")
    print(f"machine: {machine_info['host']} | {machine_info['cpu']} | {machine_info['cpus']} cpus"
          f" | torch {machine_info['torch']} | Python {machine_info['python']}")
    print(f"camel: {result['git']['commit'][:12]}"
          f"{' (dirty)' if result['git']['dirty'] else ''}"
          f" | {result['camel_version'] or 'no version line'}")
    build = result["build"]
    print(f"binary: built at {build['revision'][:12]}"
          f"{' (dirty)' if build.get('dirty') else ' (clean)'}"
          f" | {build.get('builtAt') or 'no build time'}"
          f" | fingerprint {(build.get('fingerprint') or 'unknown')[:12]}")
    print(f"rounds: {args.repeats} interleaved (rotating order), each a median of "
          f"{STEPS - WARMUP} steps after {WARMUP} warmup")
    print(f"load average: {load_start['1m']} → {load_end['1m']} over 1 min, "
          f"{load_start['15m']} → {load_end['15m']} over 15 min"
          f" ({machine_info['cpus']} hardware threads)")
    print()
    rows = []
    for name in VARIANTS:
        data = result["variants"][name]
        rows.append((
            name + (f" ({data['backend']})" if name == "camel" else ""),
            f"{data['median_ms']:.3f}",
            f"{data['spread']['min']:.3f}–{data['spread']['max']:.3f}",
            f"{data['peak_bytes'] / 2**20:.1f}",
            f"{data['final_loss']:.9f}",
            f"{data['loss_vs_reference']:.2e}",
            f"{data['vs_reference']['max_abs']:.2e}",
            "PASS" if (data["loss_vs_reference"] <= TOL_LOSS and
                       data["vs_reference"]["max_abs"] <= TOL_PARAM and
                       np.isfinite(data["final_loss"])) else "FAIL",
        ))
    rows.append((
        "float64 reference", f"{reference['median_ms']:.3f}", "one run",
        f"{reference['peak_bytes'] / 2**20:.1f}", f"{reference['final_loss']:.9f}",
        "0", "0", "—"))
    print(table(rows, ("implementation", f"ms/step (median of {args.repeats} rounds)",
                       f"spread ({args.repeats} rounds)", "peak RSS MiB", "final loss",
                       "|dloss| vs fp64", "max |dparam| vs fp64", "result")))
    print()
    print(f"result file: {output}")
    failures = [name for name in VARIANTS
                if not (np.isfinite(result["variants"][name]["final_loss"]) and
                        result["variants"][name]["loss_vs_reference"] <= TOL_LOSS and
                        result["variants"][name]["vs_reference"]["max_abs"] <= TOL_PARAM)]
    if failures:
        print(f"FAIL: {', '.join(failures)} exceed the tolerance")
        return 1
    print("PASS: all four agree with the float64 reference within the tolerance")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())