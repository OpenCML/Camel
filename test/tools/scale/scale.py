"""Scale tests: programs made of N ordinary functions.

A program of a given shape has N small functions and a `main` that calls every one of them and
prints the sum of their results, so that the expected output is known without running Camel.
The shapes differ in what each function contains, because that decides which memory space of
the compiler the function takes room in:

  arith    integer arithmetic only
  if       one `if` expression (two branch-arm graphs per function)
  string   one string literal (a constant object owned by the function's graph)
  array    one array literal
  lambda   one lambda passed to `map`
  mixed    all of the above in turn

Commands:

  gen <shape> <n>                   print the program
  run <shape> <n> [pass...]         compile and run it, compare the output, report the wall
                                    time and the peak resident memory of the camel process
  threshold <shape> [pass...]       the largest n that compiles and runs (searched up to --max)
  check                             the fixed set the test plan runs (see CHECK)
  limit <space> <mb> <shape> <n> [pass...]
                                    run the program with the limit of one space (graph, perm
                                    or meta) set to <mb> megabytes and print what camel
                                    printed; succeeds when camel refused the program (exit 1)

The peak memory is read from the resource usage of the child process and is only available on
POSIX hosts; elsewhere it is reported as 0.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import List, Optional, Tuple

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[2]

SHAPES = ["arith", "if", "string", "array", "lambda", "mixed"]


def function(shape: str, i: int) -> Tuple[str, int]:
    """One function `f<i>(x: int): int` of the shape and its result for the argument 1."""
    k = i % 7 + 1
    if shape == "arith":
        return f"func f{i}(x: int): int {{\n    return x * {k} + {i}\n}}\n", k + i
    if shape == "if":
        return (
            f"func f{i}(x: int): int {{\n    return if x > {k} then x - {k} else x + {i}\n}}\n",
            1 - k if 1 > k else 1 + i,
        )
    if shape == "string":
        text = f"s{i}"
        return (
            f"func f{i}(x: int): int {{\n    return len('{text}') + x\n}}\n",
            len(text) + 1,
        )
    if shape == "array":
        return (
            f"func f{i}(x: int): int {{\n    return len([x, {k}, {i}]) + {k}\n}}\n",
            3 + k,
        )
    if shape == "lambda":
        return (
            f"func f{i}(x: int): int {{\n"
            f"    return len([x, {k}]->map<(p: int): int => p + {i}>) + {k}\n}}\n",
            2 + k,
        )
    raise ValueError(f"unknown shape {shape}")


def program(shape: str, n: int) -> Tuple[str, int]:
    """The program text and the number it prints."""
    parts = [f"module scale_{shape}_{n}\n"]
    total = 0
    simple = [s for s in SHAPES if s != "mixed"]
    for i in range(n):
        text, value = function(simple[i % len(simple)] if shape == "mixed" else shape, i)
        parts.append(text)
        total += value
    # The calls are summed in chunks so that no single expression or graph grows with n beyond
    # what one function of ordinary size holds.
    chunk = 50
    helpers = []
    for c in range(0, n, chunk):
        calls = " + ".join(f"f{i}(1)" for i in range(c, min(n, c + chunk)))
        helpers.append(f"func sum{c // chunk}(): int {{\n    return {calls}\n}}\n")
    parts.extend(helpers)
    body = ["func main(): int sync {", "    var total = 0"]
    for h in range(len(helpers)):
        body.append(f"    total = total + sum{h}()")
    body.append("    println('scale total={}'->format<total>)")
    body.append("    return 0")
    body.append("}")
    parts.append("\n".join(body) + "\n")
    return "\n".join(parts), total


def camel_exe() -> str:
    home = os.environ.get("CAMEL_HOME") or str(REPO_ROOT / "out" / "latest")
    return str(Path(home) / "bin" / ("camel.exe" if os.name == "nt" else "camel"))


class Result:
    def __init__(self, ok: bool, seconds: float, peak_kb: int, detail: str, spaces: str = ""):
        self.ok = ok
        self.seconds = seconds
        self.peak_kb = peak_kb
        self.detail = detail
        # The line `[camel] spaces: ...` the run printed at exit (with CAMEL_GC_STATS set).
        self.spaces = spaces


def run_file(path: Path, passes: List[str], env_extra: Optional[dict] = None,
             timeout: float = 600.0, options: Optional[List[str]] = None
             ) -> Tuple[int, str, str, float, int]:
    """Runs camel on the file: exit code, stdout, stderr, seconds, peak resident KB. `options`
    are command-line options written before the file."""
    exe = camel_exe()
    env = dict(os.environ)
    env.setdefault("CAMEL_HOME", str(Path(exe).resolve().parents[1]))
    if env_extra:
        env.update(env_extra)
    start = time.perf_counter()
    proc = subprocess.Popen([exe, *(options or []), str(path), *passes], stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, env=env)
    peak_kb = 0
    try:
        if os.name == "posix":
            # wait4 gives the resource usage of exactly this child.
            import threading

            out_chunks: List[bytes] = []
            err_chunks: List[bytes] = []
            readers = [
                threading.Thread(target=lambda: out_chunks.append(proc.stdout.read())),
                threading.Thread(target=lambda: err_chunks.append(proc.stderr.read())),
            ]
            for r in readers:
                r.start()
            killer = threading.Timer(timeout, proc.kill)
            killer.start()
            _, status, usage = os.wait4(proc.pid, 0)
            killer.cancel()
            for r in readers:
                r.join()
            proc.returncode = os.waitstatus_to_exitcode(status)
            # Kilobytes on Linux, bytes on macOS.
            peak_kb = int(usage.ru_maxrss) // (1024 if sys.platform == "darwin" else 1)
            out, err = b"".join(out_chunks), b"".join(err_chunks)
        else:
            out, err = proc.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.kill()
        out, err = proc.communicate()
    seconds = time.perf_counter() - start
    return (proc.returncode, out.decode("utf-8", "replace"), err.decode("utf-8", "replace"),
            seconds, peak_kb)


def run_shape(shape: str, n: int, passes: List[str], env_extra: Optional[dict] = None) -> Result:
    text, total = program(shape, n)
    with tempfile.TemporaryDirectory(prefix="camel_scale_") as tmp:
        path = Path(tmp) / f"scale_{shape}_{n}.cml"
        path.write_text(text, encoding="utf-8", newline="\n")
        code, out, err, seconds, peak_kb = run_file(path, passes, env_extra)
    expected = f"scale total={total}"
    spaces = next((line for line in err.splitlines() if line.startswith("[camel] spaces:")), "")
    if code == 0 and expected in out.splitlines():
        return Result(True, seconds, peak_kb, "", spaces)
    detail = (out + err).strip().splitlines()
    return Result(False, seconds, peak_kb,
                  f"exit {code}, expected '{expected}': " + " | ".join(detail[:3])[:400], spaces)


LIMIT_ENV = {
    "graph": "CAMEL_GRAPH_SPACE_LIMIT_MB",
    "perm": "CAMEL_PERM_SPACE_LIMIT_MB",
    "meta": "CAMEL_META_SPACE_LIMIT_MB",
}


def run_limited(space: str, megabytes: int, shape: str, n: int, passes: List[str]) -> int:
    """Runs the program with one space limited; prints camel's output, with the diagnostics as
    JSON so that their code and stage can be checked. 0 when the program was refused."""
    text, _ = program(shape, n)
    with tempfile.TemporaryDirectory(prefix="camel_scale_") as tmp:
        path = Path(tmp) / f"scale_{shape}_{n}.cml"
        path.write_text(text, encoding="utf-8", newline="\n")
        code, out, err, _, _ = run_file(path, passes, {LIMIT_ENV[space]: str(megabytes)},
                                        options=["--output-format", "json"])
    sys.stdout.write(out)
    sys.stdout.write(err)
    print(f"camel exit={code}")
    return 0 if code == 1 else 1


def threshold(shape: str, passes: List[str], limit: int) -> int:
    """The largest n <= limit for which the program runs, assuming failure is monotonic."""
    if not run_shape(shape, 1, passes).ok:
        return 0
    lo, hi = 1, 2
    while hi <= limit and run_shape(shape, hi, passes).ok:
        lo, hi = hi, hi * 2
    if hi > limit:
        if run_shape(shape, limit, passes).ok:
            return limit
        hi = limit
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if run_shape(shape, mid, passes).ok:
            lo = mid
        else:
            hi = mid
    return lo


# What the test plan runs: (shape, number of functions, passes). The sizes are far beyond what
# the fixed-capacity spaces held (62 graphs with a constant, 80 functions with an `if`).
CHECK = [
    ("arith", 500, []),
    ("if", 500, []),
    ("string", 500, []),
    ("array", 500, []),
    ("lambda", 500, []),
    ("mixed", 2000, []),
    ("mixed", 2000, ["std::fvm"]),
    ("mixed", 500, ["std::jit"]),
    ("mixed", 500, ["std::tf"]),
]


def main(argv: List[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="cmd", required=True)
    g = sub.add_parser("gen")
    g.add_argument("shape", choices=SHAPES)
    g.add_argument("n", type=int)
    r = sub.add_parser("run")
    r.add_argument("shape", choices=SHAPES)
    r.add_argument("n", type=int)
    r.add_argument("passes", nargs="*")
    t = sub.add_parser("threshold")
    t.add_argument("shape", choices=SHAPES)
    t.add_argument("passes", nargs="*")
    t.add_argument("--max", type=int, default=4096)
    sub.add_parser("check")
    lim = sub.add_parser("limit")
    lim.add_argument("space", choices=sorted(LIMIT_ENV))
    lim.add_argument("megabytes", type=int)
    lim.add_argument("shape", choices=SHAPES)
    lim.add_argument("n", type=int)
    lim.add_argument("passes", nargs="*")
    args = parser.parse_args(argv)

    if args.cmd == "gen":
        sys.stdout.write(program(args.shape, args.n)[0])
        return 0
    if args.cmd == "run":
        res = run_shape(args.shape, args.n, args.passes)
        print(f"{'OK' if res.ok else 'FAIL'} {args.shape} n={args.n} "
              f"passes=[{' '.join(args.passes)}] {res.seconds:.2f}s peak={res.peak_kb // 1024}MB "
              f"{res.detail}")
        if res.spaces:
            print(res.spaces)
        return 0 if res.ok else 1
    if args.cmd == "limit":
        return run_limited(args.space, args.megabytes, args.shape, args.n, args.passes)
    if args.cmd == "threshold":
        n = threshold(args.shape, args.passes, args.max)
        print(f"threshold {args.shape} passes=[{' '.join(args.passes)}]: "
              f"{'>=' if n >= args.max else ''}{n}")
        return 0
    failed = 0
    for shape, n, passes in CHECK:
        res = run_shape(shape, n, passes)
        failed += 0 if res.ok else 1
        print(f"{'ok  ' if res.ok else 'FAIL'} {shape:7} n={n:5} "
              f"passes=[{' '.join(passes)}] {res.seconds:6.2f}s peak={res.peak_kb // 1024}MB "
              f"{res.detail}")
    print(f"{len(CHECK) - failed} of {len(CHECK)} scale programs ran; "
          f"{'PASS' if failed == 0 else 'FAILED'}")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
