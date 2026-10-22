#!/usr/bin/env python3
# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""Interleaved wall-clock and CPU-time measurement of Camel programs across back ends, thread
counts and builds, for machines that are not idle.

What it does for every (program, build, config):

  * runs the configurations in turn, one process each, round after round (A B C A B C ...), so
    that a load spike hits every configuration and not one of them;
  * pins the process to a CPU set with sched_setaffinity (`--cpus`), and with `--pin-per-thread`
    pins a configuration of N threads to the first N CPUs of the set;
  * records, per run: wall time, user and system CPU time, voluntary and involuntary context
    switches and peak resident memory (from the resource usage of the child), the largest
    number of threads the process had (polled from /proc), the 1-minute load average before the
    run, and a digest of the output;
  * can be run in several short invocations that add rounds to one result (`--json F --resume`);
  * refuses to compare runs whose output differs (lines that report a duration are left out of
    the digest): two configurations are only comparable when they computed the same thing.

It prints one table: minimum and median wall time, minimum CPU time, the ratio to the first
sequential configuration of the same build (`vs seq`), the speed-up over the parallel scheduler
with one thread (`t1/tN`), and, with two builds, the ratio of the first build to the second for
the same configuration (ratio of minima, and median of the per-round ratios).

A configuration is `<pass>[:<threads>[,<threads>...]]`: `std::fvm`, `std::prl:1,2,4,8`. Threads
are passed as `-t <n>`. A build is `<name>=<CAMEL_HOME>`.

Example (inside the build container):

  python3 benchmarks/parallel/prl_bench.py \
      --build new=$CAMEL_HOME --build old=/work/Camel-jit/out/latest \
      --config std::fvm --config std::prl:1,2,4,8 --cpus 0-7 --reps 7 \
      benchmarks/parallel/cases/mc_pi.cml

Linux only (affinity, /proc). Nothing here decides anything about the numbers: on a machine with
other load, read the minima and the per-round ratios, and the CPU time next to the wall time.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import statistics
import subprocess
import sys
import tempfile
import time
from dataclasses import asdict, dataclass, field
from typing import Dict, List, Optional, Tuple

DURATION_LINE = re.compile(r"(?i)second|duration|elapsed|\btook\b|\bms\b")


@dataclass
class Config:
    label: str
    passes: List[str]
    threads: Optional[int]
    parallel: bool


@dataclass
class Sample:
    wall: float
    user: float
    system: float
    vcsw: int
    icsw: int
    rss_mb: float
    threads: int
    load1: float
    exit: int
    digest: str


@dataclass
class Cell:
    program: str
    build: str
    config: Config
    samples: List[Sample] = field(default_factory=list)


def parse_cpus(spec: str) -> List[int]:
    cpus: List[int] = []
    for part in spec.split(","):
        part = part.strip()
        if not part:
            continue
        if "-" in part:
            lo, hi = part.split("-", 1)
            cpus.extend(range(int(lo), int(hi) + 1))
        else:
            cpus.append(int(part))
    return cpus


def parse_config(spec: str) -> List[Config]:
    # The thread list is a trailing ":1,2,4"; pass names contain "::" themselves. Several passes
    # are joined with "+" ("std::opt+std::prl:4").
    match = re.match(r"^(.*?)(?::(\d+(?:,\d+)*))?$", spec)
    assert match is not None
    passes_part, threads_part = match.group(1), match.group(2) or ""
    passes = passes_part.split("+")
    parallel = any(p in ("std::prl", "std::tf", "std::taskflow") for p in passes)
    if not threads_part:
        return [Config(passes_part, passes, None, parallel)]
    return [
        Config(f"{passes_part} -t {int(t)}", passes, int(t), parallel)
        for t in threads_part.split(",")
    ]


def load1() -> float:
    try:
        return os.getloadavg()[0]
    except OSError:
        return float("nan")


def thread_count(pid: int) -> int:
    try:
        with open(f"/proc/{pid}/status") as status:
            for line in status:
                if line.startswith("Threads:"):
                    return int(line.split()[1])
    except OSError:
        pass
    return 0


def digest_of(path: str) -> str:
    sha = hashlib.sha256()
    with open(path, "r", errors="replace") as out:
        for line in out:
            if DURATION_LINE.search(line):
                continue
            sha.update(line.encode())
    return sha.hexdigest()[:12]


def run_once(
    home: str, program: str, config: Config, cpus: Optional[List[int]], env_extra: Dict[str, str],
    timeout: float, keep_output: Optional[str],
) -> Sample:
    env = dict(os.environ)
    env["CAMEL_HOME"] = home
    env["PATH"] = os.path.join(home, "bin") + os.pathsep + env.get("PATH", "")
    env.update(env_extra)
    cmd = [os.path.join(home, "bin", "camel")]
    if config.threads is not None:
        cmd += ["-t", str(config.threads)]
    cmd += [program] + config.passes

    def pin() -> None:
        if cpus:
            os.sched_setaffinity(0, cpus)

    with tempfile.NamedTemporaryFile("w+", suffix=".out", delete=False) as out:
        out_path = out.name
    before = load1()
    peak_threads = 0
    with open(out_path, "w") as out:
        begin = time.perf_counter()
        child = subprocess.Popen(
            cmd, stdout=out, stderr=subprocess.STDOUT, env=env, preexec_fn=pin)
        deadline = begin + timeout
        status = None
        usage = None
        while True:
            pid, status, usage = os.wait4(child.pid, os.WNOHANG)
            if pid != 0:
                break
            peak_threads = max(peak_threads, thread_count(child.pid))
            if time.perf_counter() > deadline:
                child.kill()
                _, status, usage = os.wait4(child.pid, 0)
                break
            time.sleep(0.01)
        wall = time.perf_counter() - begin
        child.returncode = os.waitstatus_to_exitcode(status)
    digest = digest_of(out_path)
    if keep_output:
        os.replace(out_path, keep_output)
    else:
        os.unlink(out_path)
    return Sample(
        wall=wall,
        user=usage.ru_utime,
        system=usage.ru_stime,
        vcsw=usage.ru_nvcsw,
        icsw=usage.ru_nivcsw,
        rss_mb=usage.ru_maxrss / 1024.0,
        threads=peak_threads,
        load1=before,
        exit=child.returncode,
        digest=digest,
    )


def fmt(value: Optional[float], spec: str = "{:.3f}") -> str:
    return "-" if value is None else spec.format(value)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("programs", nargs="+")
    parser.add_argument(
        "--build", action="append", default=[], metavar="NAME=CAMEL_HOME[,VAR=VALUE...]",
        help="a build to run; VAR=VALUE pairs are set for this build only")
    parser.add_argument("--config", action="append", default=[], metavar="PASS[:T,T...]")
    parser.add_argument("--cpus", default="", help="CPU list to pin to, e.g. 0-7 or 0,2,4,6")
    parser.add_argument(
        "--pin-per-thread", action="store_true",
        help="pin a configuration with N threads to the first N CPUs of --cpus")
    parser.add_argument("--reps", type=int, default=7)
    parser.add_argument("--timeout", type=float, default=600.0, help="seconds per run")
    parser.add_argument("--env", action="append", default=[], metavar="NAME=VALUE")
    parser.add_argument("--json", default="", help="write every sample to this file")
    parser.add_argument(
        "--resume", action="store_true",
        help="start from the samples already in the --json file and add --reps rounds to them "
             "(a long measurement in several short invocations)")
    parser.add_argument(
        "--no-output-check", action="store_true",
        help="do not require the same output from every configuration of a program")
    parser.add_argument("--quiet", action="store_true", help="no progress lines on stderr")
    args = parser.parse_args()

    builds: List[Tuple[str, str]] = []
    # Environment of one build only: `name=HOME,VAR=VALUE,...` (the same home twice with a
    # switch set for one of them compares the switch within one table).
    build_envs: Dict[str, Dict[str, str]] = {}
    for spec in args.build or [f"cur={os.environ.get('CAMEL_HOME', '')}"]:
        name, _, rest = spec.partition("=")
        home, *assignments = rest.split(",")
        if not os.path.exists(os.path.join(home, "bin", "camel")):
            parser.error(f"no camel under {home!r} (build {name!r})")
        builds.append((name, home))
        build_envs[name] = dict(item.split("=", 1) for item in assignments)
    configs: List[Config] = []
    for spec in args.config or ["std::fvm", "std::prl:1,2,4,8"]:
        configs.extend(parse_config(spec))
    cpus = parse_cpus(args.cpus) if args.cpus else None
    env_extra = dict(item.split("=", 1) for item in args.env)

    cells: List[Cell] = [
        Cell(program, build, config)
        for program in args.programs for build, _ in builds for config in configs
    ]
    homes = dict(builds)
    done = 0
    if args.resume and args.json and os.path.exists(args.json):
        with open(args.json) as previous:
            for saved in json.load(previous)["cells"]:
                for cell in cells:
                    if (cell.program, cell.build, cell.config.label) == (
                            saved["program"], saved["build"], saved["config"]):
                        cell.samples = [Sample(**sample) for sample in saved["samples"]]
        done = min(len(cell.samples) for cell in cells)
    load_begin = load1()
    for rep in range(done, done + args.reps):
        # Rotate the order so that no configuration always follows the same one.
        order = cells[rep % len(cells):] + cells[:rep % len(cells)]
        for cell in order:
            pinned = cpus
            if cpus and args.pin_per_thread and cell.config.threads:
                pinned = cpus[: max(1, min(len(cpus), cell.config.threads))]
            sample = run_once(
                homes[cell.build], cell.program, cell.config, pinned,
                {**env_extra, **build_envs[cell.build]}, args.timeout, None)
            cell.samples.append(sample)
            if not args.quiet:
                print(
                    f"[{rep + 1}/{done + args.reps}] {os.path.basename(cell.program)} {cell.build} "
                    f"{cell.config.label}: wall={sample.wall:.3f}s cpu={sample.user + sample.system:.3f}s "
                    f"threads={sample.threads} load={sample.load1:.1f} exit={sample.exit}",
                    file=sys.stderr, flush=True)
    load_end = load1()

    # Same work everywhere: one digest per program.
    status = 0
    for program in args.programs:
        digests = {
            s.digest for c in cells if c.program == program for s in c.samples if s.exit == 0}
        if len(digests) > 1 and not args.no_output_check:
            print(f"OUTPUT DIFFERS for {program}:", file=sys.stderr)
            for c in cells:
                if c.program == program:
                    print(
                        f"  {c.build} {c.config.label}: {sorted({s.digest for s in c.samples})}",
                        file=sys.stderr)
            status = 2
        if any(s.exit != 0 for c in cells if c.program == program for s in c.samples):
            print(f"NONZERO EXIT in {program}", file=sys.stderr)
            status = status or 3

    def stat(cell: Cell) -> Optional[Tuple[float, float, float, float]]:
        ok = [s for s in cell.samples if s.exit == 0]
        if not ok:
            return None
        walls = [s.wall for s in ok]
        cpu = [s.user + s.system for s in ok]
        return min(walls), statistics.median(walls), min(cpu), statistics.median(cpu)

    def find(program: str, build: str, pred) -> Optional[Cell]:
        for c in cells:
            if c.program == program and c.build == build and pred(c.config):
                return c
        return None

    pin_text = "none" if not cpus else args.cpus + (" (first N per N threads)" if args.pin_per_thread else "")
    all_loads = [s.load1 for c in cells for s in c.samples]
    print(
        f"# cpus online={os.cpu_count()} pin={pin_text} reps={done + args.reps} "
        f"load1 begin={load_begin:.1f} end={load_end:.1f} "
        f"min={min(all_loads):.1f} max={max(all_loads):.1f}")
    header = (
        f"{'program':<22} {'build':<5} {'config':<16} {'n':>2} {'wall min':>9} {'wall med':>9} "
        f"{'cpu min':>8} {'thr':>3} {'vs seq':>7} {'t1/tN':>6} {'cpu/t1':>6} {'vcsw':>7} {'icsw':>6} {'rss MB':>7}")
    two = len(builds) == 2
    if two:
        header += f" {builds[0][0] + '/' + builds[1][0]:>9} {'paired':>7}"
    print(header)
    for cell in cells:
        st = stat(cell)
        seq = find(cell.program, cell.build, lambda c: not c.parallel)
        t1 = find(cell.program, cell.build, lambda c: c.parallel and c.threads == 1)
        seq_st = stat(seq) if seq else None
        t1_st = stat(t1) if t1 else None
        ok = [s for s in cell.samples if s.exit == 0]
        row = (
            f"{os.path.basename(cell.program):<22} {cell.build:<5} {cell.config.label:<16} "
            f"{len(ok):>2} {fmt(st[0] if st else None):>9} {fmt(st[1] if st else None):>9} "
            f"{fmt(st[2] if st else None):>8} "
            f"{(int(statistics.median(s.threads for s in ok)) if ok else 0):>3} "
            f"{fmt(st[0] / seq_st[0] if st and seq_st else None, '{:.2f}'):>7} "
            f"{fmt(t1_st[0] / st[0] if st and t1_st and cell.config.parallel else None, '{:.2f}'):>6} "
            f"{fmt(st[2] / t1_st[2] if st and t1_st and cell.config.parallel else None, '{:.2f}'):>6} "
            f"{(int(statistics.median(s.vcsw for s in ok)) if ok else 0):>7} "
            f"{(int(statistics.median(s.icsw for s in ok)) if ok else 0):>6} "
            f"{fmt(statistics.median(s.rss_mb for s in ok) if ok else None, '{:.0f}'):>7}")
        if two:
            other_build = builds[1][0] if cell.build == builds[0][0] else builds[0][0]
            other = find(cell.program, other_build, lambda c: c.label == cell.config.label)
            other_st = stat(other) if other else None
            if cell.build == builds[0][0] and st and other_st:
                paired = [
                    a.wall / b.wall for a, b in zip(cell.samples, other.samples)
                    if a.exit == 0 and b.exit == 0]
                row += f" {st[0] / other_st[0]:>9.2f} {statistics.median(paired):>7.2f}"
        print(row)

    if args.json:
        with open(args.json, "w") as out:
            json.dump(
                {
                    "cpus": cpus, "pin_per_thread": args.pin_per_thread, "reps": done + args.reps,
                    "builds": builds, "load1": [load_begin, load_end],
                    "cells": [
                        {
                            "program": c.program, "build": c.build, "config": c.config.label,
                            "samples": [asdict(s) for s in c.samples],
                        } for c in cells
                    ],
                }, out, indent=1)
    return status


if __name__ == "__main__":
    sys.exit(main())
