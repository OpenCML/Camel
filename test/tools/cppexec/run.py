#!/usr/bin/env python3
"""Translate Camel programs with std::cpp, build and run what it writes, and compare with std::fvm.

    python3 test/tools/cppexec/run.py [options] <program.cml | directory>...
    python3 test/tools/cppexec/run.py --expect test/tools/cppexec/expect.txt
    python3 test/tools/cppexec/run.py --have-compiler

For every program: `camel <program> [passes] std::cpp` writes a C++ file; the C++ compiler
($CXX, else clang++, g++ or c++) builds it with the headers of the repository (include/, where
camel/standalone/ lives); the executable runs; `camel <program> [passes] std::fvm` runs too
(after the same passes, so on the same graph). The two agree when their standard output and
exit code are the same (a fault's report is on standard output in both, design note
docs/design/cpp-standalone/README.cn.md 7.1).

Each program ends in one of these outcomes:

    same                 standard output and exit code as std::fvm
    differs              translated, built and run, but the output or exit code is not the same
    crashes              the executable died of a signal (or timed out)
    does not build       the C++ compiler rejected what std::cpp wrote
    refused              std::cpp refused the program (TranslationRefused): the reason is kept
    pass failed          std::cpp failed otherwise (an internal error)
    not a program        the program does not compile, or std::fvm does not run it

--expect <file> reads a list of programs (one path per line, relative to the repository; '#'
starts a comment) that must all be "same", runs them, and exits with 1 if one is not. That is
the form the test plan uses (test/plans/feat/trans/cpp.plan.toml).

--have-compiler exits 0 when a C++ compiler is found (the plan's precondition), 1 otherwise.
"""
import argparse
import collections
import concurrent.futures
import json
import os
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
EXE = ".exe" if os.name == "nt" else ""
CAMEL = os.environ.get("CAMEL", os.path.join(ROOT, "out", "latest", "bin", "camel" + EXE))
ANSI = re.compile(r"\x1b\[[0-9;]*m")


def find_compiler():
    """The C++ compiler and whether it takes MSVC-style options."""
    candidates = [os.environ.get("CXX")] if os.environ.get("CXX") else []
    candidates += ["clang++", "g++", "c++", "cl"]
    for name in candidates:
        path = shutil.which(name)
        if path:
            base = os.path.basename(path).lower()
            return path, base in ("cl", "cl.exe", "clang-cl", "clang-cl.exe")
    return None, False


def build_command(compiler, msvc, source, target, bridged, camel_home, pch=None):
    """A program that uses operators of runtime modules (bridged) links libcamel. `pch` is a
    precompiled camel/standalone/runtime.h (clang only): most of a compilation is spent in
    the standard library's headers it includes."""
    include = os.path.join(ROOT, "include")
    lib = os.path.join(camel_home, "libs")
    if msvc:
        cmd = [compiler, "/nologo", "/std:c++20", "/O2", "/EHsc", "/I", include, source,
               "/Fe" + target]
        return cmd + ([os.path.join(lib, "libcamel.lib")] if bridged else [])
    cmd = [compiler, "-std=c++20", "-O2", "-w", "-I", include] +         (["-include-pch", pch] if pch else []) + [source, "-o", target]
    return cmd + (["-L" + lib, "-lcamel", "-Wl,-rpath," + lib] if bridged else [])


def run(cmd, timeout, env=None, cwd=ROOT):
    """(exit code, stdout, stderr, seconds); exit code None on timeout."""
    started = time.monotonic()
    try:
        p = subprocess.run(cmd, capture_output=True, timeout=timeout, env=env, cwd=cwd)
        code = p.returncode
        out, err = p.stdout, p.stderr
    except subprocess.TimeoutExpired as e:
        code, out, err = None, e.stdout or b"", e.stderr or b""
    text = lambda b: ANSI.sub("", b.decode("utf-8", "replace"))
    return code, text(out), text(err), time.monotonic() - started


def first_difference(a, b):
    la, lb = a.splitlines(), b.splitlines()
    for i in range(max(len(la), len(lb))):
        x = la[i] if i < len(la) else "<end>"
        y = lb[i] if i < len(lb) else "<end>"
        if x != y:
            col = next((k for k in range(min(len(x), len(y))) if x[k] != y[k]), min(len(x), len(y)))
            start = max(0, col - 30)
            return "line %d, column %d: fvm %r, cpp %r" % (i + 1, col + 1, x[start:start + 70], y[start:start + 70])
    return "same lines, different line ends or trailing text"


def check(program, workdir, args):
    """The record of one program."""
    rel = os.path.relpath(program, ROOT).replace("\\", "/")
    rec = {"program": rel}
    env = dict(os.environ)
    env.setdefault("CAMEL_HOME", os.path.join(ROOT, "out", "latest"))

    code, out, err, _ = run([CAMEL, rel] + args.passes + ["std::cpp"], args.timeout, env)
    text = out + "\n" + err
    if code != 0 or "#include" not in out:
        if "TranslationRefused" in text or "refused the graph" in text:
            m = re.search(r"refused the graph: (.*)", text)
            rec["outcome"] = "refused"
            rec["detail"] = (m.group(1) if m else text.strip().splitlines()[-1]).strip()
        elif re.search(r"(Semantic|Syntax|Lexical|Parse)Diag::|requires a 'main'", text) or \
                "camel: " in text and "error" in text and "Internal" not in text:
            rec["outcome"] = "not a program"
            rec["detail"] = (text.strip().splitlines() or [""])[0][:160]
        else:
            rec["outcome"] = "pass failed"
            rec["detail"] = (text.strip().splitlines() or ["exit %s" % code])[-1][:200]
        return rec

    stem = re.sub(r"[^A-Za-z0-9_]", "_", rel)
    source = os.path.join(workdir, stem + ".cpp")
    target = os.path.join(workdir, stem + EXE)
    with open(source, "w", encoding="utf-8", newline="\n") as f:
        f.write(out)
    rec["standalone"] = "camel/standalone/bridge.h" not in out
    compiler, msvc = find_compiler()
    code, cout, cerr, seconds = run(
        build_command(compiler, msvc, source, target, not rec["standalone"], env["CAMEL_HOME"],
                      args.pch),
        600, env, cwd=workdir)
    rec["compile_seconds"] = round(seconds, 2)
    if code != 0:
        errors = [l for l in (cout + cerr).splitlines() if "error" in l]
        rec["outcome"] = "does not build"
        rec["detail"] = (errors[0] if errors else (cout + cerr).strip()[-200:])[:200]
        return rec

    fcode, fout, ferr, _ = run([CAMEL, rel] + args.passes + ["std::fvm"], args.timeout, env)
    if fcode is None:
        rec["outcome"] = "not a program"
        rec["detail"] = "std::fvm does not finish within %s s" % args.timeout
        return rec
    ccode, xout, xerr, _ = run([target], args.timeout, env)
    rec["fvm_exit"], rec["cpp_exit"] = fcode, ccode
    if ccode is None or ccode < 0:
        rec["outcome"] = "crashes"
        rec["detail"] = "timed out" if ccode is None else "signal %d" % -ccode
    elif fout == xout and fcode == ccode:
        rec["outcome"] = "same"
    else:
        rec["outcome"] = "differs"
        rec["detail"] = ("exit fvm %s, cpp %s; " % (fcode, ccode)) + (
            first_difference(fout, xout) if fout != xout else "same output")
    return rec


def expand(paths):
    files = []
    for p in paths:
        p = os.path.join(ROOT, p) if not os.path.isabs(p) else p
        if os.path.isdir(p):
            for root, _, names in os.walk(p):
                files += [os.path.join(root, n) for n in names if n.endswith(".cml")]
        else:
            files.append(p)
    return sorted(set(files))


def read_expect(path):
    entries = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if line:
                entries.append(line)
    return entries


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("programs", nargs="*")
    ap.add_argument("--expect", help="a list of programs that must all agree with std::fvm")
    ap.add_argument("--passes", default="", help="passes to run before std::cpp")
    ap.add_argument("--jobs", type=int, default=min(4, os.cpu_count() or 1))
    ap.add_argument("--timeout", type=float, default=60)
    ap.add_argument("--json", help="write the records to this file")
    ap.add_argument("--keep", help="keep the C++ files and executables in this directory")
    ap.add_argument("--have-compiler", action="store_true")
    ap.add_argument("--no-pch", action="store_true",
                    help="do not precompile camel/standalone/runtime.h (clang)")
    args = ap.parse_args()
    args.passes = args.passes.split()

    compiler, _ = find_compiler()
    if args.have_compiler:
        print(compiler or "no C++ compiler found (set CXX)")
        return 0 if compiler else 1
    if not compiler:
        print("FAIL: no C++ compiler found (set CXX)")
        return 2

    expected = read_expect(args.expect) if args.expect else []
    programs = expand(args.programs + expected)
    if not programs:
        ap.error("no programs")

    workdir = args.keep or tempfile.mkdtemp(prefix="cppexec-")
    os.makedirs(workdir, exist_ok=True)
    args.pch = None
    if not args.no_pch and "clang" in os.path.basename(compiler).lower():
        pch = os.path.join(workdir, "runtime.pch")
        header = os.path.join(ROOT, "include", "camel", "standalone", "runtime.h")
        code, _, _, _ = run([compiler, "-std=c++20", "-O2", "-I", os.path.join(ROOT, "include"),
                             "-x", "c++-header", header, "-o", pch], 600)
        args.pch = pch if code == 0 else None
    try:
        with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
            records = list(pool.map(lambda p: check(p, workdir, args), programs))
    finally:
        if not args.keep:
            shutil.rmtree(workdir, ignore_errors=True)

    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump(records, f, indent=1)

    counts = collections.Counter(r["outcome"] for r in records)
    print("programs: %d   compiler: %s" % (len(records), compiler))
    for outcome, n in counts.most_common():
        print("%6d  %5.1f%%  %s" % (n, 100.0 * n / len(records), outcome))
    same = [r for r in records if r["outcome"] == "same"]
    print("of the %d that agree, %d are standalone (no runtime linked)"
          % (len(same), sum(1 for r in same if r.get("standalone"))))
    times = [r["compile_seconds"] for r in records if "compile_seconds" in r]
    if times:
        print("C++ compile time: median %.2f s [%.2f, %.2f]"
              % (statistics.median(times), min(times), max(times)))
    refusals = collections.Counter(
        re.sub(r"'[^']*'", "'…'", r.get("detail", ""))[:110]
        for r in records if r["outcome"] == "refused")
    if refusals:
        print("\nmost frequent refusals:")
        for reason, n in refusals.most_common(15):
            print("%6d  %s" % (n, reason))
    print("\nnot the same:")
    for r in records:
        if r["outcome"] not in ("same", "refused", "not a program"):
            print("  %s: %s: %s" % (r["program"], r["outcome"], r.get("detail", "")))

    if args.expect:
        wanted = {os.path.relpath(p, ROOT).replace("\\", "/") for p in expand(expected)}
        bad = [r for r in records if r["program"] in wanted and r["outcome"] != "same"]
        for r in bad:
            print("FAIL %s: %s: %s" % (r["program"], r["outcome"], r.get("detail", "")))
        if bad:
            return 1
        print("PASS: %d of %d programs agree with std::fvm" % (len(wanted), len(wanted)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
