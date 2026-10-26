#!/usr/bin/env python3
# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""Interleaved A/B comparison of two builds of camel (docs/performance/graph-store-baseline.md).

A time measured in one build says little about another build: the layout of the code alone moves
an execution by several percent. So the two builds are
run alternately on the same program, and every pair is bracketed by a second run of A:

    A1  B  A2  A1  B  A2 ...

|A1 - A2| is the noise floor of this machine for this program; B differs from A only where
the median difference exceeds it.

    python3 test/tools/perfbaseline/ab.py --a OUT_DIR_A --b OUT_DIR_B [--pairs N]
                                          [--only NAME[,NAME...]] [--what compile,opt,optinline]

OUT_DIR is a build's out/latest directory (bin/camel inside, CAMEL_HOME). Each measurement is
the wall time of one `camel` process (the process includes the compile of the program), and
for `--what` modes that time a pass the pass's own time from `--timeit` (which compiles again
for every iteration and times only the pass):

  compile    camel PROGRAM std::stats std::null          (wall time)
  optinline  --timeit std::opt::inline/1 PROGRAM         (the pass's time)
  opt        --timeit std::opt/1 PROGRAM                 (the pass's time)
  nvm, fvm, jit, prl    --timeit std::<back end>/N PROGRAM   (the run, without the compile)

Peak memory of the process is taken from the same runs (the maximum over the runs). The time of
`compile` is the CPU time of the process (user + system) where the host reports it, which other
processes on the machine disturb less than the wall time; the pass modes time the pass inside
the process, over `--rounds` compilations.
"""

import argparse
import os
import statistics
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import baseline  # noqa: E402
import scale_programs  # noqa: E402

def timed(pass_name, rounds):
    return lambda args: (["--timeit", f"{pass_name}/{rounds}"] + args + ["--output-format", "json"], True)


def mode_table(rounds):
    return {
        "compile": lambda args: (args + ["std::stats", "std::null"], False),
        "optinline": timed("std::opt::inline", rounds),
        "opt": timed("std::opt", rounds),
        "nvm": timed("std::nvm", rounds),
        "fvm": timed("std::fvm", rounds),
        "jit": timed("std::jit", rounds),
        "prl": timed("std::prl", rounds),
    }


def measure(home, argv, timeit):
    baseline.CAMEL = os.path.join(home, "bin", "camel" + (".exe" if sys.platform == "win32" else ""))
    result = baseline.run(argv, {"CAMEL_HOME": home})
    if timeit:
        value = baseline.timeit_mean_ms(result["stdout"])
    else:
        value = result["cpu_ms"] if result["cpu_ms"] is not None else result["wall_ms"]
    return value, result["peak"]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--a", required=True)
    parser.add_argument("--b", required=True)
    parser.add_argument("--pairs", type=int, default=5)
    parser.add_argument("--rounds", type=int, default=5, help="compilations averaged by a pass run")
    parser.add_argument("--only", default="")
    parser.add_argument("--what", default="compile,optinline,opt")
    options = parser.parse_args()
    only = [n for n in options.only.split(",") if n]
    modes = [m for m in options.what.split(",") if m]

    scale_dir = tempfile.TemporaryDirectory(prefix="camel_scale_")
    programs = [(name, args) for name, args, _, required in baseline.PROGRAMS if not required]
    for name, units in scale_programs.SIZES.items():
        programs.append((name, [scale_programs.write(units, scale_dir.name)]))

    small = {name for name, _ in programs if not name.startswith("scale.") or name == "scale.1k"}
    print("| program | mode | A (ms) | B (ms) | B/A | noise (A1 vs A2) | peak A / B (MB) |")
    print("|---|---|---|---|---|---|---|")
    for name, args in programs:
        if only and name not in only:
            continue
        for mode in modes:
            # The big programs take seconds to compile and to rewrite: one round is plenty.
            rounds = options.rounds if name in small else 1
            argv, timeit = mode_table(rounds)[mode](args)
            a1s, bs, a2s = [], [], []
            peak_a = peak_b = 0
            for _ in range(options.pairs):
                value, peak = measure(options.a, argv, timeit)
                a1s.append(value)
                peak_a = max(peak_a, peak or 0)
                value, peak = measure(options.b, argv, timeit)
                bs.append(value)
                peak_b = max(peak_b, peak or 0)
                value, peak = measure(options.a, argv, timeit)
                a2s.append(value)
                peak_a = max(peak_a, peak or 0)
            a = statistics.median(a1s + a2s)
            b = statistics.median(bs)
            noise = statistics.median(abs(x - y) for x, y in zip(a1s, a2s)) / a
            print(
                f"| {name} | {mode} | {a:.1f} | {b:.1f} | {b / a:.3f} | {noise * 100:.1f}% | "
                f"{peak_a / 1048576:.0f} / {peak_b / 1048576:.0f} |",
                flush=True,
            )


if __name__ == "__main__":
    main()
