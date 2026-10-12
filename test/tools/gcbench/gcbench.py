#!/usr/bin/env python3
"""Compare the two collectors on allocation benchmarks.

    python3 test/tools/gcbench/gcbench.py [--rounds N] [--only NAME[,NAME...]] [--json FILE]

Every benchmark is one `camel` command. It is run `--rounds` times with each collector,
alternating between them (mark-sweep, generational, mark-sweep, ...) so that both see the same
drift of a machine that is busy with other work; the table gives the minimum wall time of each,
the spread (max / min) as a measure of the noise, the peak resident size of the process and
what the allocator reports with CAMEL_GC_STATS=1 (collections, time stopped, longest stop).

The numbers are only comparable within one invocation on one machine.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
CASES = os.path.join(ROOT, "test", "cases")
GC = os.path.join(CASES, "perf", "gc")
MM = os.path.join(CASES, "feat", "mm", "gc")
PARA = os.path.join(CASES, "para", "core")

# name, arguments after `camel`, extra environment
BENCHMARKS = [
    ("alloc_small.nvm", [os.path.join(GC, "alloc_small.cml"), "--arg", "reps=1000000", "std::nvm"], {}),
    ("alloc_small.fvm", [os.path.join(GC, "alloc_small.cml"), "--arg", "reps=1000000", "std::fvm"], {}),
    ("map_chain.nvm", [os.path.join(GC, "map_chain.cml"), "--arg", "n=2000", "--arg", "reps=300", "std::nvm"], {}),
    ("map_chain.fvm", [os.path.join(GC, "map_chain.cml"), "--arg", "n=2000", "--arg", "reps=300", "std::fvm"], {}),
    ("map_chain.prl", [os.path.join(GC, "map_chain.cml"), "--arg", "n=2000", "--arg", "reps=300", "std::prl"], {}),
    ("build_tree.nvm", [os.path.join(GC, "build_tree.cml"), "--arg", "depth=13", "--arg", "reps=40", "std::nvm"], {}),
    ("build_tree.fvm", [os.path.join(GC, "build_tree.cml"), "--arg", "depth=13", "--arg", "reps=40", "std::fvm"], {}),
    ("soak.nvm", [os.path.join(MM, "gc_soak.cml"), "--arg", "rows=20000", "--arg", "reps=20000", "std::nvm"], {}),
    ("soak.fvm", [os.path.join(MM, "gc_soak.cml"), "--arg", "rows=20000", "--arg", "reps=20000", "std::fvm"], {}),
    ("live_table.fvm", [os.path.join(MM, "gc_live_table.cml"), "--arg", "rows=56000", "--arg", "reps=8", "std::fvm"], {}),
    ("live_large_256mb.fvm", [os.path.join(MM, "gc_live_large.cml"), "--arg", "mb=256", "--arg", "reps=40", "std::fvm"], {}),
    ("merge_sort_small.nvm", [os.path.join(PARA, "merge_sort_small.cml"), "std::nvm"], {}),
    ("merge_sort_small.prl", [os.path.join(PARA, "merge_sort_small.cml"), "std::prl"], {}),
    ("block_sort_merge.prl", [os.path.join(PARA, "block_sort_merge.cml"), "std::prl"], {}),
    ("concurrent_alloc.prl", [os.path.join(PARA, "concurrent_alloc.cml"), "std::prl"], {}),
    ("fib32.nvm", [os.path.join(GC, "fib.cml"), "--arg", "n=32", "std::nvm"], {}),
    ("fib32.fvm", [os.path.join(GC, "fib.cml"), "--arg", "n=32", "std::fvm"], {}),
    ("fib35.jit", [os.path.join(GC, "fib.cml"), "--arg", "n=35", "std::inline", "std::jit"], {}),
]

MODES = ["mark-sweep", "generational"]


def run_once(camel, args, mode, extra_env):
    env = dict(os.environ)
    env.update(extra_env)
    env["CAMEL_GC"] = mode
    env["CAMEL_GC_STATS"] = "1"
    for name in ("CAMEL_GC_ENABLE_YOUNG_COPYING", "CAMEL_GC_POISON"):
        env.pop(name, None)
    started = time.perf_counter()
    proc = subprocess.Popen([camel] + args, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=ROOT)
    out, err = proc.communicate()
    wall = time.perf_counter() - started
    stats = {}
    line = re.search(r"\[camel\] gc: (.*)", err.decode("utf-8", "replace"))
    if line:
        for key, value in re.findall(r"(\w+)=([0-9.]+)", line.group(1)):
            stats[key] = float(value)
    return {"wall": wall, "exit": proc.returncode, "stats": stats, "stdout": out.decode("utf-8", "replace").strip().splitlines()[-1:] }


def peak_rss_kb(camel, args, mode, extra_env):
    """Peak resident size of one more run, read from the kernel's accounting of the child."""
    import resource
    env = dict(os.environ)
    env.update(extra_env)
    env["CAMEL_GC"] = mode
    for name in ("CAMEL_GC_ENABLE_YOUNG_COPYING", "CAMEL_GC_POISON"):
        env.pop(name, None)
    pid = os.fork()
    if pid == 0:
        devnull = os.open(os.devnull, os.O_WRONLY)
        os.dup2(devnull, 1)
        os.dup2(devnull, 2)
        os.chdir(ROOT)
        os.execve(camel, [camel] + args, env)
    _, _, usage = os.wait4(pid, 0)
    return usage.ru_maxrss


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--rounds", type=int, default=5)
    parser.add_argument("--only", default="")
    parser.add_argument("--json", default="")
    parser.add_argument("--camel", default=os.path.join(ROOT, "out", "latest", "bin", "camel"))
    options = parser.parse_args()
    only = [name for name in options.only.split(",") if name]
    results = []
    header = (f"{'benchmark':<22} {'collector':<13} {'min s':>8} {'spread':>7} {'rss MB':>8} "
              f"{'minor':>7} {'major':>6} {'stop ms':>9} {'max stop ms':>12}")
    print(header)
    for name, args, extra_env in BENCHMARKS:
        if only and not any(name.startswith(item) for item in only):
            continue
        samples = {mode: [] for mode in MODES}
        last = {}
        failed = False
        for _ in range(options.rounds):
            for mode in MODES:
                result = run_once(options.camel, args, mode, extra_env)
                if result["exit"] != 0:
                    failed = True
                samples[mode].append(result["wall"])
                last[mode] = result
        outputs = {mode: last[mode]["stdout"] for mode in MODES}
        for mode in MODES:
            stats = last[mode]["stats"]
            rss = peak_rss_kb(options.camel, args, mode, extra_env) / 1024.0
            row = {
                "benchmark": name,
                "collector": mode,
                "min_s": min(samples[mode]),
                "spread": max(samples[mode]) / min(samples[mode]),
                "rss_mb": rss,
                "minor": stats.get("minor", 0),
                "major": stats.get("major", 0),
                "stop_ms": stats.get("minor_pause_ms", 0) + stats.get("major_pause_ms", 0),
                "max_stop_ms": max(stats.get("minor_pause_max_ms", 0), stats.get("major_pause_max_ms", 0)),
                "failed": failed,
                "same_output": outputs[MODES[0]] == outputs[MODES[1]],
            }
            results.append(row)
            flag = "  FAILED" if failed else ("" if row["same_output"] else "  OUTPUT DIFFERS")
            print(f"{name:<22} {mode:<13} {row['min_s']:>8.3f} {row['spread']:>7.2f} {row['rss_mb']:>8.1f} "
                  f"{int(row['minor']):>7} {int(row['major']):>6} {row['stop_ms']:>9.1f} {row['max_stop_ms']:>12.2f}{flag}")
        sys.stdout.flush()
    if options.json:
        with open(options.json, "w", encoding="utf-8") as out:
            json.dump(results, out, indent=2)


if __name__ == "__main__":
    main()
