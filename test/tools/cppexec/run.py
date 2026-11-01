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


def build_pch(workdir, args):
    """Precompile runtime.h for clang: every generated program includes it, and compiling it once
    is most of the difference between a two-second build and a three-second one."""
    compiler, _ = find_compiler()
    args.pch = None
    if args.no_pch or not compiler or "clang" not in os.path.basename(compiler).lower():
        return None
    pch = os.path.join(workdir, "runtime.pch")
    header = os.path.join(ROOT, "include", "camel", "standalone", "runtime.h")
    code, _, _, _ = run([compiler, "-std=c++20", "-O2", "-ffp-contract=off",
                         "-I", os.path.join(ROOT, "include"),
                         "-x", "c++-header", header, "-o", pch], 600)
    args.pch = pch if code == 0 else None
    return args.pch


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


def check_standalone(paths, args):
    """Every program written by std::cpp must be one the Camel runtime does not have to be part
    of: the generated code names no bridge, and the executable built from it needs no Camel
    library. The verdict is the artifact's (`ldd`), not the writer's.

    `--expect` gives it a list, which is how the whole list is asked rather than one program: a
    property that holds for one case and is checked on that case is a property of the case."""
    compiler, msvc = find_compiler()
    if not compiler:
        print("FAIL: no C++ compiler found (set CXX)")
        return 2
    entries = expand(paths + (read_expect(args.expect) if args.expect else []))
    if not entries:
        print("FAIL: no programs")
        return 2
    env = dict(os.environ)
    env.setdefault("CAMEL_HOME", os.path.join(ROOT, "out", "latest"))
    workdir = tempfile.mkdtemp(prefix="cppexec-standalone-")
    build_pch(workdir, args)

    def one(entry):
        program, entry_args = entry
        program_rel = os.path.relpath(program, ROOT).replace("\\", "/")
        shown = program_rel + "".join(" " + a for a in entry_args)
        rec = {"program": shown, "same_program": program_rel, "bridge": None, "link": "",
               "ok": False, "lines": [], "reason": ""}
        # The arguments of a `main` are a run-time thing, so the translation does not see them:
        # the same program is written once, and what changes is what it is handed when it runs.
        code, out, err, _ = run([CAMEL, program_rel] + args.passes + ["std::cpp"],
                                args.timeout, env)
        if code != 0 or "#include" not in out:
            rec["lines"] = ["  FAIL: std::cpp did not write a program",
                            "  " + (err or out).strip().splitlines()[-1][:200]]
            rec["reason"] = "std::cpp did not write a program"
            return rec
        bridges = "camel/standalone/bridge.h" in out
        stem = re.sub(r"[^A-Za-z0-9_]", "_", program_rel)[:120]
        source = os.path.join(workdir, stem + ".cpp")
        target = os.path.join(workdir, stem + EXE)
        with open(source, "w", encoding="utf-8", newline="\n") as f:
            f.write(out)
        cmd = build_command(compiler, msvc, source, target, bridges, env["CAMEL_HOME"], args.pch)
        rec["bridge"], rec["link"] = bridges, " ".join(cmd)
        code, cout, cerr, _ = run(cmd, 600, env, cwd=workdir)
        if code != 0:
            errors = [l for l in (cout + cerr).splitlines() if "error" in l]
            rec["lines"] = ["  FAIL: does not build: " + (errors[0] if errors else "")[:200]]
            rec["reason"] = "does not build"
            return rec
        needs = needs_of(target, env)
        if needs is None:
            named = "libcamel" in rec["link"]
            rec["ok"] = not named
            rec["lines"] = (["  FAIL: the link line names libcamel"] if named else
                            ["  PASS: no library tool on this platform; the link line names no "
                             "Camel library"])
            rec["reason"] = "the link line names libcamel" if named else ""
            return rec
        camel = sorted({l.strip() for l in needs.splitlines() if "libcamel" in l})
        if camel:
            rec["lines"] = ["  FAIL: the built program needs the Camel runtime:"] + \
                           ["    " + line for line in camel]
            # What crossed: the bridge calls the written program makes, which is the reason the
            # link line has to name the runtime at all.
            crossed = [l.strip() for l in out.splitlines() if "bridge::" in l]
            if crossed:
                rec["lines"] += ["  crosses: " + crossed[0][:160]]
                for line in crossed[1:4]:
                    rec["lines"] += ["           " + line.strip()[:160]]
            rec["reason"] = "needs the Camel runtime"
            return rec
        rec["ok"] = True
        rec["lines"] = ["  needs: none of the Camel runtime"]
        return rec

    try:
        with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
            records = list(pool.map(one, entries))
    finally:
        shutil.rmtree(workdir, ignore_errors=True)
    failed = [rec for rec in records if not rec["ok"]]
    baseline = set()
    if args.baseline:
        path = os.path.join(ROOT, args.baseline)
        if os.path.exists(path):
            with open(path, encoding="utf-8") as f:
                for line in f:
                    name = line.split("#", 1)[0].strip()
                    if name:
                        baseline.add(name)
    # A program the baseline names is a known crossing, so its report is the baseline's; the
    # detail (which operator crossed, what `ldd` said) is printed for what the baseline does not
    # explain, or for the one program a report is asked about.
    detailed = records if len(records) == 1 else [
        rec for rec in failed if rec["same_program"] not in baseline]
    for rec in detailed:
        print("%s" % rec["program"])
        if rec["bridge"] is not None:
            print("  bridge: %s" % ("yes" if rec["bridge"] else "no"))
            print("  link: " + rec["link"])
        for line in rec["lines"]:
            print(line)
    if len(records) > 1:
        # One line per reason, names on that line: a list of forty names is still one line, and a
        # line is what a report can keep. Printing them one per line loses the verdict.
        by_reason = collections.defaultdict(list)
        for rec in records:
            if rec["ok"] or rec["same_program"] in baseline:
                continue
            by_reason[rec["reason"]].append(rec["same_program"])
        for reason, names in sorted(by_reason.items()):
            print("  %s (%d): %s" % (reason, len(names), ", ".join(sorted(set(names)))))
    if args.baseline:
        # A list of the programs allowed to link the runtime, and nothing else allowed: the whole
        # list is asked, and the answer has to be exactly the list -- a fifth program that links
        # it is red, and so is one of these four becoming standalone (the list is then wrong).
        actual = {rec["same_program"] for rec in failed}
        extra = sorted(actual - baseline)
        missing = sorted(baseline - actual)
        for name in extra:
            print("  links the runtime and is not in the baseline: " + name)
        for name in missing:
            print("  is in the baseline and no longer links the runtime: " + name)
        if extra or missing:
            print("FAIL: the programs that link the runtime are not the ones the baseline names "
                  "(%d extra, %d gone)" % (len(extra), len(missing)))
            return 1
        print("PASS: %d of %d program(s) need no Camel runtime; %d link it, all in the baseline"
              % (len(records) - len(actual), len(records), len(actual)))
        return 0
    if failed:
        print("FAIL: %d of %d program(s) are not standalone" % (len(failed), len(records)))
        return 1
    print("PASS: %d of %d program(s) need no Camel runtime" % (len(records), len(records)))
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
        # The line the compiler named, then the error: a build failure that names a place and
        # not the text at that place is a failure nobody can read, and the generated program is
        # temporary. The text comes first because it is the shorter of the two.
        text = ""
        at = re.search(re.escape(source) + r":(\d+):", errors[0] if errors else "")
        if at:
            lines = out.splitlines()
            number = int(at.group(1))
            if 0 < number <= len(lines):
                text = lines[number - 1].strip() + "  <<  "
        rec["detail"] = (text + (errors[0] if errors else (cout + cerr).strip()[-200:]))[:400]
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


BACKENDS = ("std::nvm", "std::fvm", "std::cpp", "std::jit")


def back_end_run(program, entry_args, args, env, workdir, back_end):
    """(exit code, text) of one program on one back end. For `std::cpp` that is the program it
    writes: translated, built, then run -- the same three steps `check` takes."""
    rel = os.path.relpath(program, ROOT).replace("\\", "/")
    if back_end != "std::cpp":
        code, out, err, _ = run([CAMEL] + entry_args + [rel] + args.passes + [back_end],
                                args.timeout, env)
        return code, (out + ("\n" + err if err.strip() else ""))
    code, out, err, _ = run([CAMEL] + entry_args + [rel] + args.passes + ["std::cpp"],
                            args.timeout, env)
    if code != 0 or "#include" not in out:
        return code, (out + err).strip()
    stem = re.sub(r"[^A-Za-z0-9_]", "_", rel)[:150]
    source = os.path.join(workdir, stem + ".cpp")
    target = os.path.join(workdir, stem + EXE)
    with open(source, "w", encoding="utf-8", newline="\n") as f:
        f.write(out)
    compiler, msvc = find_compiler()
    bridged = "camel/standalone/bridge.h" in out
    code, cout, cerr, _ = run(
        build_command(compiler, msvc, source, target, bridged, env["CAMEL_HOME"], args.pch),
        600, env, cwd=workdir)
    if code != 0:
        errors = [l for l in (cout + cerr).splitlines() if "error" in l]
        return code, (errors[0] if errors else (cout + cerr).strip())
    code, out, err, _ = run([target] + entry_args, args.timeout, env)
    return code, (out + ("\n" + err if err.strip() else ""))


def check_four(entry, workdir, args):
    """The four cells of one program, and whether they say the same thing."""
    program, entry_args = entry
    rel = os.path.relpath(program, ROOT).replace("\\", "/")
    env = dict(os.environ)
    env.setdefault("CAMEL_HOME", os.path.join(ROOT, "out", "latest"))
    if os.name == "nt":
        env["PATH"] = os.path.join(env["CAMEL_HOME"], "libs") + os.pathsep + env.get("PATH", "")
    cells = []
    for back_end in BACKENDS:
        code, text = back_end_run(program, entry_args, args, env, workdir, back_end)
        cells.append({"back_end": back_end, "exit": code, "text": text,
                      "not_supported": bool(re.search(r"[Nn]ot supported|unsupported|no support",
                                                      text))})
    rec = {"program": rel + "".join(" " + a for a in entry_args), "cells": cells}
    speaking = [c for c in cells if not c["not_supported"]]
    same = len({(c["exit"], c["text"]) for c in speaking}) == 1 if speaking else False
    rec["same"] = same
    rec["silent"] = [c["back_end"] for c in cells if c["not_supported"]]
    rec["differs"] = [] if same else [c for c in speaking
                                      if (c["exit"], c["text"]) != (speaking[0]["exit"],
                                                                    speaking[0]["text"])]
    return rec


def check_four_backends(programs, args):
    """Every program on the four back ends, one line per cell: a type is implemented when the
    four say the same sentence, not when one of them runs it."""
    entries = expand(programs)
    workdir = tempfile.mkdtemp(prefix="cppexec-")
    build_pch(workdir, args)
    try:
        records = []
        for entry in entries:
            rec = check_four(entry, workdir, args)
            records.append(rec)
            print("%s  %s" % (rec["program"],
                              "same" if rec["same"] else "differs"))
            for cell in rec["cells"]:
                text = " ".join(cell["text"].split())
                if len(text) > 200:
                    text = text[:200] + "..."
                print("  %-9s %s  %s" % (cell["back_end"], cell["exit"], text or "(no output)"))
            if rec["silent"]:
                print("  not run on %s: the back end says so above" % ", ".join(rec["silent"]))
            for cell in rec["differs"]:
                print("  differs at %s: %s" % (cell["back_end"],
                                               " ".join(cell["text"].split())[:300]))
    finally:
        shutil.rmtree(workdir, ignore_errors=True)
    agree = [r for r in records if r["same"]]
    print("")
    if len(agree) == len(records):
        print("PASS: %d of %d programs say the same thing on four back ends"
              % (len(agree), len(records)))
        return 0
    print("FAIL: %d of %d programs speak with more than one voice"
          % (len(records) - len(agree), len(records)))
    return 1


def first_line_difference(a, b):
    """The first line that differs, whole. `first_difference` shows a window around the first
    different column, which is right for a number and wrong for a fault: the sentence is the thing,
    and half of it is not a report."""
    la, lb = a.splitlines(), b.splitlines()
    for i in range(max(len(la), len(lb))):
        x = la[i] if i < len(la) else "<end>"
        y = lb[i] if i < len(lb) else "<end>"
        if x != y:
            return "line %d: fvm %r, cpp %r" % (i + 1, x, y)
    return "same lines, different line ends or trailing text"


def check_fault(entry, workdir, args):
    """One `fault_*` program: what std::fvm prints, and what the program std::cpp writes prints.
    Both texts are stdout and stderr together -- a fault is a report, and where a runtime sends it
    is not something this check may assume to keep it out of the comparison."""
    program, entry_args = entry
    rel = os.path.relpath(program, ROOT).replace("\\", "/")
    env = dict(os.environ)
    env.setdefault("CAMEL_HOME", os.path.join(ROOT, "out", "latest"))
    if os.name == "nt":
        env["PATH"] = os.path.join(env["CAMEL_HOME"], "libs") + os.pathsep + env.get("PATH", "")
    rec = {"program": rel + "".join(" " + a for a in entry_args)}

    code, out, err, _ = run([CAMEL] + entry_args + [rel] + args.passes + ["std::cpp"], args.timeout, env)
    text = out + "\n" + err
    if code != 0 or "#include" not in out:
        # A program the writer does not translate has no fault sentence of its own to compare:
        # the interpreter's is the only one that exists, and this is not where a refusal is judged.
        rec["outcome"] = "not translated"
        rec["detail"] = (text.strip().splitlines() or ["exit %s" % code])[-1][:160]
        return rec

    stem = re.sub(r"[^A-Za-z0-9_]", "_", rel)[:150]
    source = os.path.join(workdir, stem + ".cpp")
    target = os.path.join(workdir, stem + EXE)
    with open(source, "w", encoding="utf-8", newline="\n") as f:
        f.write(out)
    compiler, msvc = find_compiler()
    bridged = "camel/standalone/bridge.h" in out
    code, cout, cerr, _ = run(
        build_command(compiler, msvc, source, target, bridged, env["CAMEL_HOME"], args.pch),
        600, env, cwd=workdir)
    if code != 0:
        errors = [l for l in (cout + cerr).splitlines() if "error" in l]
        rec["outcome"] = "does not build"
        rec["detail"] = (errors[0] if errors else (cout + cerr).strip()[-200:])[:300]
        return rec

    fcode, fout, ferr, _ = run([CAMEL] + entry_args + [rel] + args.passes + ["std::fvm"], args.timeout, env)
    ccode, xout, xerr, _ = run([target] + entry_args, args.timeout, env)
    ftext, ctext = fout + ferr, xout + xerr
    rec["fvm_exit"], rec["cpp_exit"] = fcode, ccode
    if fcode == ccode and ftext == ctext:
        rec["outcome"] = "same"
        rec["standalone"] = not bridged
    else:
        rec["outcome"] = "differs"
        rec["detail"] = ("exit fvm %s, cpp %s; " % (fcode, ccode)) + (
            first_line_difference(ftext, ctext) if ftext != ctext else "same text")
    return rec


def check_faults(paths, args):
    """Every `fault_*` case under `paths`: the words of its fault have to be the interpreter's, as
    printed, not a sentence of a back end's own. A fault is part of the language's answer, and a
    back end may not improve on it. `--fault-baseline` names the ones known not to, in both
    directions: a fourth is red, and so is one of them starting to agree."""
    programs = []
    for root_dir in paths:
        if os.path.isfile(root_dir):
            programs.append(os.path.abspath(root_dir))
            continue
        for dirpath, _, names in os.walk(root_dir):
            for name in names:
                if name.startswith("fault_") and name.endswith(".cml"):
                    programs.append(os.path.join(dirpath, name))
    programs.sort()
    if not programs:
        print("FAIL: no fault_* case found under %s" % ", ".join(paths))
        return 1
    baseline = set(read_expect(args.fault_baseline)) if args.fault_baseline else set()
    workdir = tempfile.mkdtemp(prefix="cppexec-faults-")
    build_pch(workdir, args)
    try:
        with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
            records = list(pool.map(lambda p: check_fault((p, []), workdir, args), programs))
    finally:
        shutil.rmtree(workdir, ignore_errors=True)

    counts = collections.Counter(r["outcome"] for r in records)
    print("fault programs: %d   compiler: %s" % (len(records), find_compiler()[0]))
    for outcome, n in counts.most_common():
        print("%6d  %5.1f%%  %s" % (n, 100.0 * n / len(records), outcome))
    differing = [r for r in records if r["outcome"] in ("differs", "does not build")]
    for rec in differing:
        print("  %s: %s: %s" % (rec["program"], rec["outcome"], rec.get("detail", "")))
    silent = [r for r in records if r["outcome"] == "not translated"]
    for rec in silent:
        print("  %s: not translated by std::cpp: %s" % (rec["program"], rec.get("detail", "")))
    if args.fault_baseline:
        actual = {r["program"] for r in differing}
        extra = sorted(actual - baseline)
        missing = sorted(baseline - actual)
        for name in extra:
            print("  differs and is not in the baseline: " + name)
        for name in missing:
            print("  is in the baseline and now prints what std::fvm prints: " + name)
        if extra or missing:
            print("FAIL: the faults that differ are not the ones the baseline names (%d extra, "
                  "%d gone)" % (len(extra), len(missing)))
            return 1
        print("PASS: %d of %d fault program(s) print what std::fvm prints; %d differ, all in the "
              "baseline (%d not translated, not compared)"
              % (len(records) - len(actual), len(records), len(actual), len(silent)))
        return 0
    if differing:
        print("FAIL: %d of %d fault program(s) do not print what std::fvm prints"
              % (len(differing), len(records)))
        return 1
    print("PASS: %d of %d fault program(s) print what std::fvm prints (%d not translated, not "
          "compared)" % (len(records) - len(silent), len(records), len(silent)))
    return 0


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
    ap.add_argument("--baseline",
                    help="with --standalone: a file of the programs allowed to link the runtime; "
                         "the answer has to be exactly that list, in both directions")
    ap.add_argument("--four-backends", action="store_true",
                    help="run each program on std::nvm, std::fvm, std::cpp and std::jit, say what "
                         "each one said, and fail when they do not all say the same thing")
    ap.add_argument("--faults", nargs="?", const="test/cases", default=None, metavar="DIR",
                    help="every fault_* case under DIR (test/cases by default): the fault it prints "
                         "has to be what std::fvm prints, word for word")
    ap.add_argument("--fault-baseline",
                    help="with --faults: a file of the fault programs whose words are known to "
                         "differ, and why; the answer has to be exactly that list")
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
        if not args.programs and not args.expect:
            ap.error("--standalone needs at least one program, or --expect with a list")
        return check_standalone(args.programs, args)
    if args.four_backends:
        if not args.programs:
            ap.error("--four-backends needs at least one program")
        return check_four_backends(args.programs, args)
    if args.faults:
        return check_faults([args.faults], args)

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
