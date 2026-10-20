#!/usr/bin/env python3
"""Run the Camel and PyTorch implementations of C2 and compare them.

The script is the single source of every number in README/RESULTS.md:

  * it prepares the data (make_data.py) and removes the previous checkpoints;
  * it runs the Camel program and the PyTorch baseline on the same data and the same
    starting weights, and measures the training time, the throughput and the peak memory
    of each process (psutil), plus the compile time of the Camel program (a
    `std::macro std::stats std::null` pipeline, median of three);
  * it compares the two runs against the tolerances below and exits non-zero when one of
    them is not met;
  * it counts the code and writes the tables to RESULTS.md.

Profiles:

  small     synthetic 1000/200, 200 steps, batch 50 -- the `slow` test tier
  standard  MNIST when available (10000/2000), 1000 steps, batch 100 -- the reported run
  full      60000/10000, 3000 steps, batch 128

Examples:

  python compare.py --profile small
  python compare.py --profile standard --mnist D:/Projects/Camel/.cache/mnist/mnist.npz
  python compare.py --profile standard --source synthetic --steps 300
"""

from __future__ import annotations

import argparse
import datetime as dt
import importlib.metadata
import json
import os
import platform
import re
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path

import psutil

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
TRAIN_CML = HERE / "train.cml"
BASELINE = HERE / "baseline" / "train_torch.py"
MAKE_DATA = HERE / "make_data.py"

# Tolerances, justified in RESULTS.md and in the README (design principles section 7):
# only the canonical algorithm on one back end is promised bit for bit; these two programs
# compute the same f32 formulas but sum in a different order (BLAS/tensor kernels) and Adam
# compounds the difference over every step, so the comparison is a bounded-error one.
TOL_INIT_LOSS = 1.0e-5      # relative; the same weights and data, one forward pass
TOL_STEP_LOSS = 1.0e-2      # relative; every logged training loss
TOL_VAL_LOSS = 1.0e-2       # relative; the final validation loss
TOL_VAL_ACC = 1.0e-2        # absolute; the final validation accuracy (one percentage point)

PROFILES = {
    "small": dict(train=1000, val=200, steps=200, batch=50, log_every=20, source="synthetic"),
    "standard": dict(train=10000, val=2000, steps=1000, batch=100, log_every=100, source="auto"),
    "full": dict(train=60000, val=10000, steps=3000, batch=128, log_every=100, source="auto"),
}

SECTION = re.compile(r"^\s*(?://|#)\s*==\s*([a-z0-9-]+)\s*==\s*$")


# ----------------------------------------------------------------- data and commands


def python_executable() -> str:
    return sys.executable


def camel_executable(explicit: str | None) -> Path:
    if explicit:
        return Path(explicit)
    name = "camel.exe" if os.name == "nt" else "camel"
    return REPO / "out" / "latest" / "bin" / name


def tree_peak_bytes(pid: int) -> int:
    """Peak working set of a process and its descendants.

    A virtual-environment `python.exe` on Windows is a launcher that runs the real
    interpreter as a child, so measuring only the started pid would report the launcher's
    few MiB.  `peak_wset` is the lifetime peak of each process; summing the peaks of a small
    tree can over-report by the shared runtime, which is noted in RESULTS.md.
    """
    try:
        root = psutil.Process(pid)
        processes = [root] + root.children(recursive=True)
    except (psutil.NoSuchProcess, psutil.AccessDenied):
        return 0
    total = 0
    for process in processes:
        try:
            info = process.memory_info()
            total += getattr(info, "peak_wset", 0) or info.rss
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue
    return total


def run_command(command: list[str], env: dict[str, str], timeout: float | None = None):
    """Run a command, sampling the peak resident set of the child and its children."""
    started = time.perf_counter()
    process = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
        env=env,
    )
    peak = 0
    while process.poll() is None:
        peak = max(peak, tree_peak_bytes(process.pid))
        time.sleep(0.02)
    stdout, stderr = process.communicate(timeout=timeout)
    return {
        "exit": process.returncode,
        "stdout": stdout,
        "stderr": stderr,
        "seconds": time.perf_counter() - started,
        "peak_bytes": peak,
    }


def prepare_data(args, data: Path) -> dict:
    command = [
        python_executable(),
        str(MAKE_DATA),
        "--out",
        str(data),
        "--train",
        str(args.train),
        "--val",
        str(args.val),
        "--seed",
        str(args.seed),
        "--source",
        args.source,
    ]
    if args.mnist:
        command += ["--mnist", args.mnist]
    result = run_command(command, os.environ.copy())
    if result["exit"] != 0:
        sys.stderr.write(result["stdout"] + result["stderr"])
        raise SystemExit("make_data.py failed")
    manifest = json.loads((data / "manifest.json").read_text(encoding="utf-8"))
    sys.stdout.write(result["stdout"])
    return manifest


def camel_arg_command(camel: Path, data: Path, args, passes: list[str]) -> list[str]:
    command = [
        str(camel),
        str(TRAIN_CML),
        "--arg", f"data={data}",
        "--arg", f"steps={args.steps}",
        "--arg", f"batch={args.batch}",
        "--arg", f"lr={args.lr}",
        "--arg", f"log_every={args.log_every}",
        "--arg", f"threads={args.threads}",
    ]
    return command + passes


def camel_env(threads: int) -> dict[str, str]:
    env = os.environ.copy()
    home = REPO / "out" / "latest"
    env["CAMEL_HOME"] = str(home)
    env["PATH"] = str(home / "bin") + os.pathsep + env.get("PATH", "")
    env["CAMEL_NUM_THREADS"] = str(threads)
    return env


def python_env(threads: int) -> dict[str, str]:
    env = os.environ.copy()
    env["OMP_NUM_THREADS"] = str(threads)
    env["MKL_NUM_THREADS"] = str(threads)
    return env


# ----------------------------------------------------------------- parsing and comparison


def parse_camel_output(text: str) -> dict:
    parsed = {"config": None, "init": None, "steps": [], "trained": None, "checkpoint": None}
    for line in text.splitlines():
        line = line.strip()
        match = re.match(r"^c2 config steps=(\d+) batch=(\d+) lr=(\S+) threads=(\d+) params=(\d+) batches=(\d+)$", line)
        if match:
            parsed["config"] = dict(
                steps=int(match.group(1)),
                batch=int(match.group(2)),
                lr=float(match.group(3)),
                threads=int(match.group(4)),
                params=int(match.group(5)),
                batches=int(match.group(6)),
            )
            continue
        match = re.match(r"^c2 init val_loss=(\S+) val_accuracy=(\S+)$", line)
        if match:
            parsed["init"] = (float(match.group(1)), float(match.group(2)))
            continue
        match = re.match(r"^c2 step=(\d+) loss=(\S+)$", line)
        if match:
            parsed["steps"].append((int(match.group(1)), float(match.group(2))))
            continue
        match = re.match(
            r"^c2 trained steps=(\d+) seconds=(\S+) val_loss=(\S+) val_accuracy=(\S+)$", line
        )
        if match:
            parsed["trained"] = dict(
                steps=int(match.group(1)),
                seconds=float(match.group(2)),
                val_loss=float(match.group(3)),
                val_accuracy=float(match.group(4)),
            )
            continue
        match = re.match(
            r"^c2 checkpoint val_loss=(\S+) val_accuracy=(\S+) l1_drift=(\S+)$", line
        )
        if match:
            parsed["checkpoint"] = dict(
                val_loss=float(match.group(1)),
                val_accuracy=float(match.group(2)),
                l1_drift=float(match.group(3)),
            )
    return parsed


def relative(a: float, b: float) -> float:
    return abs(a - b) / max(abs(a), abs(b), 1.0e-12)


def compare_runs(camel: dict, torch: dict) -> tuple[list[dict], bool]:
    rows = []
    ok = True

    def add(name: str, left: float, right: float, difference: float, tolerance: float, passed: bool):
        nonlocal ok
        ok = ok and passed
        rows.append(
            dict(
                metric=name,
                camel=left,
                torch=right,
                difference=difference,
                tolerance=tolerance,
                passed=passed,
            )
        )

    for label, parsed, source in (("Camel", camel, "camel"), ("PyTorch", torch, "torch")):
        for part in ("config", "init", "steps", "trained", "checkpoint"):
            if parsed[part] in (None, []):
                raise SystemExit(f"{label} output has no '{part}' line:\n{json.dumps(parsed, indent=2)}")

    if camel["config"] != torch["config"]:
        raise SystemExit(f"the runs used different configurations: {camel['config']} != {torch['config']}")

    add(
        "initial validation loss",
        camel["init"][0],
        torch["init"][0],
        relative(camel["init"][0], torch["init"][0]),
        TOL_INIT_LOSS,
        relative(camel["init"][0], torch["init"][0]) <= TOL_INIT_LOSS,
    )

    camel_steps = dict(camel["steps"])
    torch_steps = dict(torch["steps"])
    if sorted(camel_steps) != sorted(torch_steps):
        raise SystemExit(f"different logged steps: {sorted(camel_steps)} != {sorted(torch_steps)}")
    worst_step, worst_value = 0, 0.0
    for step in sorted(camel_steps):
        value = relative(camel_steps[step], torch_steps[step])
        if value > worst_value:
            worst_step, worst_value = step, value
    add(
        f"training loss over the logged steps (worst: step {worst_step})",
        camel_steps[worst_step],
        torch_steps[worst_step],
        worst_value,
        TOL_STEP_LOSS,
        worst_value <= TOL_STEP_LOSS,
    )

    add(
        "final validation loss",
        camel["trained"]["val_loss"],
        torch["trained"]["val_loss"],
        relative(camel["trained"]["val_loss"], torch["trained"]["val_loss"]),
        TOL_VAL_LOSS,
        relative(camel["trained"]["val_loss"], torch["trained"]["val_loss"]) <= TOL_VAL_LOSS,
    )

    accuracy_difference = abs(camel["trained"]["val_accuracy"] - torch["trained"]["val_accuracy"])
    add(
        "final validation accuracy",
        camel["trained"]["val_accuracy"],
        torch["trained"]["val_accuracy"],
        accuracy_difference,
        TOL_VAL_ACC,
        accuracy_difference <= TOL_VAL_ACC,
    )

    add(
        "checkpoint round-trip drift (L1, each side alone)",
        camel["checkpoint"]["l1_drift"],
        torch["checkpoint"]["l1_drift"],
        0.0,
        0.0,
        camel["checkpoint"]["l1_drift"] == 0.0 and torch["checkpoint"]["l1_drift"] == 0.0,
    )
    return rows, ok


# ----------------------------------------------------------------- code size


def is_code_line(line: str, language: str) -> bool:
    """Whether one line is code (for the size table), outside docstrings and block comments.

    A documented heuristic, not a parser: full-line comments are dropped, Python docstrings
    and C-style block comments are tracked across lines, and a code line with a trailing
    comment counts once.
    """
    stripped = line.strip()
    if not stripped or stripped.startswith("#") or stripped.startswith("//"):
        return False
    return True


def section_counts(path: Path, language: str) -> dict[str, int]:
    """Code lines (see is_code_line) per `== name ==` section, docstrings/blocks excluded."""
    counts: dict[str, int] = {}
    current = "header"
    in_block = False
    in_docstring = None
    for line in path.read_text(encoding="utf-8").splitlines():
        match = SECTION.match(line)
        if match:
            current = match.group(1)
            continue
        counts.setdefault(current, 0)
        stripped = line.strip()
        if in_docstring is not None:
            if in_docstring in stripped:
                in_docstring = None
            continue
        if in_block:
            if "*/" in stripped:
                in_block = False
            continue
        if language == "python" and (stripped.startswith('"""') or stripped.startswith("'''")):
            marker = stripped[:3]
            if stripped.count(marker) == 1:
                in_docstring = marker
            continue
        if stripped.startswith("/*"):
            if "*/" not in stripped:
                in_block = True
            continue
        if is_code_line(line, language):
            counts[current] += 1
    return counts


def code_size() -> list[dict]:
    camel = section_counts(TRAIN_CML, "camel")
    torch = section_counts(BASELINE, "python")
    order = ["header", "imports", "model", "loss", "data", "train", "evaluate", "checkpoint", "entry"]
    names = {name: name for name in order}
    names["header"] = "header (module/docstring)"
    rows = []
    for name in order:
        rows.append(
            dict(section=names[name], camel=camel.get(name, 0), torch=torch.get(name, 0))
        )
    rows.append(dict(section="total", camel=sum(camel.values()), torch=sum(torch.values())))
    return rows


# ----------------------------------------------------------------- report


def markdown_table(headers: list[str], rows: list[list[str]]) -> str:
    lines = ["| " + " | ".join(headers) + " |", "|" + "|".join("---" for _ in headers) + "|"]
    for row in rows:
        lines.append("| " + " | ".join(row) + " |")
    return "\n".join(lines)


def fmt(value: float, digits: int = 9) -> str:
    return f"{value:.{digits}f}"


def build_report(args, manifest, camel_run, torch_run, compile_times, compile_stats, agreement, size, results_path):
    camel = parse_camel_output(camel_run["stdout"])
    torch = parse_camel_output(torch_run["stdout"])

    try:
        torch_version = importlib.metadata.version("torch")
    except importlib.metadata.PackageNotFoundError:
        torch_version = "unknown"
    try:
        numpy_version = importlib.metadata.version("numpy")
    except importlib.metadata.PackageNotFoundError:
        numpy_version = "unknown"
    camel_version = args.camel_version

    agreement_table = markdown_table(
        ["metric", "Camel", "PyTorch", "difference", "tolerance", "result"],
        [
            [
                row["metric"],
                fmt(row["camel"]),
                fmt(row["torch"]),
                f"{row['difference']:.3e}",
                f"{row['tolerance']:.0e}" if row["tolerance"] else "exact",
                "PASS" if row["passed"] else "FAIL",
            ]
            for row in agreement
        ],
    )

    camel_throughput = camel["trained"]["steps"] * args.batch / camel["trained"]["seconds"]
    torch_throughput = torch["trained"]["steps"] * args.batch / torch["trained"]["seconds"]
    performance_table = markdown_table(
        ["metric", "Camel", "PyTorch", "Camel / PyTorch"],
        [
            ["training time (s)", fmt(camel["trained"]["seconds"], 6), fmt(torch["trained"]["seconds"], 6),
             fmt(camel["trained"]["seconds"] / torch["trained"]["seconds"], 3)],
            ["throughput (samples/s)", fmt(camel_throughput, 1), fmt(torch_throughput, 1),
             fmt(camel_throughput / torch_throughput, 3)],
            ["peak process memory (MiB)", fmt(camel_run["peak_bytes"] / 2**20, 1),
             fmt(torch_run["peak_bytes"] / 2**20, 1),
             fmt(camel_run["peak_bytes"] / torch_run["peak_bytes"], 3)],
            ["compile time (s)", fmt(statistics.median(compile_times), 3), "n/a (eager)", "n/a"],
            ["whole process wall time (s)", fmt(camel_run["seconds"], 3), fmt(torch_run["seconds"], 3),
             fmt(camel_run["seconds"] / torch_run["seconds"], 3)],
        ],
    )

    size_table = markdown_table(
        ["section", "Camel (SLOC)", "PyTorch (SLOC)"],
        [[row["section"], str(row["camel"]), str(row["torch"])] for row in size],
    )

    machine = f"{platform.system()} {platform.release()} ({platform.machine()})"
    processor = platform.processor() or "unknown"
    stamp = dt.datetime.now().astimezone().isoformat(timespec="seconds")
    command_line = " ".join([Path(sys.executable).name, *sys.argv])
    stats_line = ""
    if compile_stats:
        stats_line = f"\nCamel graph after `std::macro` (from `std::stats`): `{json.dumps(compile_stats)}`\n"

    report = f"""# C2 — MLP on MNIST: measured results

Generated by `showcase/c2-mlp-mnist/compare.py`; do not edit by hand.

- date: {stamp}
- machine: {machine}, {processor}
- Camel: {camel_version}
- PyTorch: {torch_version}, Python {platform.python_version()}, NumPy {numpy_version}
- command: `{command_line}`
- data: `{manifest['source']}`, train={manifest['train']}, val={manifest['val']}, hidden={manifest['hidden']}, seed={manifest['seed']}, id=`{manifest['dataset_id']}`
- profile: {args.profile}; {args.steps} steps, batch {args.batch}, lr {args.lr}, threads {args.threads}

## Agreement

{agreement_table}

Tolerance: the two programs compute the same f32 formulas, but not in the same order (the
matrix kernels and Adam's bias correction differ), and small differences compound over the
training run. Design principles section 7 promises bit-for-bit equality only for the
canonical algorithm on the same back end; across implementations the comparison is a
bounded-error one. The bounds are {TOL_INIT_LOSS:.0e} (relative) for the initial loss,
{TOL_STEP_LOSS:.0e} for every logged training loss, {TOL_VAL_LOSS:.0e} for the final
validation loss and {TOL_VAL_ACC:.0e} (absolute) for the final validation accuracy.
`compare.py` exits non-zero when one of them is not met. The checkpoint line is not a
comparison between the two: each implementation must load back exactly what it saved.

## Performance

{performance_table}

Training time is measured inside each program around the training loop only (data loading,
evaluation and checkpointing excluded, so the two numbers measure the same work);
throughput is `steps * batch / time`. Peak memory is the peak working set of the whole
process and its children, sampled with psutil, so it includes the runtime and the loaded
weights, and the PyTorch number includes the Python interpreter (the virtual-environment
`python.exe` is a launcher on Windows, which is why the tree is sampled). The compile time
is the median of three runs of `camel train.cml ... std::macro std::stats std::null` (parse,
type check, macro expansion and graph building, no execution); PyTorch is eager and has no
separate compile step. The whole-process time includes startup and the loading of the
input files, so for PyTorch it is dominated by importing torch.

{stats_line}
## Code size

{size_table}

SLOC is a documented line-counting heuristic: blank lines, full-line comments, Python
docstrings and C-style block comments are not counted (a code line with a trailing comment
counts once). Sections are the `// == name ==` / `# == name ==` markers the two files share;
a section that appears more than once (data and evaluate, in the entry function) is summed.
"""

    results_path.write_text(report, encoding="utf-8")
    return report


# ----------------------------------------------------------------- main


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--profile", choices=tuple(PROFILES), default="standard")
    parser.add_argument("--data", default=None, help="data directory (default: artifacts/<profile>)")
    parser.add_argument("--camel", default=None, help="path to camel.exe")
    parser.add_argument("--steps", type=int, default=None)
    parser.add_argument("--batch", type=int, default=None)
    parser.add_argument("--lr", type=float, default=1.0e-3)
    parser.add_argument("--log-every", type=int, default=None)
    parser.add_argument("--threads", type=int, default=1)
    parser.add_argument("--seed", type=int, default=20261020)
    parser.add_argument("--source", choices=("auto", "mnist", "synthetic"), default=None)
    parser.add_argument("--mnist", default=os.environ.get("C2_MNIST"), help="path to mnist.npz")
    parser.add_argument("--train", type=int, default=None)
    parser.add_argument("--val", type=int, default=None)
    parser.add_argument("--reuse-data", action="store_true", help="keep an existing data directory")
    parser.add_argument("--results", default=None, help="where to write RESULTS.md")
    parser.add_argument("--skip-compile-time", action="store_true")
    args = parser.parse_args()

    profile = PROFILES[args.profile]
    for field in ("train", "val", "steps", "batch", "log_every", "source"):
        if getattr(args, field) is None:
            setattr(args, field, profile[field])
    if args.train < 1 or args.val < 1:
        parser.error("--train and --val must be positive")
    if args.steps < 1 or args.batch < 1 or args.log_every < 1 or args.threads < 1:
        parser.error("--steps, --batch, --log-every and --threads must be positive")

    camel = camel_executable(args.camel)
    if not camel.is_file():
        raise SystemExit(
            f"camel not found at {camel}; build the worktree first (see the repository's build command)"
        )
    data = Path(args.data) if args.data else HERE / "artifacts" / args.profile
    results_path = Path(args.results) if args.results else HERE / "RESULTS.md"

    sys.stdout.write(
        f"c2 compare: profile={args.profile} train={args.train} val={args.val} "
        f"steps={args.steps} batch={args.batch} lr={args.lr} threads={args.threads}\n"
    )

    if args.reuse_data and (data / "manifest.json").is_file():
        manifest = json.loads((data / "manifest.json").read_text(encoding="utf-8"))
        sys.stdout.write(f"c2 compare: reusing {data}\n")
    else:
        manifest = prepare_data(args, data)

    # A previous checkpoint is left by the previous run; `save_tree` refuses to replace a
    # directory, so the harness removes it (see the README's "遇到的问题").
    shutil.rmtree(data / "checkpoint", ignore_errors=True)
    (data / "checkpoint-torch.pt").unlink(missing_ok=True)

    if args.skip_compile_time:
        compile_times = [float("nan")]
        compile_stats = None
    else:
        compile_times = []
        compile_stats = None
        for _ in range(3):
            compile_run = run_command(
                camel_arg_command(camel, data, args, ["std::macro", "std::stats", "std::null"]),
                camel_env(args.threads),
            )
            if compile_run["exit"] != 0:
                sys.stderr.write(compile_run["stdout"] + compile_run["stderr"])
                raise SystemExit("the Camel compile-time run failed")
            compile_times.append(compile_run["seconds"])
            for line in compile_run["stdout"].splitlines():
                if line.startswith("GRAPH_STATS "):
                    try:
                        compile_stats = json.loads(line[len("GRAPH_STATS "):])
                    except json.JSONDecodeError:
                        compile_stats = None
        sys.stdout.write(
            f"c2 compare: Camel compile time {', '.join(f'{t:.3f}' for t in compile_times)} s\n"
        )

    camel_run = run_command(
        camel_arg_command(camel, data, args, ["std::macro", "std::nvm"]), camel_env(args.threads)
    )
    if camel_run["exit"] != 0:
        sys.stderr.write(camel_run["stdout"] + camel_run["stderr"])
        raise SystemExit("the Camel run failed")
    sys.stdout.write(camel_run["stdout"])

    torch_command = [
        python_executable(),
        str(BASELINE),
        "--data", str(data),
        "--steps", str(args.steps),
        "--batch", str(args.batch),
        "--lr", repr(args.lr),
        "--log-every", str(args.log_every),
        "--threads", str(args.threads),
    ]
    torch_run = run_command(torch_command, python_env(args.threads))
    if torch_run["exit"] != 0:
        sys.stderr.write(torch_run["stdout"] + torch_run["stderr"])
        raise SystemExit("the PyTorch run failed")
    sys.stdout.write(torch_run["stdout"])

    # The Camel version is printed in the report; get it from the same binary that ran.
    version_run = run_command([str(camel), "--version"], camel_env(args.threads))
    version_line = version_run["stdout"].strip().splitlines()
    args.camel_version = version_line[0] if version_line else "unknown"

    camel_parsed = parse_camel_output(camel_run["stdout"])
    torch_parsed = parse_camel_output(torch_run["stdout"])
    agreement, ok = compare_runs(camel_parsed, torch_parsed)
    size = code_size()

    report = build_report(
        args, manifest, camel_run, torch_run, compile_times, compile_stats, agreement, size, results_path
    )
    sys.stdout.write("\n" + report)
    sys.stdout.write(f"\nc2 compare: results written to {results_path}\n")
    if not ok:
        sys.stdout.write("c2 compare: FAILED\n")
        return 1
    sys.stdout.write("c2 compare: PASS\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())