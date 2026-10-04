#!/usr/bin/env python3
"""Synthetic checks of classify.py (frozen rules): payload comparison by value and its hash
fallback, tolerances, the benchmark timing mask, the time limit, written files.

    python3 benchmarks/pipeline/selftest.py      (exit 1 when a check fails)
"""
import copy
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import classify as c

R = c.RULESETS["frozen"]


def rec(effects, output="x\n", exit_code=0, mask=""):
    return {"effects": effects, "output": output, "output_sha1": "", "exit_code": exit_code, "timed_out": False,
            "wall_ms": 10.0, "diagnostics": None, "passes": ["std::nvm"], "pipeline_id": "P1", "output_mask": mask}


def ev(uri, payload=None, sha="h", truncated=False):
    e = {"uri": uri, "stage": "run", "origin": None, "payload_sha1": sha}
    if payload is not None:
        e["payload"] = payload
    if truncated:
        e["payload_truncated"] = True
    return e


def outcome(ref, got):
    return c.classify_native(ref, got, R, 10.0)["outcome"]


checks = []


def check(name, got, want):
    checks.append((name, got, want, got == want))


a = rec([ev(":io/println", "string=hello|||", "1")])
check("identical text", outcome(a, copy.deepcopy(a)), "CONSISTENT")
check("string differs", outcome(a, rec([ev(":io/println", "string=hellp|||", "2")])), "EFFECT_MISMATCH")
f = rec([ev(":op/assn", "float=0.5|float=1.0000001|||", "1")])
check("float within 1e-5", outcome(f, rec([ev(":op/assn", "float=0.5|float=1.0000002|||", "2")])), "CONSISTENT")
check("float beyond 1e-5", outcome(f, rec([ev(":op/assn", "float=0.5|float=1.001|||", "2")])), "EFFECT_MISMATCH")
i = rec([ev(":op/assn", "int=3|||", "1")])
check("int differs", outcome(i, rec([ev(":op/assn", "int=4|||", "2")])), "EFFECT_MISMATCH")
g = rec([ev(":op/assn", "float=100000|||", "1")])
check("float printed like an int, within tol", outcome(g, rec([ev(":op/assn", "float=100000.5|||", "2")])), "CONSISTENT")
check("float printed like ints both sides, within tol", outcome(g, rec([ev(":op/assn", "float=100001|||", "2")])), "CONSISTENT")
check("int typed, off by one", outcome(rec([ev(":op/assn", "int=100000|||", "1")]),
                                        rec([ev(":op/assn", "int=100001|||", "2")])), "EFFECT_MISMATCH")
n = rec([ev(":op/assn", "float=nan|||", "1")])
check("nan equals nan", outcome(n, copy.deepcopy(n)), "CONSISTENT")
h = rec([ev(":op/assn", sha="1")])
check("hash only, equal", outcome(h, rec([ev(":op/assn", sha="1")])), "CONSISTENT")
check("hash only, differ", outcome(h, rec([ev(":op/assn", sha="2")])), "UNDECIDED")
check("text one side only, hashes differ", outcome(f, rec([ev(":op/assn", sha="2")])), "UNDECIDED")
t = rec([ev(":op/assn", "Tensor=[1.0, 2.0, 3.0", "1", truncated=True)])
check("truncated, equal hash", outcome(t, copy.deepcopy(t)), "CONSISTENT")
check("truncated, prefix differs by value",
      outcome(t, rec([ev(":op/assn", "Tensor=[1.0, 9.0, 3.0", "2", truncated=True)])), "EFFECT_MISMATCH")
check("truncated, prefix equal, hashes differ",
      outcome(t, rec([ev(":op/assn", "Tensor=[1.0, 2.0, 3.0", "2", truncated=True)])), "UNDECIDED")
check("sequence differs", outcome(a, rec([ev(":io/print", "string=hello|||", "1")])), "EFFECT_MISMATCH")
check("macro operator ignored", outcome(a, rec([ev("autodiff:grad", "x", "9"), ev(":io/println", "string=hello|||", "1")])),
      "CONSISTENT")
m1 = rec([ev(":io/println", "string=CAMEL_BENCH latency_ms=1.25 loss=0.5|||", "1")],
         output="CAMEL_BENCH latency_ms=1.25 loss=0.5\n", mask="camel_bench_ms")
m2 = rec([ev(":io/println", "string=CAMEL_BENCH latency_ms=7.5 loss=0.5|||", "2")],
         output="CAMEL_BENCH latency_ms=7.5 loss=0.5\n", mask="camel_bench_ms")
m3 = rec([ev(":io/println", "string=CAMEL_BENCH latency_ms=7.5 loss=0.6|||", "3")],
         output="CAMEL_BENCH latency_ms=7.5 loss=0.6\n", mask="camel_bench_ms")
check("masked timing", outcome(m1, m2), "CONSISTENT")
check("masked timing, loss differs", outcome(m1, m3), "VALUE_MISMATCH")
check("unmasked timing", outcome(rec(m1["effects"], m1["output"]), rec(m2["effects"], m2["output"])), "VALUE_MISMATCH")
check("output near", c.classify_native(rec([], "v=1.0\n"), rec([], "v=1.0005\n"), R, 10.0)["near"], True)
check("output int differs", outcome(rec([], "n=3\n"), rec([], "n=4\n")), "VALUE_MISMATCH")
late = rec([]); late["wall_ms"] = 20000.0
check("late finish", outcome(rec([]), late), "RESOURCE_LIMIT")
fa = dict(rec([]), files=[{"name": "o.npy", "bytes": 1, "sha1": "a", "npy": {"dtype": "<f4", "shape": [2], "count": 2, "values": [1.0, 2.0]}}])
fb = dict(rec([]), files=[{"name": "o.npy", "bytes": 1, "sha1": "b", "npy": {"dtype": "<f4", "shape": [2], "count": 2, "values": [1.0, 2.000001]}}])
fc = dict(rec([]), files=[{"name": "o.npy", "bytes": 1, "sha1": "c", "npy": {"dtype": "<f4", "shape": [2], "count": 2, "values": [1.0, 2.5]}}])
check("npy within tol", outcome(fa, fb), "CONSISTENT")
check("npy beyond tol", outcome(fa, fc), "VALUE_MISMATCH")
check("file missing", outcome(fa, dict(rec([]), files=[])), "EFFECT_MISMATCH")

bad = [x for x in checks if not x[3]]
for name, got, want, ok in checks:
    print(("ok  " if ok else "FAIL"), name, "->", got, "" if ok else f"(wanted {want})")
print(f"{len(checks) - len(bad)} / {len(checks)} checks passed")
sys.exit(1 if bad else 0)
