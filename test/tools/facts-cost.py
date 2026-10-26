#!/usr/bin/env python3
# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""Measure a Camel pipeline's wall time and its own CPU time, interleaved between cases.

The cost of invalidating graph facts by the declaration epoch is a difference between two
pipelines that differ only in *when* a module is loaded. Wall time alone cannot resolve that on a
shared host, so this tool:

  * runs the cases in an interleaved, randomly ordered round per repetition, so the machine's load
    drift hits every case the same way;
  * reads each child's CPU time (user + system) from ``getrusage(RUSAGE_CHILDREN)``, which on a
    loaded host is the "how much work was this" number;
  * prints the median, p10, p90, min and max of both, and the per-round paired difference against
    a baseline case;
  * records the 1-minute load average before and after the whole run.

It is a measuring instrument, not a test: nothing here has a threshold, and it is not part of any
plan. Use it on the Linux host; on Windows ``getrusage(RUSAGE_CHILDREN)`` is not available.

Example (Linux host, from the worktree root, with ``CAMEL_HOME`` set to the install to measure):
the three pipelines of ``test/cases/facts/tensors_and_loops.cml`` that differ only in when the rules
library is loaded -- ``control`` loads it before the seed, so the declaration epoch does not move
under the measured pass; ``bump`` lets ``std::opt`` load it; ``fold`` loads nothing:

    python3 test/tools/facts-cost.py --reps 21 \
      --case "bump=test/cases/facts/tensors_and_loops.cml test::facts_seed std::opt test::facts_check" \
      --case "control=test/cases/facts/tensors_and_loops.cml test::facts_load_rules test::facts_seed std::opt test::facts_check" \
      --case "fold=test/cases/facts/tensors_and_loops.cml test::facts_seed std::opt::fold test::facts_check" \
      --baseline control --outdir /tmp/facts-cost

The camel binary is ``$CAMEL_HOME/bin/camel`` unless ``--camel`` says otherwise; the working
directory is ``--root`` (default: the current directory), which is where the program paths in a
case resolve -- run from the worktree root, or name the root with ``--root``. A case's arguments are
split like a shell would split them. ``--reps`` and ``--warmup`` decide how long the measurement
takes; on a shared host the CPU figure this tool reads is the number to look at, not the wall time.
"""

import argparse
import json
import os
import random
import re
import resource
import shlex
import statistics
import subprocess
import sys
import time

# The lines `test::facts_check` prints that say what the pass itself did. They are read
# from each run's stdout as extra per-run numbers beside the whole process's CPU.
EXTRACTED = {
    "recompute_us": re.compile(r"FACTS recompute us=([0-9.]+)"),
    "stale": re.compile(r"FACTS counts .*?stale=(\d+)"),
    "recomputed": re.compile(r"FACTS recomputed (\d+)"),
}


def child_cpu():
    usage = resource.getrusage(resource.RUSAGE_CHILDREN)
    return usage.ru_utime + usage.ru_stime


def load_average():
    try:
        return os.getloadavg()[0]
    except OSError:
        return None


def percentiles(values, p):
    ordered = sorted(values)
    if not ordered:
        return None
    k = max(0, min(len(ordered) - 1, int(round((len(ordered) - 1) * p / 100.0))))
    return ordered[k]


def describe(values):
    return {
        "n": len(values),
        "median": statistics.median(values),
        "p10": percentiles(values, 10),
        "p90": percentiles(values, 90),
        "min": min(values),
        "max": max(values),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--camel", default=None, help="camel binary (default $CAMEL_HOME/bin/camel)")
    parser.add_argument("--root", default=".", help="working directory for the runs")
    parser.add_argument("--reps", type=int, default=21, help="timed repetitions per case")
    parser.add_argument("--warmup", type=int, default=3, help="untimed runs per case first")
    parser.add_argument(
        "--case",
        action="append",
        required=True,
        metavar="NAME=ARGS",
        help="one case; NAME=ARGS with ARGS split as a shell would (repeatable)",
    )
    parser.add_argument("--baseline", help="case the paired differences are against")
    parser.add_argument("--outdir", help="write each case's last stdout here as NAME.stdout")
    parser.add_argument("--out", help="write the measurements as JSON here")
    parser.add_argument("--seed", type=int, default=158, help="shuffle seed for the round order")
    args = parser.parse_args()

    camel = args.camel or os.path.join(os.environ.get("CAMEL_HOME", ""), "bin", "camel")
    if not os.path.isfile(camel):
        sys.exit(f"camel binary not found: {camel}")
    root = os.path.abspath(args.root)

    cases = []
    names = set()
    for item in args.case:
        name, sep, line = item.partition("=")
        if not sep or not name or not line.strip():
            sys.exit(f"--case wants NAME=ARGS, got {item!r}")
        if name in names:
            sys.exit(f"duplicate case name {name!r}")
        names.add(name)
        cases.append({"name": name, "argv": [camel] + shlex.split(line)})
    if args.baseline and args.baseline not in names:
        sys.exit(f"--baseline names no case: {args.baseline}")

    if args.outdir:
        os.makedirs(args.outdir, exist_ok=True)

    run = {
        "camel": camel,
        "cwd": root,
        "load_before": load_average(),
        "reps": args.reps,
        "warmup": args.warmup,
        "cases": {case["name"]: case["argv"][1:] for case in cases},
    }

    def invoke(case):
        before = child_cpu()
        started = time.perf_counter()
        process = subprocess.run(
            case["argv"],
            cwd=root,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        wall = time.perf_counter() - started
        cpu = child_cpu() - before
        if process.returncode != 0:
            sys.stderr.write(process.stdout)
            sys.exit(f"case {case['name']!r} exited {process.returncode}")
        return wall, cpu, process.stdout

    for case in cases:
        for _ in range(args.warmup):
            invoke(case)

    samples = {case["name"]: {"wall": [], "cpu": [], "facts": {key: [] for key in EXTRACTED}}
               for case in cases}
    order = random.Random(args.seed)
    for _ in range(args.reps):
        round_cases = list(cases)
        order.shuffle(round_cases)
        for case in round_cases:
            wall, cpu, stdout = invoke(case)
            samples[case["name"]]["wall"].append(wall * 1000.0)
            samples[case["name"]]["cpu"].append(cpu * 1000.0)
            for key, pattern in EXTRACTED.items():
                found = pattern.search(stdout)
                if found:
                    samples[case["name"]]["facts"][key].append(float(found.group(1)))
            if args.outdir:
                with open(os.path.join(args.outdir, case["name"] + ".stdout"), "w") as out:
                    out.write(stdout)

    run["load_after"] = load_average()
    run["summary"] = {name: {kind: describe(values) for kind, values in data.items()
                             if kind != "facts"}
                      for name, data in samples.items()}
    run["facts"] = {
        name: {key: describe(values) for key, values in data["facts"].items() if values}
        for name, data in samples.items()
    }
    run["samples"] = samples

    if args.baseline:
        base = args.baseline
        run["paired"] = {}
        for name in samples:
            if name == base:
                continue
            differences = [
                samples[name]["cpu"][i] - samples[base]["cpu"][i] for i in range(args.reps)
            ]
            walls = [samples[name]["wall"][i] - samples[base]["wall"][i] for i in range(args.reps)]
            run["paired"][name] = {
                "cpu": describe(differences),
                "wall": describe(walls),
            }

    columns = ["case", "cpu median", "cpu p10", "cpu p90", "wall median", "wall p10", "wall p90"]
    print("{:<12} {:>11} {:>10} {:>10} {:>12} {:>11} {:>11}".format(*columns))
    for case in cases:
        summary = run["summary"][case["name"]]
        cpu, wall = summary["cpu"], summary["wall"]
        print(
            "{:<12} {:>11.3f} {:>10.3f} {:>10.3f} {:>12.3f} {:>11.3f} {:>11.3f}".format(
                case["name"], cpu["median"], cpu["p10"], cpu["p90"],
                wall["median"], wall["p10"], wall["p90"],
            )
        )
    if args.baseline:
        for name, paired in run["paired"].items():
            print(
                f"paired {name} - {args.baseline}: cpu {paired['cpu']['median']:+.3f} ms "
                f"[{paired['cpu']['p10']:+.3f}, {paired['cpu']['p90']:+.3f}], "
                f"wall {paired['wall']['median']:+.3f} ms"
            )
    if any(run["facts"][case["name"]] for case in cases):
        columns = ["case", "recompute us", "recompute p10", "recompute p90", "stale", "recomputed"]
        print("{:<12} {:>13} {:>13} {:>13} {:>6} {:>11}".format(*columns))
        for case in cases:
            facts = run["facts"][case["name"]]
            recompute = facts.get("recompute_us", {})
            print(
                "{:<12} {:>13.1f} {:>13.1f} {:>13.1f} {:>6.0f} {:>11.0f}".format(
                    case["name"],
                    recompute.get("median", float("nan")),
                    recompute.get("p10", float("nan")),
                    recompute.get("p90", float("nan")),
                    facts.get("stale", {}).get("median", float("nan")),
                    facts.get("recomputed", {}).get("median", float("nan")),
                )
            )
    print(
        f"load 1 min: {run['load_before']} -> {run['load_after']}; "
        f"reps {args.reps}, warmup {args.warmup}"
    )

    if args.out:
        with open(args.out, "w") as out:
            json.dump(run, out, indent=2)
            out.write("\n")


if __name__ == "__main__":
    main()