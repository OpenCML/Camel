#!/usr/bin/env python3
# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""Repeat the `--timeit` command of the H9/2 crash report until it fails, and count how often.

The command

    camel --timeit std::nvm/5 test/cases/perf/gc/alloc_small.cml --arg reps=200000 \
          --output-format json

was seen to end in an access violation (`0xC0000005`, exit code 3221225477) twice in about thirty
`--timeit` calls of the H8 performance baseline (handover note ⟨89307a8⟩ (H8), section 6). The same
command passed when it was repeated, so the script runs it in fresh processes and records every
exit code: a run that ends in the violation, and a run that ends with any other nonzero code, are
both failures here, and the stdout and stderr of each are kept.

Every run is a `--timeit` process, which rebuilds the module, the graph and the Context for every
iteration and destroys them again (tools/camel-cli/timeit.cpp); `--iterations` sets how many of
those rebuild-and-destroy cycles one process makes. The failure is reported per run, and the
number of iterations that were run is printed too, so a rate can be given for either.

    python3 test/tools/timeitloop/timeitloop.py                    # until the first failure
    python3 test/tools/timeitloop/timeitloop.py --runs 500 --count # the rate over 500 processes
    python3 test/tools/timeitloop/timeitloop.py --runs 100 --gc both --jobs 8

The command is run the way `test/tools/perfbaseline/baseline.py` runs it: with stdout and stderr
as pipes, from the repository root, with the binary at `out/latest/bin/camel`. Nothing about the
process or its environment is special; `--gc` passes `--gc` to the child, as `gcbench.py` does,
and with `--gc both` the two collectors alternate from one process to the next (mark-sweep,
generational, ...) so that both see the same drift of a machine that is busy with other work.

The `perf.gc.timeit_cycles` tests of `test/plans/perf/gc.plan.toml` run the same loop inside the
test plans, once per collector; this script is for the longer hunt (thousands of processes) and
for the frequency that is written down in the report.
"""

import argparse
import os
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
CAMEL = os.path.join(ROOT, "out", "latest", "bin", "camel" + (".exe" if sys.platform == "win32" else ""))
COLLECTORS = ("mark-sweep", "generational")


def describe(code):
    """The exit code in both spellings: 3221225477 (0xC0000005)."""
    if code is None:
        return "none"
    return f"{code} (0x{code & 0xFFFFFFFF:08X})"


def run_once(index, binary, args, env):
    started = time.perf_counter()
    proc = subprocess.Popen(
        [binary] + args, cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
    out, err = proc.communicate()
    return index, proc.returncode, time.perf_counter() - started, out or b"", err or b""


def write_failure(out_dir, index, code, out, err, command):
    base = os.path.join(out_dir, f"FAILED-{index}-0x{code & 0xFFFFFFFF:08X}")
    with open(base + ".stdout.txt", "wb") as file:
        file.write(out)
    with open(base + ".stderr.txt", "wb") as file:
        file.write(err)
    with open(os.path.join(out_dir, "failures.log"), "a", encoding="utf-8") as file:
        file.write(f"run={index} exit={describe(code)} command={command}\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--runs", type=int, default=100000, help="processes to run (default: until a failure)")
    parser.add_argument("--iterations", type=int, default=5, help="contexts rebuilt and destroyed per process")
    parser.add_argument("--pass", dest="passes", default="std::nvm",
                        help="the timed pass, without the /n (default: std::nvm; --iterations is the n)")
    parser.add_argument("--case", default=os.path.join("test", "cases", "perf", "gc", "alloc_small.cml"))
    parser.add_argument("--arg", dest="arg", default="reps=200000", help="an --arg of the program")
    parser.add_argument("--gc", default="", choices=("", "mark-sweep", "generational", "both"),
                        help="the collector of the child (--gc); both alternates the collectors")
    parser.add_argument("--camel", default="",
                        help="the binary to run (default: out/latest/bin/camel), for a build with assertions or symbols")
    parser.add_argument("--jobs", type=int, default=1, help="processes in parallel")
    parser.add_argument("--count", action="store_true", help="keep going after a failure (report the rate)")
    parser.add_argument("--out-dir", default=os.path.join("tmp", "timeitloop"))
    options = parser.parse_args()

    if not os.path.exists(CAMEL):
        raise SystemExit(f"no camel binary at {CAMEL}; run npm run build first")
    binary = os.path.abspath(options.camel) if options.camel else CAMEL
    if not os.path.exists(binary):
        raise SystemExit(f"no camel binary at {binary}")

    out_dir = os.path.join(ROOT, options.out_dir)
    os.makedirs(out_dir, exist_ok=True)
    args = [
        "--timeit",
        f"{options.passes}/{options.iterations}",
        options.case,
        "--arg",
        options.arg,
        "--output-format",
        "json",
    ]
    command = os.path.basename(binary) + " " + " ".join(args)
    collectors = COLLECTORS if options.gc == "both" else ((options.gc,) if options.gc else ("",))
    print(f"# {command}", flush=True)
    print(f"# jobs={options.jobs} runs={options.runs} collectors={','.join(c or 'default' for c in collectors)}",
          flush=True)

    def collector_of(index):
        return collectors[(index - 1) % len(collectors)]

    failures = []
    completed = 0
    started = time.perf_counter()
    try:
        with ThreadPoolExecutor(max_workers=options.jobs) as pool:
            futures = []
            next_index = 1
            while completed < options.runs:
                while len(futures) < options.jobs and next_index <= options.runs:
                    chosen = collector_of(next_index)
                    run_args = (["--gc", chosen] if chosen else []) + args
                    futures.append(pool.submit(run_once, next_index, binary, run_args,
                                               dict(os.environ)))
                    next_index += 1
                index, code, seconds, out, err = futures.pop(0).result()
                completed += 1
                if code != 0:
                    failures.append((index, code))
                    stamp = time.strftime("%H:%M:%S")
                    print(f"[{stamp}] run {index} FAILED exit={describe(code)} after {seconds:.1f}s", flush=True)
                    print(f"  command: {command}", flush=True)
                    write_failure(out_dir, index, code, out, err, command)
                    text = err.decode("utf-8", "replace")
                    if text:
                        print(text, flush=True)
                    if not options.count:
                        break
                if completed % 10 == 0:
                    elapsed = time.perf_counter() - started
                    print(
                        f"[{time.strftime('%H:%M:%S')}] {completed}/{options.runs} runs, "
                        f"failures={len(failures)}, {elapsed:.1f}s, {elapsed / completed:.2f}s/run",
                        flush=True,
                    )
                if not options.count and failures:
                    break
    except KeyboardInterrupt:
        print("# interrupted", flush=True)

    elapsed = time.perf_counter() - started
    rate = len(failures) / completed if completed else 0.0
    print(
        f"# done: {completed} runs ({completed * options.iterations} iterations), "
        f"{len(failures)} failures, {rate:.3%} per run, "
        f"{elapsed:.1f}s total, {elapsed / max(1, completed):.2f}s/run",
        flush=True,
    )
    if failures:
        codes = ", ".join(describe(code) for _, code in failures[:8])
        print(f"# exit codes of the first failures: {codes}", flush=True)
        print(f"# the stdout and stderr of each failure are in {out_dir}", flush=True)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())