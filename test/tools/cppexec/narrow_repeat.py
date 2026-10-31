#!/usr/bin/env python3
# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""Twenty times the same command on the same input, and what each run wrote.

A writer whose output depends on the process it runs in makes every red test a matter of luck.
This asks the question directly: translate one program twenty times, then twenty-four more with
eight alive at a time, and say what each run wrote.
"""
import collections
import concurrent.futures
import hashlib
import os
import re
import subprocess
import sys

RUNS = 20
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
CAMEL = os.environ.get("CAMEL", os.path.join(ROOT, "out", "latest", "bin", "camel"))


def translate(program, passes):
    p = subprocess.run([CAMEL, program] + passes + ["std::cpp"], cwd=ROOT,
                       capture_output=True, text=True, timeout=300)
    return p.returncode, p.stdout + p.stderr


def shape(text):
    """What the loop's items are declared as. The defect is a *cell* there -- `Tuple` -- where the
    program wants an array, so the question is that one declaration, not the word `Tuple` anywhere
    in the program: a program of two values has a struct named Tuple as well."""
    m = re.search(r"const (\S+) (\w+_items) = rt::range\(", text)
    if m is None:
        return "no range loop"
    return "cell(%s)" % m.group(1) if m.group(1) == "Tuple" else "array(%s)" % m.group(1)


def main(program, passes, together):
    env = dict(os.environ)
    env.setdefault("CAMEL_HOME", os.path.join(ROOT, "out", "latest"))
    os.environ.update(env)
    command = [CAMEL, program] + passes + ["std::cpp"]
    print("program: %s" % program)
    print("command: %s" % " ".join(command))
    print("")
    keys = collections.Counter()
    for i in range(RUNS):
        code, text = translate(program, passes)
        h = hashlib.sha256(text.encode()).hexdigest()[:12]
        keys[(code, h, shape(text))] += 1
        print("  run %2d: exit %d  %s  %s  %d bytes"
              % (i + 1, code, h, shape(text), len(text)))
        if i == 0:
            line = re.search(r"const \S+ \w+_items = rt::range\(([^;]*);", text)
            print("  items: %s" % (("rt::range(" + line.group(1) + " ...")[:110]
                                   if line else "(no range loop)"))
    print("")
    for (code, h, s), n in keys.most_common():
        print("  %d of %d: exit %d, %s, %s" % (n, RUNS, code, h, s))
    if len(keys) > 1:
        print("FAIL: %d runs of one command wrote %d different programs"
              % (RUNS, len(keys)))
    else:
        print("PASS: %d runs of one command wrote one program (%s)"
              % (RUNS, list(keys)[0][2]))
    # The same command with other runs of it alive at the same time: a cache or a file the runs
    # share is the one thing that can make two of them disagree.
    at_once = collections.Counter()
    for round_ in range(3):
        with concurrent.futures.ThreadPoolExecutor(together) as pool:
            results = list(pool.map(lambda _: translate(program, passes), range(together)))
        said = []
        for code, text in results:
            h = hashlib.sha256(text.encode()).hexdigest()[:12]
            at_once[(code, h, shape(text))] += 1
            said.append("%s %s" % (shape(text), h))
        print("  %d at once (round %d): %s" % (together, round_ + 1, ", ".join(said)))
    print("")
    print("  %d runs, %d at a time: %s" % (together * 3, together,
                                           ", ".join("%d x %s (%s)" % (n, s, h)
                                                     for (_, h, s), n in at_once.most_common())))
    if len(at_once) > 1:
        print("FAIL: runs that overlap in time disagree")
        return 1
    return 1 if len(keys) > 1 else 0


if __name__ == "__main__":
    args = sys.argv[1:]
    passes = [a for a in args if a.startswith("std::")]
    width = 8
    for a in args:
        if a.startswith("--concurrent="):
            width = int(a.split("=", 1)[1])
    programs = [a for a in args if not a.startswith("std::") and not a.startswith("--")]
    if not programs:
        sys.exit("usage: narrow_repeat.py <program> [std::pass ...] [--concurrent=N]")
    sys.exit(main(programs[0], passes, width))
