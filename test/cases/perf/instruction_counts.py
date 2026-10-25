"""Instruction counts per step for one training step, at d=64 and a wider control cell.

Wall time and CPU time on the shared reference host both carry the neighbours: worker threads spin,
the clock moves, caches are evicted.  Retired user-space instructions do not: the same program on
the same input executes the same instruction stream whatever else the host is doing, so a build that
does more work per step is visible here even when the host is busy, and a build that does not is not
accused by a slow afternoon.

The workload is the committed one.  `train_step.cml` is read from the example directory and its
extents are substituted exactly as `measure.py: scaled_source` does (the substitution table is
copied from it and every occurrence count is checked), with `let steps` and `let warmup` set to the
run length; the program's own `naive config` line is checked against the cell before a count is
accepted, and the generated text's SHA-256 is recorded so two builds can be shown to have run the
same text.

`/usr/bin/perf stat -e instructions:u` is the counter the ruling names.  On this host
`kernel.perf_event_paranoid` is 4, so an unprivileged `perf` is refused; the counter therefore runs
inside a privileged container that chroots into the host root (`--privileged -v /:/host`), where
perf holds CAP_PERFMON and measures the same host process without changing any host-wide setting.

The step's instruction count is the slope of the retired count against the step count over two run
lengths, so start-up, the JIT's compilation and the one-off parameter save cancel; the longer the
pair, the smaller the share of anything that does not scale with the step.

Modes

  measure  Count the step at the named sizes and run lengths, repeated, and print the counts, the
           per-step slopes, the ratio between repetitions and the load average each run saw.
  probe    Check the counter (a known program), the build stamp and the load, and print them.

Usage

  python test/cases/perf/instruction_counts.py probe
  python test/cases/perf/instruction_counts.py measure --sizes 64,256 --lengths 64,256,512 \
      --repeats 3 --json ${test_tmp}/instruction-counts.json --work ${test_tmp}/work
  python test/cases/perf/instruction_counts.py measure --sizes 64 --lengths 64,512 \
      --repeats 1 --induce-load 32 --label loaded
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent          # test/cases/perf
REPO = HERE.parents[2]
EXAMPLE = REPO / "examples/models/naive-in-expert-out"
TRAIN_STEP = EXAMPLE / "train_step.cml"

# measure.py's cell rule and its substitution table, copied so this case does not need numpy.
BASE_D_MODEL, BASE_BATCH, BASE_SEQUENCE = 64, 8, 16
BASE_VOCAB, D_HEAD = 32, 16
WARMUP = 3
THREADS = 1
BLOCK_LIMIT_MB = 8192

# The privileged perf: `perf stat` needs CAP_PERFMON, and perf_event_paranoid is 4 here.
PERF_IMAGE = os.environ.get("CAMEL_INSTR_IMAGE", "alpine:latest")
PERF = "/usr/bin/perf"

PERF_LINE = re.compile(r"^([0-9.]+)\s*,[^,]*,\s*([A-Za-z0-9:_-]+)\s*,")
CONFIG_LINE = re.compile(
    r"naive config batch=(\d+) sequence=(\d+) d_model=(\d+) heads=(\d+) d_ff=(\d+) "
    r"vocab=(\d+) blocks=(\d+) params=(\d+)")


def load_average() -> dict:
    try:
        fields = Path("/proc/loadavg").read_text().split()
        return {"1m": float(fields[0]), "5m": float(fields[1]), "15m": float(fields[2]),
                "running": int(fields[3].split("/")[0]), "threads": int(fields[3].split("/")[1])}
    except (OSError, ValueError, IndexError):
        return {"1m": None, "5m": None, "15m": None, "running": None, "threads": None}


def camel_binary() -> Path:
    home = os.environ.get("CAMEL_HOME")
    candidate = Path(home) / "bin" / "camel" if home else REPO / "out" / "latest" / "bin" / "camel"
    if not candidate.exists():
        raise SystemExit(f"no Camel binary at {candidate} (set CAMEL_HOME or build first)")
    return candidate


def build_stamp(camel: Path) -> dict:
    """The install's own stamp: the revision and fingerprint the binary was built from."""
    path = camel.resolve().parents[1] / "build-info.json"
    try:
        info = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {"revision": "unknown", "fingerprint": "unknown", "note": f"no readable {path}"}
    fingerprint = info.get("fingerprint") or {}
    return {"revision": info.get("revision"), "dirty": info.get("dirty"),
            "dirtyCount": info.get("dirtyCount"), "dirtyPaths": info.get("dirtyPaths"),
            "builtAt": info.get("builtAt"), "config": info.get("config"),
            "fingerprint": fingerprint.get("hash"), "fingerprintFiles": fingerprint.get("files")}


def git_state() -> dict:
    def git(*args: str) -> str:
        try:
            return subprocess.run(["git", "-C", str(REPO), *args], capture_output=True, text=True,
                                  check=True).stdout.strip()
        except (OSError, subprocess.CalledProcessError):
            return ""
    return {"commit": git("rev-parse", "HEAD") or "unknown",
            "branch": git("rev-parse", "--abbrev-ref", "HEAD") or "unknown",
            "dirty": bool(git("status", "--porcelain"))}


def shape_for(d_model: int) -> dict:
    """measure.py's cell rule: d_model is 64 times a square, and the data grows with the width."""
    ratio = d_model / BASE_D_MODEL
    scale = int(round(math.sqrt(ratio)))
    if scale < 1 or BASE_D_MODEL * scale * scale != d_model:
        raise SystemExit(f"d_model {d_model} is not {BASE_D_MODEL} times a square "
                         "(64, 256, 576, 1024, ...)")
    return {"d_model": d_model, "batch": BASE_BATCH * scale, "sequence": BASE_SEQUENCE * scale,
            "tokens": BASE_BATCH * scale * BASE_SEQUENCE * scale, "heads": d_model // D_HEAD,
            "d_head": D_HEAD, "d_ff": 2 * d_model, "vocab": BASE_VOCAB,
            "params": 16 * d_model * d_model + 11 * d_model + d_model * BASE_VOCAB, "blocks": 2}


def scaled_source(shape: dict, steps: int, warmup: int) -> tuple[str, list[list[str]]]:
    """train_step.cml with exactly its extents substituted; every occurrence count is checked."""
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


def sha256_text(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def camel_command(camel: Path, source: Path, init: Path) -> list[str]:
    """measure.py's own Camel command: one thread, the same block limit, the canonical path.

    The recorded sweep ran `std::jit` with no `tensor::fuse` in front of it, and the recorded build
    has no such pass; that is the configuration both builds are measured in.
    """
    return [str(camel), "-t", str(THREADS), "--block-limit", str(BLOCK_LIMIT_MB), str(source),
            "std::jit", "--arg", "bench=true", "--arg", f"init_dir={init}", "--arg", "final_dir="]


def perf_wrapper(target: list[str], env: dict[str, str]) -> list[str]:
    pairs = []
    for name in ("CAMEL_HOME", "LD_LIBRARY_PATH", "PATH", "HOME"):
        if env.get(name):
            pairs += ["-e", f"{name}={env[name]}"]
    return ["docker", "run", "--rm", "--privileged", "-v", "/:/host", *pairs, PERF_IMAGE,
            "chroot", "/host", PERF, "stat", "-x", ",", "-e", "instructions:u,task-clock", "--",
            *target]


def parse_counters(stderr: str) -> dict:
    """instructions:u (retired user instructions) and task-clock (CPU milliseconds)."""
    counters: dict[str, float] = {}
    for line in stderr.splitlines():
        match = PERF_LINE.match(line.strip())
        if match:
            counters[match.group(2)] = float(match.group(1))
    return counters


def parse_config(stdout: str) -> dict | None:
    match = CONFIG_LINE.search(stdout)
    if not match:
        return None
    keys = ("batch", "sequence", "d_model", "heads", "d_ff", "vocab", "blocks", "params")
    return {key: int(value) for key, value in zip(keys, match.groups())}


def run_counted(target: list[str], env: dict[str, str]) -> dict:
    before = load_average()
    started = time.perf_counter()
    result = subprocess.run(perf_wrapper(target, env), text=True, encoding="utf-8",
                            errors="replace", stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            timeout=3600)
    wall = time.perf_counter() - started
    after = load_average()
    counters = parse_counters(result.stderr)
    instructions = counters.get("instructions:u")
    return {"exit": result.returncode,
            "instructions": int(instructions) if instructions is not None else None,
            "task_clock_ms": counters.get("task-clock"), "config": parse_config(result.stdout),
            "wall_seconds": wall, "load_before": before, "load_after": after,
            "stdout_tail": result.stdout[-200:] if result.returncode else "",
            "stderr_tail": result.stderr[-400:] if result.returncode else ""}


def median(values: list[float]) -> float | None:
    ordered = sorted(values)
    return ordered[len(ordered) // 2] if ordered else None


def measure_size(shape: dict, lengths: list[int], repeats: int, camel: Path, env: dict,
                 work: Path) -> dict:
    d_model = shape["d_model"]
    cell = work / f"d{d_model}"
    init = cell / "init"
    if cell.exists():
        shutil.rmtree(cell)
    init.mkdir(parents=True)
    programs: dict[int, Path] = {}
    sources: dict[str, dict] = {}
    for steps in sorted(set(lengths)):
        text, applied = scaled_source(shape, steps, WARMUP)
        path = cell / f"train_step_s{steps}.cml"
        path.write_text(text, encoding="utf-8")
        programs[steps] = path
        sources[str(steps)] = {"sha256": sha256_text(text), "substitutions": len(applied),
                               "steps": steps, "warmup": WARMUP}
    runs: dict[int, list[dict]] = {steps: [] for steps in lengths}
    for round_index in range(repeats):
        order = list(lengths) if round_index % 2 == 0 else list(reversed(lengths))
        for steps in order:
            print(f"  d={d_model} steps={steps} round {round_index + 1}/{repeats}", flush=True)
            row = run_counted(camel_command(camel, programs[steps], init), env)
            row["steps"] = steps
            row["round"] = round_index
            if row["exit"] != 0 or row["instructions"] is None:
                raise SystemExit(f"the count failed for d={d_model} steps={steps}: {row}")
            expected = {key: shape[key] for key in
                        ("batch", "sequence", "d_model", "heads", "d_ff", "vocab", "blocks",
                         "params")}
            if row["config"] != expected:
                raise SystemExit(f"the generated program ran {row['config']}, the cell is {expected}")
            runs[steps].append(row)
    short, long = min(lengths), max(lengths)

    def slope(key: str) -> list[float]:
        return [(runs[long][index][key] - runs[short][index][key]) / (long - short)
                for index in range(repeats)]

    slopes = slope("instructions")
    cpu_slopes = slope("task_clock_ms")
    intermediate = None
    if len(lengths) > 2:
        mid = sorted(lengths)[len(lengths) // 2]
        intermediate = [(runs[long][index]["instructions"] - runs[mid][index]["instructions"]) /
                        (long - mid) for index in range(repeats)]
    return {
        "d_model": d_model, "shape": shape, "threads": THREADS, "warmup": WARMUP,
        "lengths": lengths, "short": short, "long": long, "repeats": repeats,
        "sources": sources, "runs": {str(steps): rows for steps, rows in runs.items()},
        "slope_instructions_per_step": {"median": median(slopes), "samples": slopes},
        "slope_cpu_ms_per_step": {"median": median(cpu_slopes), "samples": cpu_slopes},
        "slope_from_middle": {"length": sorted(lengths)[len(lengths) // 2] if intermediate else None,
                              "median": median(intermediate) if intermediate else None,
                              "samples": intermediate},
        "load_1m": [row["load_before"]["1m"] for steps in lengths for row in runs[steps]],
    }


def probe() -> int:
    camel = camel_binary()
    env = os.environ.copy()
    env["CAMEL_HOME"] = str(camel.resolve().parents[1])
    target = ["/bin/echo", "hi"]
    result = subprocess.run(perf_wrapper(target, env), text=True, encoding="utf-8",
                            errors="replace", stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            timeout=300)
    counters = parse_counters(result.stderr)
    print(f"perf wrapper exit {result.returncode}: {json.dumps(counters)} for /bin/echo")
    print(f"camel: {camel}")
    print(f"build: {json.dumps(build_stamp(camel))}")
    print(f"git:   {json.dumps(git_state())}")
    print(f"load:  {json.dumps(load_average())}")
    ok = bool(counters.get("instructions:u"))
    print(f"perf probe: {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("measure", "probe"))
    parser.add_argument("--sizes", default="64,256")
    parser.add_argument("--lengths", default="64,256,512")
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--control-repeats", type=int, default=None,
                        help="repeats for every size after the first (default: --repeats)")
    parser.add_argument("--induce-load", type=int, default=0,
                        help="start this many busy spinners on the host for the measurement")
    parser.add_argument("--load-seconds", type=int, default=180)
    parser.add_argument("--load-settle", type=int, default=5,
                        help="seconds to let the spinners show in the 1-minute load average "
                             "before the first run")
    parser.add_argument("--label", default="")
    parser.add_argument("--json", default="")
    parser.add_argument("--work", default=os.environ.get("CAMEL_TEST_TMPDIR",
                                                          "test/tmp/instruction-counts"))
    args = parser.parse_args()

    if args.mode == "probe":
        return probe()

    camel = camel_binary()
    env = os.environ.copy()
    env["CAMEL_HOME"] = str(camel.resolve().parents[1])
    env["PATH"] = str(camel.resolve().parents[1] / "bin") + os.pathsep + env.get("PATH", "")
    sizes = [int(part) for part in args.sizes.split(",") if part.strip()]
    lengths = [int(part) for part in args.lengths.split(",") if part.strip()]
    if len(lengths) < 2:
        raise SystemExit("--lengths needs at least the two ends of the slope")
    # The runner hands a scratch directory relative to the repository; the measured process runs
    # under `chroot /host` with the container's own working directory, so every path it is given
    # (the generated program, the parameter directory) has to be absolute.
    work = Path(args.work).resolve()
    work.mkdir(parents=True, exist_ok=True)

    load_container = None
    if args.induce_load:
        name = f"camel-instr-load-{os.getpid()}"
        subprocess.run(["docker", "run", "-d", "--rm", "--name", name, PERF_IMAGE, "sh", "-c",
                        f"i=0; while [ $i -lt {args.induce_load} ]; do yes > /dev/null & "
                        f"i=$((i+1)); done; sleep {args.load_seconds}"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
        load_container = name
        time.sleep(args.load_settle)
        print(f"induced load: {args.induce_load} spinners in {name}; load now {load_average()}",
              flush=True)

    result = {
        "schema": 1,
        "what": "the naive step's retired user-space instructions per step, by the slope over two "
                "run lengths, on the committed workload",
        "generated_at": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        "label": args.label,
        "case_sha256": sha256_text(Path(__file__).read_text(encoding="utf-8")),
        "camel": str(camel),
        "build": build_stamp(camel),
        "git": git_state(),
        "protocol": {"counter": "perf stat -e instructions:u,task-clock, in a privileged container "
                                "that chroots into the host root (perf_event_paranoid is 4)",
                     "threads": THREADS, "warmup": WARMUP, "lengths": lengths,
                     "sizes": sizes, "repeats": args.repeats,
                     "control_repeats": args.control_repeats,
                     "induce_load": args.induce_load,
                     "workload": "train_step.cml with measure.py's substitution table; the "
                                 "program's own naive config line is checked against the cell"},
        "load_at_start": load_average(),
        "cells": [],
    }
    try:
        for index, d_model in enumerate(sizes):
            repeats = args.control_repeats if (index and args.control_repeats) else args.repeats
            print(f"cell d={d_model} t={THREADS} lengths={lengths} repeats={repeats}", flush=True)
            result["cells"].append(measure_size(shape_for(d_model), lengths, repeats, camel, env,
                                                work))
    finally:
        if load_container:
            subprocess.run(["docker", "rm", "-f", load_container], stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL, check=False)
            print(f"induced load stopped; load now {load_average()}", flush=True)

    result["load_at_end"] = load_average()

    print()
    print("| d | steps | instructions | task-clock ms | wall s | load 1m before | load 1m after |")
    print("|---|---|---|---|---|---|---|")
    for cell in result["cells"]:
        for steps in cell["lengths"]:
            for row in cell["runs"][str(steps)]:
                print(f"| {cell['d_model']} | {steps} | {row['instructions']} | "
                      f"{row['task_clock_ms']} | {row['wall_seconds']:.2f} | "
                      f"{row['load_before']['1m']} | {row['load_after']['1m']} |")
    print()
    print("| d | slope (instructions/step) | samples | CPU ms/step (task-clock) | "
          "slope from the middle |")
    print("|---|---|---|---|---|")
    for cell in result["cells"]:
        print(f"| {cell['d_model']} | {cell['slope_instructions_per_step']['median']:.1f} | "
              f"{[round(value, 1) for value in cell['slope_instructions_per_step']['samples']]} | "
              f"{cell['slope_cpu_ms_per_step']['median']:.4f} | "
              f"{cell['slope_from_middle']['samples']} |")

    ok = bool(result["cells"]) and all(
        cell["slope_instructions_per_step"]["median"] and
        cell["slope_instructions_per_step"]["median"] > 0 for cell in result["cells"])
    result["pass"] = ok

    if args.json:
        Path(args.json).write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print("\n=== instruction counts json begin ===")
    print(json.dumps(result, indent=1))
    print("=== instruction counts json end ===")
    print(f"\ninstruction counts: {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())