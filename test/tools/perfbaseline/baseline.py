#!/usr/bin/env python3
"""Performance and memory baseline of the graph store (docs/performance/graph-store-baseline.md).

For every program of the list below, on this machine and with the build in out/latest:

  compile   `camel <program> std::stats std::null`: parsing and building the graph, then
            discarding it (std::null consumes the program, so nothing runs). The wall time of the
            whole process, the nodes, edges and bytes std::stats reports, and the peak working
            set of the process.
  opt       `std::opt::inline`: timed with `--timeit` (which compiles again for every iteration
            and times only the pass), and the peak working set of one run of the pass.
  execute   `std::nvm`, `std::fvm`, `std::jit`: the pass's mean time over `--rounds`
            iterations, and the peak working set of one full run of the program.

    python3 test/tools/perfbaseline/baseline.py [--rounds N] [--only NAME[,NAME...]]
                                                [--json FILE] [--markdown FILE]

The peak working set is read from the process after it ended: Windows keeps the process object
alive while a handle to it is open, so the number is exact, not sampled. Elsewhere the peak is
reported as "n/a".

The numbers are only comparable within one invocation on one machine: they drift with the load.
Nothing here optimizes anything; it writes down what the current code does.
"""

import argparse
import json
import os
import platform
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
CAMEL = os.path.join(ROOT, "out", "latest", "bin", "camel" + (".exe" if sys.platform == "win32" else ""))
CASES = os.path.join(ROOT, "test", "cases")

BACKENDS = ("nvm", "fvm", "jit")

# The training model of benchmarks/mlp: one step, one thread, no warm-up.
MLP_ENV = {
    "CAMEL_PACKAGES": os.path.join(ROOT, "benchmarks", "common"),
    "CAMEL_BENCH_WEIGHTS": os.path.join(ROOT, "benchmarks", "artifacts", "mlp", "weights"),
    "CAMEL_BENCH_INPUT": os.path.join(ROOT, "benchmarks", "artifacts", "mlp", "input.npy"),
    "CAMEL_BENCH_WORKLOAD": "train",
    "CAMEL_BENCH_THREADS": "1",
    "CAMEL_BENCH_WARMUP": "0",
    "CAMEL_BENCH_REPS": "1",
}

# name, arguments after `camel`, extra environment
PROGRAMS = [
    ("fib.deep", [os.path.join(CASES, "perf", "fib_recursive.cml")], {}),
    ("alloc_small", [os.path.join(CASES, "perf", "gc", "alloc_small.cml"), "--arg", "reps=200000"], {}),
    ("map_chain", [os.path.join(CASES, "perf", "gc", "map_chain.cml"), "--arg", "n=2000", "--arg", "reps=20"], {}),
    ("build_tree", [os.path.join(CASES, "perf", "gc", "build_tree.cml"), "--arg", "depth=12", "--arg", "reps=5"], {}),
    ("jit_const", [os.path.join(CASES, "perf", "gc", "jit_const.cml"), "--arg", "n=24"], {}),
    ("train.mlp", [os.path.join(ROOT, "benchmarks", "mlp", "model.cml")], MLP_ENV),
]


def open_process_query(pid):
    """A handle whose lifetime keeps the finished process's memory counters readable (Windows)."""
    if sys.platform != "win32":
        return None
    import ctypes
    from ctypes import wintypes

    kernel32 = ctypes.windll.kernel32
    kernel32.OpenProcess.restype = wintypes.HANDLE
    kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    return kernel32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION


def peak_working_set(handle):
    if handle is None:
        return None
    import ctypes
    from ctypes import wintypes

    class Counters(ctypes.Structure):
        _fields_ = [
            ("cb", wintypes.DWORD),
            ("PageFaultCount", wintypes.DWORD),
            ("PeakWorkingSetSize", ctypes.c_size_t),
            ("WorkingSetSize", ctypes.c_size_t),
            ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
            ("QuotaPagedPoolUsage", ctypes.c_size_t),
            ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
            ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
            ("PagefileUsage", ctypes.c_size_t),
            ("PeakPagefileUsage", ctypes.c_size_t),
        ]

    counters = Counters()
    counters.cb = ctypes.sizeof(Counters)
    if not ctypes.windll.psapi.GetProcessMemoryInfo(handle, ctypes.byref(counters), counters.cb):
        return None
    return counters.PeakWorkingSetSize


def close_handle(handle):
    if handle is not None:
        import ctypes

        ctypes.windll.kernel32.CloseHandle(handle)


def run_once(args, extra_env):
    env = dict(os.environ)
    env.update(extra_env)
    started = time.perf_counter()
    proc = subprocess.Popen(
        [CAMEL] + args, cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
    handle = open_process_query(proc.pid)
    out, err = proc.communicate()
    wall_ms = (time.perf_counter() - started) * 1000.0
    peak = peak_working_set(handle)
    close_handle(handle)
    return {
        "wall_ms": wall_ms,
        "exit": proc.returncode,
        "peak": peak,
        "stdout": out.decode("utf-8", "replace"),
        "stderr": err.decode("utf-8", "replace"),
    }


def run(args, extra_env, attempts=3):
    """One `camel` run: wall time in ms, exit code, peak working set in bytes and stdout.

    A run that dies of a signal is tried again (up to `attempts` times) and the retries are
    counted in the output: `--timeit` with heavy allocation has been seen to die of an access
    violation once in a few dozen runs (docs/performance/graph-store-baseline.md).
    """
    for attempt in range(1, attempts + 1):
        result = run_once(args, extra_env)
        if result["exit"] == 0:
            result["retries"] = attempt - 1
            return result
        sys.stderr.write(
            f"[baseline] attempt {attempt} of `camel {' '.join(args)}` exited with {result['exit']}\n"
        )
    sys.stderr.write(result["stderr"])
    raise SystemExit(f"camel {' '.join(args)} exited with {result['exit']} every time")


def graph_stats(stdout):
    match = re.search(r"GRAPH_STATS (\{[^}]*\})", stdout)
    return json.loads(match.group(1)) if match else {}


def timeit_mean_ms(stdout):
    """The mean of the `--timeit --output-format json` summary (a pretty-printed object)."""
    for match in reversed(list(re.finditer(r"\{", stdout))):
        payload = None
        try:
            payload, _ = json.JSONDecoder().raw_decode(stdout[match.start() :])
        except ValueError:
            continue
        if isinstance(payload, dict) and payload.get("kind") == "timeit":
            return float(payload["mean_ms"])
    return None


def megabytes(value):
    return "n/a" if value is None else f"{value / (1024 * 1024):.1f}"


def millis(value):
    if value is None:
        return "n/a"
    return f"{value / 1000:.2f} s" if value >= 1000 else f"{value:.1f} ms"


def machine_info():
    return {
        "machine": f"{platform.system()} {platform.release()} ({platform.machine()})",
        "cpu": platform.processor() or "unknown",
        "cores": os.cpu_count(),
        "python": platform.python_version(),
        "repo": ROOT,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--rounds", type=int, default=5)
    parser.add_argument("--only", default="")
    parser.add_argument("--json", default="")
    parser.add_argument("--markdown", default="")
    options = parser.parse_args()
    only = [name for name in options.only.split(",") if name]
    if not os.path.exists(CAMEL):
        raise SystemExit(f"no camel binary at {CAMEL}; run npm run build first")

    rows = []
    for name, args, env in PROGRAMS:
        if only and name not in only:
            continue
        sys.stderr.write(f"[baseline] {name}\n")
        stats = run(args + ["std::stats", "std::null"], env)
        graph = graph_stats(stats["stdout"])
        opt_peak = run(args + ["std::opt::inline", "std::null"], env)
        opt_time = run(["--timeit", f"std::opt::inline/{options.rounds}"] + args + ["--output-format", "json"], env)
        row = {
            "name": name,
            "graphs": graph.get("graphs"),
            "nodes": graph.get("nodes"),
            "edges": graph.get("edges"),
            "bytes": graph.get("bytes"),
            "bytes_per_node": (graph["bytes"] / graph["nodes"]) if graph.get("nodes") else None,
            "compile_ms": stats["wall_ms"],
            "compile_peak": stats["peak"],
            "opt_ms": timeit_mean_ms(opt_time["stdout"]),
            "opt_peak": opt_peak["peak"],
            "exec_ms": {},
            "exec_peak": {},
        }
        for backend in BACKENDS:
            timed = run(["--timeit", f"std::{backend}/{options.rounds}"] + args + ["--output-format", "json"], env)
            ran = run(args + [f"std::{backend}"], env)
            row["exec_ms"][backend] = timeit_mean_ms(timed["stdout"])
            row["exec_peak"][backend] = ran["peak"]
        rows.append(row)

    info = machine_info()
    header = (
        "| 程序 | 图 | 节点 | 边 | 图字节 | 字节/节点 | 编译 | "
        + " | ".join(f"执行 {backend}" for backend in BACKENDS)
        + " | 峰值（编译 / 优化 / "
        + " / ".join(BACKENDS)
        + "） |"
    )
    divider = "|---|---|---|---|---|---|---|" + "---|" * len(BACKENDS) + "---|"
    lines = []
    for row in rows:
        peaks = [row["compile_peak"], row["opt_peak"]] + [row["exec_peak"][b] for b in BACKENDS]
        per_node = "n/a" if row["bytes_per_node"] is None else f"{row['bytes_per_node']:.1f}"
        lines.append(
            f"| {row['name']} | {row['graphs']} | {row['nodes']} | {row['edges']} | {row['bytes']} | "
            f"{per_node} | {millis(row['compile_ms'])} | "
            + " | ".join(millis(row["exec_ms"][b]) for b in BACKENDS)
            + " | "
            + " / ".join(f"{megabytes(p)} MB" for p in peaks)
            + " |"
        )
    markdown = "\n".join(
        [
            f"机器：{info['machine']}，{info['cpu']} × {info['cores']}，Python {info['python']}。",
            "",
            header,
            divider,
            *lines,
            "",
        ]
    )
    print(markdown)
    if options.markdown:
        with open(options.markdown, "w", encoding="utf-8", newline="\n") as file:
            file.write(markdown)
    if options.json:
        with open(options.json, "w", encoding="utf-8", newline="\n") as file:
            json.dump({"machine": info, "rounds": options.rounds, "programs": rows}, file, indent=2)
            file.write("\n")


if __name__ == "__main__":
    main()