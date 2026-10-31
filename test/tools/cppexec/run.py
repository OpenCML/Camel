#!/usr/bin/env python3
# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""Translate Camel programs with std::cpp, build and run what it writes, and compare with std::fvm.

    python3 test/tools/cppexec/run.py [options] <program.cml | directory>...
    python3 test/tools/cppexec/run.py --expect test/tools/cppexec/expect.txt
    python3 test/tools/cppexec/run.py --have-compiler
    python3 test/tools/cppexec/run.py --link-line
    python3 test/tools/cppexec/run.py --standalone <program.cml>...

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
the form the test plan uses (test/plans/feat/trans/cpp.plan.toml). A path may be followed by the
arguments of the program's `main` (`--arg name=value ...`, design principles 10.6): `camel`
is given them before the file, the generated program on its command line. A program may be
listed several times with different arguments (or none: the report of the missing ones).

--have-compiler exits 0 when a C++ compiler is found (the plan's precondition), 1 otherwise.

A bridged program is linked with the standard library of the compiler it is built with: the
directory of `libstdc++.so` that the compiler itself reports, on the link line and in the
program's rpath. The host's default search order can find an older libstdc++ than the one
libcamel.so was built with, and the link then fails with `undefined reference to
std::__detail::__wait_impl(...)@GLIBCXX_3.4.35`. When the compiler names no such library (MSVC,
libc++, a driver that prints the name back), the link line is the one it was.

--link-line prints the command a bridged program is linked with and checks that it names that
directory: exit 0 if it does, or if the compiler names none (nothing to name), 1 if the link line
lost it.

--standalone <program>... builds each program the way agree_with_fvm builds it -- with the link
line its generated code asks for -- and checks the opposite of the same thing: the program must
name no operator of the bridge (camel/standalone/bridge.h) and its built executable must need no
Camel library, which `ldd` answers. "Standalone" is therefore a claim about the artifact, not
about the writer's intentions: a value the bridge cannot carry (a bf16, an f16) that is emitted
as a bridged call is caught here, and the link line that names libcamel is printed with it.
Exit 0 when every program is standalone, 1 otherwise.
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
# A statement that is only `x->append<...>` or `x->extend<...>`: it relies on the change in place.
IN_PLACE = re.compile(r"^\s*[\w.\[\]]+->(append|extend)<.*>\s*$", re.M)


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
    cmd = [compiler, "-std=c++20", "-O2", "-ffp-contract=off", "-w", "-I", include] +         (["-include-pch", pch] if pch else []) + [source, "-o", target]
    if not bridged:
        return cmd
    if os.name == "nt":
        return cmd + [os.path.join(lib, "libcamel.lib")]
    # The standard library of the compiler, ahead of the linker's defaults: libcamel.so is built
    # with a newer libstdc++ than the one a host's default search order finds (measured:
    # GLIBCXX_3.4.35 and 3.4.36), and the link then fails with "undefined reference to
    # std::__detail::__wait_impl(...)@GLIBCXX_3.4.35". The rpath makes the program start with the
    # library it was linked against, whatever LD_LIBRARY_PATH holds at run time.
    stdlib = stdlib_dir(compiler)
    if stdlib:
        cmd += ["-L" + stdlib, "-Wl,-rpath," + stdlib]
    return cmd + ["-L" + lib, "-lcamel", "-Wl,-rpath," + lib]


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


STDLIB = {}


def stdlib_dir(compiler):
    """The directory of the C++ standard library the compiler itself resolves, or None.

    `-print-file-name=libstdc++.so` is the compiler's own answer to "where is the library you
    link against" -- `dirname` of it is the `-L` that makes the link use the library the compiler
    was built with instead of the host's default one. Nothing of a host is written here: a driver
    that has no answer (MSVC, libc++, or one that prints the name back unchanged) gives None, and
    the link line stays the one it was. Asked once per compiler, from the threads of the pool."""
    if compiler in STDLIB:
        return STDLIB[compiler]
    found = None
    code, out, _, _ = run([compiler, "-print-file-name=libstdc++.so"], 60)
    path = out.strip().splitlines()[0].strip() if code == 0 and out.strip() else ""
    if os.path.isabs(path) and os.path.exists(path):
        found = os.path.dirname(path)
    STDLIB[compiler] = found
    return found


def check_link_line(compiler, msvc):
    """The command a bridged program is linked with, printed and checked: it must name the
    standard library of the compiler. Deleting the `-L` of stdlib_dir from build_command turns
    this red (the plan's case link_line_keeps_the_compilers_stdlib)."""
    camel_home = os.path.join(ROOT, "out", "latest")
    cmd = build_command(compiler, msvc, "PROGRAM.cpp", "PROGRAM" + EXE, True, camel_home)
    print("link: " + " ".join(cmd))
    if msvc or os.name == "nt":
        print("PASS: this platform's link line takes the runtime library alone: no libstdc++ to name")
        return 0
    stdlib = stdlib_dir(compiler)
    print("stdlib: " + (stdlib or "none"))
    if stdlib is None:
        print("PASS: the compiler names no libstdc++.so; the link line is the one it was")
        return 0
    if "-L" + stdlib not in cmd:
        print("FAIL: the compiler's own libstdc++ directory %s is not on the link line" % stdlib)
        return 1
    print("PASS: the link line names the compiler's own libstdc++ directory (%s)" % stdlib)
    return 0


def needs_of(target, env):
    """The shared libraries the built program needs, as the platform reports them: `ldd` on
    Linux, `otool -L` on macOS, and (on Windows, where an import is resolved when the program
    starts) the import table `dumpbin /dependents` prints. Empty when the platform has no tool."""
    if os.name == "nt":
        cmd = ["dumpbin", "/dependents", target]
    elif sys.platform == "darwin":
        cmd = ["otool", "-L", target]
    else:
        cmd = ["ldd", target]
    try:
        code, out, err, _ = run(cmd, 60, env)
    except OSError:
        return None
    if code != 0:
        return None
    return out + err


def check_standalone(programs, args):
    """Every program written by std::cpp must be one the Camel runtime does not have to be part
    of: the generated code names no bridge, and the executable built from it needs no Camel
    library. The verdict is the artifact's (`ldd`), not the writer's."""
    compiler, msvc = find_compiler()
    if not compiler:
        print("FAIL: no C++ compiler found (set CXX)")
        return 2
    env = dict(os.environ)
    env.setdefault("CAMEL_HOME", os.path.join(ROOT, "out", "latest"))
    workdir = tempfile.mkdtemp(prefix="cppexec-standalone-")
    failed = 0
    for path in programs:
        rel = os.path.relpath(os.path.abspath(path), ROOT).replace("\\", "/")
        code, out, err, _ = run([CAMEL, rel] + args.passes + ["std::cpp"], args.timeout, env)
        if code != 0 or "#include" not in out:
            print("FAIL %s: std::cpp did not write a program" % rel)
            print("  " + (err or out).strip().splitlines()[-1][:200])
            failed += 1
            continue
        bridges = "camel/standalone/bridge.h" in out
        stem = re.sub(r"[^A-Za-z0-9_]", "_", rel)[:120]
        source = os.path.join(workdir, stem + ".cpp")
        target = os.path.join(workdir, stem + EXE)
        with open(source, "w", encoding="utf-8", newline="\n") as f:
            f.write(out)
        cmd = build_command(compiler, msvc, source, target, bridges, env["CAMEL_HOME"])
        print("%s" % rel)
        print("  bridge: %s" % ("yes" if bridges else "no"))
        print("  link: " + " ".join(cmd))
        code, cout, cerr, _ = run(cmd, 600, env, cwd=workdir)
        if code != 0:
            errors = [l for l in (cout + cerr).splitlines() if "error" in l]
            print("  FAIL: does not build: " + (errors[0] if errors else "")[:200])
            failed += 1
            continue
        needs = needs_of(target, env)
        if needs is None:
            print("  PASS: no library tool on this platform; the link line names no Camel library"
                  if "libcamel" not in " ".join(cmd) else "  FAIL: the link line names libcamel")
            failed += 0 if "libcamel" not in " ".join(cmd) else 1
            continue
        camel = sorted({l.strip() for l in needs.splitlines() if "libcamel" in l})
        if camel:
            print("  FAIL: the built program needs the Camel runtime:")
            for line in camel:
                print("    " + line)
            failed += 1
        else:
            print("  needs: none of the Camel runtime")
    shutil.rmtree(workdir, ignore_errors=True)
    if failed:
        print("FAIL: %d of %d program(s) are not standalone" % (failed, len(programs)))
        return 1
    print("PASS: %d program(s) need no Camel runtime" % len(programs))
    return 0


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


def check(entry, workdir, args):
    """The record of one program, run with the arguments of its `main` (`entry` is the path and
    those arguments)."""
    program, entry_args = entry
    rel = os.path.relpath(program, ROOT).replace("\\", "/")
    rec = {"program": rel + ("".join(" " + a for a in entry_args))}
    env = dict(os.environ)
    env.setdefault("CAMEL_HOME", os.path.join(ROOT, "out", "latest"))
    if os.name == "nt":
        # A bridged executable imports libcamel.dll before its main function can run.
        env["PATH"] = os.path.join(env["CAMEL_HOME"], "libs") + os.pathsep + env.get("PATH", "")

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

    stem = re.sub(r"[^A-Za-z0-9_]", "_", rec["program"])[:150]
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

    fcode, fout, ferr, _ = run([CAMEL] + entry_args + [rel] + args.passes + ["std::fvm"], args.timeout, env)
    if fcode is None:
        rec["outcome"] = "not a program"
        rec["detail"] = "std::fvm does not finish within %s s" % args.timeout
        return rec
    ccode, xout, xerr, _ = run([target] + entry_args, args.timeout, env)
    rec["fvm_exit"], rec["cpp_exit"] = fcode, ccode
    if ccode is None or ccode < 0:
        rec["outcome"] = "crashes"
        rec["detail"] = "timed out" if ccode is None else "signal %d" % -ccode
    elif fout == xout and fcode == ccode:
        rec["outcome"] = "same"
    elif IN_PLACE.search(open(program, encoding="utf-8", errors="replace").read()):
        # Design principles 11.3: `append` returns a new array; the runtime still changes the
        # array in place, and a program that relies on it (the result of a statement
        # `xs->append<x>` unused) is to become a compile-time error.
        rec["outcome"] = "differs: in-place append (11.3)"
        rec["detail"] = first_difference(fout, xout) if fout != xout else "exit codes"
    else:
        rec["outcome"] = "differs"
        rec["detail"] = ("exit fvm %s, cpp %s; " % (fcode, ccode)) + (
            first_difference(fout, xout) if fout != xout else "same output")
    return rec


def expand(paths):
    """(program, arguments of its main) for every path: a directory gives its programs, a path
    followed by arguments (an entry of an --expect list) that program with them."""
    files = []
    for entry in paths:
        p, *entry_args = entry.split()
        p = os.path.join(ROOT, p) if not os.path.isabs(p) else p
        if os.path.isdir(p):
            for root, _, names in os.walk(p):
                files += [(os.path.join(root, n), ()) for n in names if n.endswith(".cml")]
        else:
            files.append((p, tuple(entry_args)))
    return [(p, list(a)) for p, a in sorted(set(files))]


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
    ap.add_argument("--link-line", action="store_true",
                    help="print the command a bridged program is linked with, and check that it "
                         "names the compiler's own standard library")
    ap.add_argument("--standalone", action="store_true",
                    help="check that the named programs are built without the Camel runtime: no "
                         "bridge in the generated code, no Camel library in what it needs")
    ap.add_argument("--no-pch", action="store_true",
                    help="do not precompile camel/standalone/runtime.h (clang)")
    args = ap.parse_args()
    args.passes = args.passes.split()

    compiler, msvc = find_compiler()
    if args.have_compiler:
        print(compiler or "no C++ compiler found (set CXX)")
        return 0 if compiler else 1
    if not compiler:
        print("FAIL: no C++ compiler found (set CXX)")
        return 2
    if args.link_line:
        return check_link_line(compiler, msvc)
    if args.standalone:
        if not args.programs:
            ap.error("--standalone needs at least one program")
        return check_standalone(args.programs, args)

    expected = read_expect(args.expect) if args.expect else []
    programs = expand(args.programs + expected)
    if not programs:
        ap.error("no programs")

    workdir = os.path.abspath(args.keep) if args.keep else tempfile.mkdtemp(prefix="cppexec-")
    os.makedirs(workdir, exist_ok=True)
    args.pch = None
    if not args.no_pch and "clang" in os.path.basename(compiler).lower():
        pch = os.path.join(workdir, "runtime.pch")
        header = os.path.join(ROOT, "include", "camel", "standalone", "runtime.h")
        code, _, _, _ = run([compiler, "-std=c++20", "-O2", "-ffp-contract=off", "-I", os.path.join(ROOT, "include"),
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
        # Keyed as the records are: the program and the arguments of its main.
        wanted = {os.path.relpath(p, ROOT).replace("\\", "/") + "".join(" " + x for x in a)
                  for p, a in expand(expected)}
        bad = [r for r in records if r["program"] in wanted and r["outcome"] != "same"]
        for r in bad:
            print("FAIL %s: %s: %s" % (r["program"], r["outcome"], r.get("detail", "")))
        if bad:
            return 1
        print("PASS: %d of %d programs agree with std::fvm" % (len(wanted), len(wanted)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
