#!/usr/bin/env python3
# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""Compare the two collectors on allocation benchmarks.

    python3 test/tools/gcbench/gcbench.py [--rounds N] [--only NAME[,NAME...]] [--json FILE]

Every benchmark is one `camel` command. It is run `--rounds` times with each collector,
alternating between them (mark-sweep, generational, mark-sweep, ...) so that both see the same
drift of a machine that is busy with other work. The table gives, for each collector, the median
wall time of the runs with the fastest and the slowest beside it as a measure of the noise, the
median peak resident size (ru_maxrss of the child, the number `/usr/bin/time -v` prints as
"Maximum resident set size"), what the allocator reports with a `std::profile gc_stats=1`
pass (collections and time stopped: medians; longest stop: the longest of all runs) and the
one-minute load average of the machine before and after the benchmark.

The tensor benchmarks need the artifacts of benchmarks/ (`python3 benchmarks/run.py` generates
them) and are skipped without them.

The numbers are only comparable within one invocation on one machine.
"""

import argparse
import json
import os
import re
import statistics
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
CASES = os.path.join(ROOT, "test", "cases")
GC = os.path.join(CASES, "perf", "gc")
MM = os.path.join(CASES, "feat", "mm", "gc")
PARA = os.path.join(CASES, "para", "core")
BENCH = os.path.join(ROOT, "benchmarks")

ALLOC_SMALL = [os.path.join(GC, "alloc_small.cml"), "--arg", "reps=1000000"]
MAP_CHAIN = [os.path.join(GC, "map_chain.cml"), "--arg", "n=2000", "--arg", "reps=300"]
BUILD_TREE = [os.path.join(GC, "build_tree.cml"), "--arg", "depth=13", "--arg", "reps=40"]
SOAK = [os.path.join(MM, "gc_soak.cml"), "--arg", "rows=20000", "--arg", "reps=20000"]
FIB = os.path.join(GC, "fib.cml")
JIT = ["std::opt::inline", "std::jit"]


def train(name, reps):
    """The training workload of benchmarks/<name>/model.cml: `reps` timed steps after 2 warm-up steps."""
    return {
        "CAMEL_PACKAGES": os.path.join(BENCH, "common"),
        "CAMEL_BENCH_WEIGHTS": os.path.join(BENCH, "artifacts", name, "weights"),
        "CAMEL_BENCH_INPUT": os.path.join(BENCH, "artifacts", name, "input.npy"),
        "CAMEL_BENCH_WORKLOAD": "train",
        "CAMEL_BENCH_THREADS": "4",
        "CAMEL_BENCH_WARMUP": "2",
        "CAMEL_BENCH_REPS": str(reps),
    }


def model(name):
    return os.path.join(BENCH, name, "model.cml")


# name, arguments after `camel`, extra environment
BENCHMARKS = [
    ("alloc_small.nvm", ALLOC_SMALL + ["std::nvm"], {}),
    ("alloc_small.fvm", ALLOC_SMALL + ["std::fvm"], {}),
    ("alloc_small.jit", ALLOC_SMALL + JIT, {}),
    ("alloc_small.prl", ALLOC_SMALL + ["std::prl"], {}),
    ("map_chain.nvm", MAP_CHAIN + ["std::nvm"], {}),
    ("map_chain.fvm", MAP_CHAIN + ["std::fvm"], {}),
    ("map_chain.jit", MAP_CHAIN + JIT, {}),
    ("map_chain.prl", MAP_CHAIN + ["std::prl"], {}),
    ("build_tree.nvm", BUILD_TREE + ["std::nvm"], {}),
    ("build_tree.fvm", BUILD_TREE + ["std::fvm"], {}),
    ("build_tree.jit", BUILD_TREE + JIT, {}),
    ("build_tree.prl", BUILD_TREE + ["std::prl"], {}),
    ("soak.nvm", SOAK + ["std::nvm"], {}),
    ("soak.fvm", SOAK + ["std::fvm"], {}),
    ("soak.jit", SOAK + JIT, {}),
    ("soak.prl", SOAK + ["std::prl"], {}),
    ("live_table.fvm", [os.path.join(MM, "gc_live_table.cml"), "--arg", "rows=56000", "--arg", "reps=8", "std::fvm"], {}),
    ("live_large_256mb.fvm", [os.path.join(MM, "gc_live_large.cml"), "--arg", "mb=256", "--arg", "reps=40", "std::fvm"], {}),
    ("block_sort_merge.prl", [os.path.join(PARA, "block_sort_merge.cml"), "std::prl"], {}),
    ("concurrent_alloc.prl", [os.path.join(PARA, "concurrent_alloc.cml"), "std::prl"], {}),
    ("train_lenet.nvm", [model("lenet"), "std::macro", "std::nvm"], train("lenet", 150)),
    ("train_lenet.fvm", [model("lenet"), "std::macro", "std::fvm"], train("lenet", 150)),
    ("train_lenet.jit", [model("lenet"), "std::macro", "std::jit"], train("lenet", 150)),
    ("train_lenet.prl", [model("lenet"), "std::macro", "std::prl"], train("lenet", 150)),
    ("train_mlp.nvm", [model("mlp"), "std::macro", "std::nvm"], train("mlp", 300)),
    ("train_mlp.fvm", [model("mlp"), "std::macro", "std::fvm"], train("mlp", 300)),
    ("fib32.nvm", [FIB, "--arg", "n=32", "std::nvm"], {}),
    ("fib32.fvm", [FIB, "--arg", "n=32", "std::fvm"], {}),
    ("fib30.prl", [FIB, "--arg", "n=30", "std::prl"], {}),
    ("fib40.jit", [FIB, "--arg", "n=40"] + JIT, {}),
    ("jit_const36.fvm", [os.path.join(GC, "jit_const.cml"), "--arg", "n=36", "std::opt::inline", "std::fvm"], {}),
    ("jit_const36.jit", [os.path.join(GC, "jit_const.cml"), "--arg", "n=36"] + JIT, {}),
]

MODES = ["mark-sweep", "generational"]


def load_average():
    try:
        with open("/proc/loadavg", encoding="ascii") as source:
            return float(source.read().split()[0])
    except OSError:
        return float("nan")


def run_once(camel, args, mode, extra_env):
    """One run: wall time, exit status, peak resident size and the allocator's statistics line."""
    env = dict(os.environ)
    env.update(extra_env)
    for name in ("CAMEL_GC_ENABLE_YOUNG_COPYING", "CAMEL_GC_POISON"):
        env.pop(name, None)
    # The collector and the statistics line are the run's, not the environment's: --gc and a
    # profile pass before the back end. args[0] is the entry file.
    args = ["--gc", mode, args[0], "std::profile gc_stats=1 report=false", *args[1:]]
    with tempfile.TemporaryFile() as out, tempfile.TemporaryFile() as err:
        started = time.perf_counter()
        pid = os.fork()
        if pid == 0:
            os.dup2(out.fileno(), 1)
            os.dup2(err.fileno(), 2)
            os.chdir(ROOT)
            os.execve(camel, [camel] + args, env)
        _, status, usage = os.wait4(pid, 0)
        wall = time.perf_counter() - started
        out.seek(0)
        err.seek(0)
        stdout = out.read().decode("utf-8", "replace")
        stderr = err.read().decode("utf-8", "replace")
    stats = {}
    line = re.search(r"\[camel\] gc: (.*)", stderr)
    if line:
        for key, value in re.findall(r"(\w+)=([0-9.]+)", line.group(1)):
            stats[key] = float(value)
    # Timings that a program prints differ from run to run; its other lines must not.
    lines = [text for text in stdout.strip().splitlines() if "_ms=" not in text and " seconds" not in text]
    return {"wall": wall, "exit": status, "rss_kb": usage.ru_maxrss // (1024 if sys.platform == "darwin" else 1), "stats": stats, "stdout": lines}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--rounds", type=int, default=5)
    parser.add_argument("--only", default="")
    parser.add_argument("--json", default="")
    parser.add_argument("--camel", default=os.path.join(ROOT, "out", "latest", "bin", "camel"))
    options = parser.parse_args()
    only = [name for name in options.only.split(",") if name]
    results = []
    print(f"{'benchmark':<22} {'collector':<13} {'n':>2} {'median s':>9} {'min s':>8} {'max s':>8} {'rss MB':>8} "
          f"{'minor':>7} {'major':>6} {'stop ms':>9} {'max stop ms':>12} {'load':>11}")
    for name, args, extra_env in BENCHMARKS:
        if only and not any(name.startswith(item) for item in only):
            continue
        weights = extra_env.get("CAMEL_BENCH_WEIGHTS")
        if weights and not os.path.isdir(weights):
            print(f"{name:<22} skipped: no {os.path.relpath(weights, ROOT)}")
            continue
        runs = {mode: [] for mode in MODES}
        load_before = load_average()
        for _ in range(options.rounds):
            for mode in MODES:
                runs[mode].append(run_once(options.camel, args, mode, extra_env))
        load_after = load_average()
        failed = any(run["exit"] != 0 for mode in MODES for run in runs[mode])
        outputs = {mode: runs[mode][-1]["stdout"] for mode in MODES}
        for mode in MODES:
            mine = runs[mode]
            walls = [run["wall"] for run in mine]
            stop = [run["stats"].get("minor_pause_ms", 0) + run["stats"].get("major_pause_ms", 0) for run in mine]
            longest = [max(run["stats"].get("minor_pause_max_ms", 0), run["stats"].get("major_pause_max_ms", 0))
                       for run in mine]
            row = {
                "benchmark": name,
                "collector": mode,
                "rounds": options.rounds,
                "median_s": statistics.median(walls),
                "min_s": min(walls),
                "max_s": max(walls),
                "rss_mb": statistics.median(run["rss_kb"] for run in mine) / 1024.0,
                "minor": statistics.median(run["stats"].get("minor", 0) for run in mine),
                "major": statistics.median(run["stats"].get("major", 0) for run in mine),
                "promoted": statistics.median(run["stats"].get("promoted", 0) for run in mine),
                "stop_ms": statistics.median(stop),
                "max_stop_ms": max(longest),
                "load_before": load_before,
                "load_after": load_after,
                "failed": failed,
                "same_output": outputs[MODES[0]] == outputs[MODES[1]],
            }
            results.append(row)
            flag = "  FAILED" if failed else ("" if row["same_output"] else "  OUTPUT DIFFERS")
            print(f"{name:<22} {mode:<13} {options.rounds:>2} {row['median_s']:>9.3f} {row['min_s']:>8.3f} "
                  f"{row['max_s']:>8.3f} {row['rss_mb']:>8.1f} {int(row['minor']):>7} {int(row['major']):>6} "
                  f"{row['stop_ms']:>9.1f} {row['max_stop_ms']:>12.2f} {load_before:>5.1f}/{load_after:<5.1f}{flag}")
        sys.stdout.flush()
    if options.json:
        with open(options.json, "w", encoding="utf-8") as out:
            json.dump(results, out, indent=2)


if __name__ == "__main__":
    main()
