# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""Measure the four implementations of one Transformer training step.

The Camel program writes its starting weights and data as .npy files; every PyTorch variant
loads exactly those, so all four start from the same numbers.  The reference is the same
mathematics in float64.

One command, one machine-readable file:

  python examples/models/naive-in-expert-out/measure.py
  python examples/models/naive-in-expert-out/measure.py --output /tmp/results.json

The step time is the median of the timed steps, warmup excluded; it is measured inside each
program, around one forward, backward and Adam update.  Peak memory is the whole process's
maximum resident set, measured by /usr/bin/time, so it includes interpreter startup and the
allocator, not just the model.  The machine, the thread count and the versions are recorded in
the output because a time without them is not a measurement.
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

CANONICAL = [
    f"block{block}.{name}"
    for block in (0, 1)
    for name in ("norm1", "wq", "wk", "wv", "wo", "norm2", "w1", "b1", "w2", "b2")
] + ["norm_f", "head"]

VARIANTS = ("camel", "naive", "expert", "compiled")


def camel_env(camel: Path) -> dict[str, str]:
    # The install root is the parent of bin/, whether the binary was built in this worktree or
    # named from another checkout with --camel.
    install = camel.resolve().parents[1]
    env = os.environ.copy()
    env["CAMEL_HOME"] = str(install)
    env["PATH"] = str(install / "bin") + os.pathsep + env.get("PATH", "")
    env["CAMEL_NUM_THREADS"] = str(THREADS)
    for name in ("OMP_NUM_THREADS", "MKL_NUM_THREADS", "OPENBLAS_NUM_THREADS"):
        env[name] = str(THREADS)
    return env


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


def machine() -> dict:
    cpu = ""
    try:
        for line in Path("/proc/cpuinfo").read_text().splitlines():
            if line.startswith("model name"):
                cpu = line.split(":", 1)[1].strip()
                break
    except OSError:
        pass
    return {
        "host": platform.node(),
        "platform": platform.platform(),
        "cpu": cpu,
        "cpus": os.cpu_count(),
        "threads": THREADS,
        "python": sys.version.split()[0],
        "numpy": np.__version__,
        "torch": __import__("torch").__version__,
    }


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
    parser.add_argument("--work", default=None, help="scratch directory (default: a temp dir)")
    args = parser.parse_args()

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
    environment = camel_env(Path(args.camel))

    # 1. Camel writes the shared starting point and its final model, then trains.
    print(f"camel: {args.camel} train_step.cml std::{args.backend}")
    camel_run = run(
        [args.camel, str(HERE / "train_step.cml"), f"std::{args.backend}"],
        {**environment, "NAIVE_BENCH": "1",
         "NAIVE_INIT_DIR": str(init), "NAIVE_FINAL_DIR": str(finals / "camel")},
        stats=work / "camel.time",
    )
    if camel_run["exit"] != 0:
        sys.stderr.write(camel_run["stdout"] + camel_run["stderr"])
        raise SystemExit("the Camel program failed")
    camel = parse_camel(camel_run["stdout"])
    camel["peak_bytes"] = camel_run["peak_bytes"]
    camel["params"] = load_npy_leaves(finals / "camel")
    camel["backend"] = args.backend

    # 2. The three PyTorch counterparts and the float64 reference, each in its own process.
    runs = {}
    for variant in ("naive", "expert", "compiled", "reference"):
        print(f"torch: {variant}")
        command = [
            sys.executable, str(TORCH_IMPL), "--variant", variant,
            "--init", str(init), "--steps", str(STEPS), "--warmup", str(WARMUP),
            "--threads", str(THREADS), "--save-final", str(finals / f"{variant}.npz"),
        ]
        result = run(command, environment, stats=work / f"{variant}.time")
        if result["exit"] != 0:
            sys.stderr.write(result["stdout"] + result["stderr"])
            raise SystemExit(f"the {variant} counterpart failed")
        parsed = parse_torch(result["stdout"])
        parsed["peak_bytes"] = result["peak_bytes"]
        parsed["params"] = load_npz(finals / f"{variant}.npz")
        runs[variant] = parsed

    reference = runs.pop("reference")
    for variant in ("naive", "expert", "compiled"):
        runs[variant]["vs_reference"] = compare(runs[variant]["params"], reference["params"])
    camel["vs_reference"] = compare(camel["params"], reference["params"])
    runs["camel"] = camel

    # 3. Pairwise agreement among the four (max absolute parameter difference, and final loss).
    pairwise = {}
    for left in VARIANTS:
        for right in VARIANTS:
            if left >= right:
                continue
            pairwise[f"{left}:{right}"] = {
                "max_abs_param": compare(runs[left]["params"], runs[right]["params"])["max_abs"],
                "loss": abs(runs[left]["final_loss"] - runs[right]["final_loss"]),
            }

    # 4. The result file and the table.
    result = {
        "schema": 1,
        "generated_at": dt.datetime.now().astimezone().isoformat(timespec="seconds"),
        "machine": machine(),
        "camel_version": subprocess.run(
            [args.camel, "--version"], capture_output=True, text=True,
            env=environment).stdout.strip(),
        "model": {
            "batch": BATCH, "sequence": SEQUENCE, "d_model": D_MODEL, "heads": N_HEADS,
            "d_ff": D_FF, "vocab": VOCAB, "blocks": 2, "params": PARAMS,
            "steps": STEPS, "warmup": WARMUP, "lr": LR,
        },
        "tolerance": {"loss_abs": TOL_LOSS, "param_abs": TOL_PARAM},
        "reference": {
            "variant": "float64 expert (stable softmax, log_softmax)",
            "init_loss": reference["init_loss"], "final_loss": reference["final_loss"],
            "median_ms": reference["median_ms"],
        },
        "variants": {
            name: {
                "backend": "std::" + args.backend if name == "camel" else "torch",
                "init_loss": runs[name]["init_loss"],
                "final_loss": runs[name]["final_loss"],
                "median_ms": runs[name]["median_ms"],
                "mean_ms": runs[name]["mean_ms"],
                "peak_bytes": runs[name]["peak_bytes"],
                "step_ms": runs[name]["step_ms"],
                "losses": runs[name]["losses"],
                "vs_reference": runs[name]["vs_reference"],
                "loss_vs_reference": abs(runs[name]["final_loss"] - reference["final_loss"]),
            }
            for name in VARIANTS
        },
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
    print()
    rows = []
    for name in VARIANTS:
        data = result["variants"][name]
        rows.append((
            name + (f" ({data['backend']})" if name == "camel" else ""),
            f"{data['median_ms']:.3f}",
            f"{data['peak_bytes'] / 2**20:.1f}",
            f"{data['final_loss']:.9f}",
            f"{data['loss_vs_reference']:.2e}",
            f"{data['vs_reference']['max_abs']:.2e}",
            "PASS" if (data["loss_vs_reference"] <= TOL_LOSS and
                       data["vs_reference"]["max_abs"] <= TOL_PARAM and
                       np.isfinite(data["final_loss"])) else "FAIL",
        ))
    rows.append((
        "float64 reference", f"{reference['median_ms']:.3f}",
        f"{reference['peak_bytes'] / 2**20:.1f}", f"{reference['final_loss']:.9f}",
        "0", "0", "-"))
    print(table(rows, ("implementation", "ms/step (median)", "peak RSS MiB",
                       "final loss", "|dloss| vs fp64", "max |dparam| vs fp64", "result")))
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