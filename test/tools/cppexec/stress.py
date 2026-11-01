#!/usr/bin/env python3
# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""Run the whole-list standalone check many times, which is how a race it had was found.

    python3 test/tools/cppexec/stress.py                 # 8 rounds, 8 jobs
    python3 test/tools/cppexec/stress.py --rounds 24     # the run that is in the commit
    python3 test/tools/cppexec/stress.py --rounds 3 --jobs 2

The check itself (`run.py --standalone --expect=... --baseline=...`) builds every entry of the
expect list with one pool. A flake there is a fact about the pool, not about a program: it shows up
in a round or two and never when a single round is run by hand, so a single green round says
nothing. This tool asks the same question `rounds` times and stops at the first red, printing the
round it happened in and the lines that say why.

Not part of a plan gate: it is minutes long by nature. `test/tools/cppexec/run.py --check-names`
is the fast assertion that the naming it depends on still holds; this is the one that would notice
a race the naming does not explain.
"""

import argparse
import os
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
RUN = os.path.join(ROOT, "test", "tools", "cppexec", "run.py")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--rounds", type=int, default=8, help="how many times to ask (default 8)")
    ap.add_argument("--jobs", type=int, default=8, help="the pool width of one round (default 8)")
    ap.add_argument("--expect", default="test/tools/cppexec/expect.txt")
    ap.add_argument("--baseline", default="test/tools/cppexec/bridged.txt")
    args = ap.parse_args()
    if args.rounds < 1:
        ap.error("--rounds must be at least 1")

    env = dict(os.environ)
    env.setdefault("CAMEL_HOME", os.path.join(ROOT, "out", "latest"))
    red = None
    for round_number in range(1, args.rounds + 1):
        started = time.monotonic()
        done = subprocess.run(
            [sys.executable, RUN, "--standalone", "--jobs", str(args.jobs),
             "--expect=" + args.expect, "--baseline=" + args.baseline],
            cwd=ROOT, env=env, capture_output=True, text=True)
        seconds = time.monotonic() - started
        text = done.stdout + done.stderr
        verdict = "green" if done.returncode == 0 else "RED"
        print("round %d/%d: %s (%.1f s)" % (round_number, args.rounds, verdict, seconds), flush=True)
        if done.returncode != 0:
            red = (round_number, text)
            break
    if red is None:
        print("%d of %d rounds green: no red" % (args.rounds, args.rounds))
        return 0
    round_number, text = red
    print("\nround %d is red. What it said:" % round_number)
    for line in text.splitlines():
        if line.startswith("FAIL") or "does not build" in line or "error" in line:
            print("  " + line)
    return 1


if __name__ == "__main__":
    sys.exit(main())