#!/usr/bin/env python3
"""For every program std::cpp translates: does the C++ compile, does the program run, and does
it print what std::fvm prints?

    python3 docs/design/cpp-memory/tools/cpp_coverage.py --json /tmp/cov.json test/cases benchmarks
    python3 docs/design/cpp-memory/tools/cpp_build_run.py /tmp/cov.json [--jobs 4] [--json out.json]

Reads the JSON of cpp_coverage.py, takes the files with outcome "translated", builds each with
cppbuild.sh and compares stdout and exit code of the executable with `camel <file> std::fvm`.
Programs whose output cannot be the same twice (they print a time) are listed as "differs";
look at the list.
"""
import argparse
import collections
import concurrent.futures
import json
import os
import re
import subprocess
import sys

CAMEL = os.environ.get("CAMEL", "out/latest/bin/camel")
HERE = os.path.dirname(os.path.abspath(__file__))
ANSI = re.compile(r"\x1b\[[0-9;]*m")


def run(cmd, timeout, env=None):
    try:
        p = subprocess.run(cmd, capture_output=True, timeout=timeout, env=env)
        return p.returncode, ANSI.sub("", p.stdout.decode("utf-8", "replace")), ANSI.sub("", p.stderr.decode("utf-8", "replace"))
    except subprocess.TimeoutExpired:
        return 124, "", "timeout"


def one(path, idx, outdir, timeout):
    env = dict(os.environ, CPPMEM_OUT=os.path.join(outdir, "p%04d" % idx))
    rc, out, err = run(["bash", os.path.join(HERE, "cppbuild.sh"), path], 600, env)
    rec = {"file": path}
    if rc != 0:
        errors = [l for l in err.splitlines() if "error:" in l]
        rec["outcome"] = "C++ does not compile" if rc == 3 else "std::cpp failed"
        rec["detail"] = re.sub(r"^.*error: ", "", errors[0])[:160] if errors else err.strip()[-160:]
        return rec
    exe = out.strip().splitlines()[-1]
    frc, fout, ferr = run([CAMEL, path, "std::fvm"], timeout)
    crc, cout, cerr = run([exe], timeout)
    rec["fvm_exit"], rec["cpp_exit"] = frc, crc
    if crc < 0 or crc in (134, 139):
        rec["outcome"] = "emitted program crashes"
        rec["detail"] = "signal/exit %d" % crc
    elif fout == cout and frc == crc:
        rec["outcome"] = "same stdout and exit code as std::fvm"
    elif fout == cout:
        rec["outcome"] = "same stdout, different exit code"
        rec["detail"] = "fvm %d, cpp %d; cpp stderr: %s" % (frc, crc, cerr.strip()[:100])
    else:
        rec["outcome"] = "stdout differs"
        fl, cl = fout.splitlines(), cout.splitlines()
        for i in range(max(len(fl), len(cl))):
            a = fl[i] if i < len(fl) else "<end>"
            b = cl[i] if i < len(cl) else "<end>"
            if a != b:
                rec["detail"] = "line %d: fvm %r, cpp %r" % (i + 1, a[:60], b[:60])
                break
    return rec


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("coverage_json")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--timeout", type=float, default=60)
    ap.add_argument("--out", default="/tmp/cppmem/all")
    ap.add_argument("--json")
    args = ap.parse_args()
    files = [r["file"] for r in json.load(open(args.coverage_json)) if r["outcome"] == "translated"]
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as ex:
        recs = list(ex.map(lambda t: one(t[1], t[0], args.out, args.timeout), enumerate(files)))
    if args.json:
        json.dump(recs, open(args.json, "w"), indent=1)
    print("translated programs: %d" % len(recs))
    for k, v in collections.Counter(r["outcome"] for r in recs).most_common():
        print("%6d  %5.1f%%  %s" % (v, 100.0 * v / max(1, len(recs)), k))
    print("\n== C++ compile errors by message")
    for k, v in collections.Counter(re.sub(r"'[^']*'", "'…'", r.get("detail", "")) for r in recs
                                    if r["outcome"] == "C++ does not compile").most_common(8):
        print("%6d  %s" % (v, k))
    print("\n== everything that is not 'same'")
    for r in recs:
        if not r["outcome"].startswith("same stdout and"):
            print("  %s: %s: %s" % (r["file"], r["outcome"], r.get("detail", "")))


if __name__ == "__main__":
    sys.exit(main())
