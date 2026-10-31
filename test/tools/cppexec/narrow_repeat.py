# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""The same command on the same input, many times, and what every run wrote.

A writer whose output depends on anything but the program turns every later red test into a
matter of luck, so the question is asked directly instead of assumed. Two modes:

    python3 test/tools/cppexec/narrow_repeat.py <program.cml> [std::pass ...]
        twenty runs of one program, then twenty-four more with eight alive at a time

    python3 test/tools/cppexec/narrow_repeat.py --expect test/tools/cppexec/expect.txt [--runs 5]
        every program of the expect list, `--runs` times each: no program may be written two ways

The second mode is the guard the first one grew into: the defect this file was written for wrote
one line two ways when the program it belonged to held a value of two ints, and a check that only
ever looks at that one program would not see its next shape.
"""
import collections
import concurrent.futures
import hashlib
import os
import re
import subprocess
import sys
import time

RUNS = 20
AT_ONCE = 8
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
CAMEL = os.environ.get("CAMEL", os.path.join(ROOT, "out", "latest", "bin", "camel"))

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run as cppexec  # noqa: E402  (the expect list and the arguments of a main are its business)


def translate(program, passes, entry_args=()):
    """(exit code, what the run wrote) of one translation. The arguments of the program's `main`
    come before the path, as they do in run.py."""
    rel = os.path.relpath(program, ROOT).replace("\\", "/")
    p = subprocess.run([CAMEL] + list(entry_args) + [rel] + passes + ["std::cpp"], cwd=ROOT,
                       capture_output=True, text=True, timeout=300)
    return p.returncode, p.stdout + p.stderr


def shape(text):
    """What a loop's items are declared as -- the defect this file was written for is a *cell*
    there where the program wants an array. Empty when the program has no interval loop."""
    m = re.search(r"const (\S+) (\w+_items) = rt::range\(", text)
    if m is None:
        return ""
    return "cell(%s)" % m.group(1) if m.group(1) == "Tuple" else "array(%s)" % m.group(1)


def digest(exit_code, text):
    return hashlib.sha256(("%d\n%s" % (exit_code, text)).encode()).hexdigest()[:12]


def same_command(program, passes, together):
    """The first mode: one program, many runs, then many at a time."""
    env = dict(os.environ)
    env.setdefault("CAMEL_HOME", os.path.join(ROOT, "out", "latest"))
    os.environ.update(env)
    print("program: %s" % program)
    print("command: %s %s %s std::cpp" % (CAMEL, program, " ".join(passes)))
    print("")
    keys = collections.Counter()
    for i in range(RUNS):
        code, text = translate(program, passes)
        h = digest(code, text)
        keys[(code, h, shape(text))] += 1
        print("  run %2d: exit %d  %s  %s  %d bytes"
              % (i + 1, code, h, shape(text) or "-", len(text)))
        if i == 0:
            line = re.search(r"const \S+ \w+_items = rt::range\(([^;]*);", text)
            print("  items: %s" % (("rt::range(" + line.group(1) + " ...")[:110]
                                   if line else "(no range loop)"))
    print("")
    for (code, h, s), n in keys.most_common():
        print("  %d of %d: exit %d, %s, %s" % (n, RUNS, code, h, s or "-"))
    if len(keys) > 1:
        print("FAIL: %d runs of one command wrote %d different programs"
              % (RUNS, len(keys)))
    else:
        print("PASS: %d runs of one command wrote one program (%s)"
              % (RUNS, list(keys)[0][2] or "-"))
    # The same command with other runs of it alive at the same time: a cache or a file the runs
    # share is the one thing that can make two of them disagree.
    at_once = collections.Counter()
    for round_ in range(3):
        with concurrent.futures.ThreadPoolExecutor(together) as pool:
            results = list(pool.map(lambda _: translate(program, passes), range(together)))
        said = []
        for code, text in results:
            h = digest(code, text)
            at_once[(code, h, shape(text))] += 1
            said.append("%s %s" % (shape(text) or "-", h))
        print("  %d at once (round %d): %s" % (together, round_ + 1, ", ".join(said)))
    print("")
    print("  %d runs, %d at a time: %s" % (together * 3, together,
                                           ", ".join("%d x %s (%s)" % (n, s or "-", h)
                                                     for (_, h, s), n in at_once.most_common())))
    if len(at_once) > 1:
        print("FAIL: runs that overlap in time disagree")
        return 1
    return 1 if len(keys) > 1 else 0


def every_program(expect, runs, together, verbose):
    """The second mode: every program of an expect list, `runs` times each."""
    started = time.time()
    env = dict(os.environ)
    env.setdefault("CAMEL_HOME", os.path.join(ROOT, "out", "latest"))
    os.environ.update(env)
    entries = cppexec.expand(cppexec.read_expect(expect))
    print("programs: %d   runs of each: %d   at once: %d" % (len(entries), runs, together))

    def one(entry):
        program, entry_args = entry
        said = collections.Counter()
        for _ in range(runs):
            code, text = translate(program, [], entry_args)
            said[(code, digest(code, text), shape(text))] += 1
        return entry, said

    records = []
    with concurrent.futures.ThreadPoolExecutor(together) as pool:
        for entry, said in pool.map(one, entries):
            records.append((entry, said))
    seconds = time.time() - started

    differing = []
    for (program, entry_args), said in records:
        rel = os.path.relpath(program, ROOT).replace("\\", "/") + "".join(" " + a for a in entry_args)
        if len(said) == 1:
            (_, h, s), _ = list(said.items())[0]
            if verbose:
                print("  %-58s %d of %d  one program (%s%s)"
                      % (rel, runs, runs, h, (", " + s) if s else ""))
        else:
            differing.append((rel, said))
            print("  %s: %d runs wrote %d programs" % (rel, runs, len(said)))
            for (code, h, s), n in said.most_common():
                print("    %d of %d: exit %d, %s%s" % (n, runs, code, h,
                                                       (", " + s) if s else ""))
    print("")
    print("  %d programs, %d runs each, %.1f s" % (len(records), runs, seconds))
    if differing:
        print("FAIL: %d of %d programs were written more than one way"
              % (len(differing), len(records)))
        for rel, said in differing:
            print("  %s: %s" % (rel, ", ".join(h for (_, h, _) in said)))
        return 1
    print("PASS: %d of %d programs: %d runs of one command wrote one program"
          % (len(records), len(records), runs))
    return 0


def main(argv):
    expect = None
    runs = 5
    verbose = False
    together = AT_ONCE
    rest = []
    for a in argv:
        if a == "--verbose":
            verbose = True
        elif a.startswith("--expect="):
            expect = a.split("=", 1)[1]
        elif a.startswith("--runs="):
            runs = int(a.split("=", 1)[1])
        elif a.startswith("--concurrent="):
            together = int(a.split("=", 1)[1])
        else:
            rest.append(a)
    if expect is not None:
        return every_program(expect, runs, together, verbose)
    passes = [a for a in rest if a.startswith("std::")]
    programs = [a for a in rest if not a.startswith("std::")]
    if not programs:
        sys.exit("usage: narrow_repeat.py <program> [std::pass ...] [--concurrent=N] "
                 "| --expect=<list> [--runs=N] [--verbose]")
    return same_command(programs[0], passes, together)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
