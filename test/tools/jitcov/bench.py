#!/usr/bin/env python3
"""Interleaved wall-clock comparison of camel command lines.

    bench.py [--runs 7] [--cpu 2] [--env K=V ...] LABEL=COMMAND [LABEL=COMMAND ...]

Each COMMAND is a shell command line. The variants are run in turn, RUNS rounds, each pinned to
one core with taskset when --cpu is given; the minimum and the median of every variant are
reported, with /proc/loadavg before and after. A variant whose exit code differs
between rounds is flagged. Numbers describe that the system is usable; they are not a claim of
a performance advantage (design principles 2.9).
"""
import argparse
import os
import statistics
import subprocess
import sys
import time


def loadavg():
    try:
        with open('/proc/loadavg') as f:
            return f.read().strip()
    except OSError:
        return 'n/a'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--runs', type=int, default=7)
    ap.add_argument('--cpu', type=int, default=None)
    ap.add_argument('--env', action='append', default=[])
    ap.add_argument('--timeout', type=float, default=300)
    ap.add_argument('variants', nargs='+')
    args = ap.parse_args()

    env = dict(os.environ)
    for kv in args.env:
        k, _, v = kv.partition('=')
        env[k] = v
    variants = []
    for spec in args.variants:
        label, _, cmd = spec.partition('=')
        if args.cpu is not None:
            cmd = f'taskset -c {args.cpu} {cmd}'
        variants.append((label, cmd))

    times = {label: [] for label, _ in variants}
    seen = {}
    flagged = set()
    print(f'loadavg before: {loadavg()}')
    for _ in range(args.runs):
        for label, cmd in variants:
            t0 = time.perf_counter()
            try:
                p = subprocess.run(cmd, shell=True, env=env, capture_output=True,
                                   timeout=args.timeout)
                key = (p.returncode, b"")
            except subprocess.TimeoutExpired:
                key = ('timeout', b'')
            dt = time.perf_counter() - t0
            times[label].append(dt)
            if seen.setdefault(label, key) != key:
                flagged.add(label)
    print(f'loadavg after:  {loadavg()}')
    width = max(len(label) for label, _ in variants)
    print(f'{"variant":<{width}}  {"min ms":>9}  {"median ms":>9}  exit')
    for label, _ in variants:
        ts = times[label]
        rc = seen[label][0]
        mark = '  (exit code varied between runs)' if label in flagged else ''
        print(f'{label:<{width}}  {min(ts) * 1e3:9.1f}  {statistics.median(ts) * 1e3:9.1f}  {rc}{mark}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
