#!/usr/bin/env python3
"""Coverage of std::cpp over the programs of the repository.

For every .cml under the given directories, runs `camel <file> std::cpp` and records what came
out: a translation unit, a refusal (with its reason), a compile-time diagnostic of the program
itself, or a failure of the pass.

    python3 docs/design/cpp-memory/tools/cpp_coverage.py test/cases benchmarks examples exemplars
    python3 docs/design/cpp-memory/tools/cpp_coverage.py --passes "std::inline" test/cases
    python3 docs/design/cpp-memory/tools/cpp_coverage.py --json /tmp/cov.json --list-ok test/cases
    python3 docs/design/cpp-memory/tools/cpp_coverage.py --from-json /tmp/cov.json

Run from the repository root with `camel` built (out/latest/bin/camel, or CAMEL=...).
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
ANSI = re.compile(r"\x1b\[[0-9;]*m")

# First match wins. The reasons are the texts of src/passes/trans/cpp/runtime_emitter.cpp.
REASONS = [
    ("main takes parameters", r"'main' takes parameters"),
    ("no main", r"requires a 'main' graph"),
    ("computed top-level value", r"only literal top-level values"),
    ("closure (callee captures)", r"which captures variables"),
    ("call of a function value (CALL)", r"contains a call of a function value"),
    ("construction of struct/tuple/array/closure (FILL)", r"contains the construction of"),
    ("element or field access (ACCS)", r"contains an access to an element or a field"),
    ("sync node (SYNC)", r"contains a sync block"),
    ("bound function (BIND)", r"a function bound to its 'with' arguments"),
    ("reference read (DREF)", r"a read through a reference"),
    ("return type", r"returns a value of type '"),
    ("parameter type", r"parameter of .* has type '"),
    ("constant type", r"uses a constant of type '"),
    ("operator result type", r"the result of operator '"),
    ("operator argument type", r"an argument of operator '"),
    ("callee return type", r"calls .*, which returns a value of type '"),
    ("cast/copy/gate type", r"a value of type '.*' is (converted|copied|passed on)"),
]

TYPE_IN_REASON = re.compile(r"type '([^']*)'")


def type_class(t):
    """A coarse class for the type named in a refusal."""
    if t is None:
        return None
    s = t.strip()
    if s.endswith("?"):
        return "T? (nullable)"
    if s.endswith("!"):
        return "T! (fallible)"
    if s.endswith("[]") or re.search(r"\[\d*\]$", s):
        return "array"
    if "=>" in s:
        return "function"
    if s.startswith("("):
        return "tuple"
    if s.startswith("{"):
        return "struct"
    low = s.lower()
    if low == "void":
        return "void (the empty arm of an if without else)"
    if low.startswith("pyobject"):
        return "foreign object (PyObject)"
    if low in ("int", "float", "bool", "string", "int32", "int64", "float32", "float64"):
        return None
    if low.startswith("tensor"):
        return "tensor"
    if low.startswith("map<") or low.startswith("set<") or low.startswith("table"):
        return "map/set"
    if low == "any":
        return "any"
    if low.startswith("ref") or low.startswith("var"):
        return "var cell"
    return "other (" + s[:24] + ")"


def annotate(rec):
    """Class of the reason and of the type it names (recomputed when a JSON is re-read)."""
    reason = rec.get("reason", "")
    rec["class"] = "other refusal"
    for name, pat in REASONS:
        if re.search(pat, reason):
            rec["class"] = name
            break
    tm = TYPE_IN_REASON.search(reason)
    rec["type"] = tm.group(1) if tm else None
    rec["type_class"] = type_class(rec["type"])


def classify(path, passes, timeout):
    cmd = [CAMEL, path] + passes + ["std::cpp"]
    try:
        p = subprocess.run(cmd, capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return {"file": path, "outcome": "timeout"}
    out = ANSI.sub("", p.stdout.decode("utf-8", "replace"))
    err = ANSI.sub("", p.stderr.decode("utf-8", "replace"))
    text = out + "\n" + err
    rec = {"file": path, "exit": p.returncode}
    if p.returncode < 0:
        rec["outcome"] = "crash (signal %d)" % -p.returncode
        return rec
    if p.returncode == 0 and "#include" in out and "namespace generated" in out:
        rec["outcome"] = "translated"
        rec["bridge_ops"] = sorted(set(re.findall(r'bridge\(\)\.bind\("([^"]+)"\)', out)))
        rec["uses_string"] = "String *" in out
        rec["functions"] = len(re.findall(r"^static inline [^;]*\{$", out, re.M))
        return rec
    if "TranslationRefused" in text or "refused the graph" in text:
        m = re.search(r"refused the graph: (.*)", text)
        reason = m.group(1).strip() if m else text.strip().splitlines()[-1]
        rec["outcome"] = "refused"
        rec["reason"] = reason
        annotate(rec)
        return rec
    if "requires a 'main' graph" in text:
        rec["outcome"] = "no main"
        return rec
    if "PassInternalError" in text or "internal error" in text.lower():
        rec["outcome"] = "pass internal error"
        m = re.search(r"internal error: (.*)", text)
        rec["reason"] = re.sub(r"node#\d+", "node#N", m.group(1).strip()[:200]) if m else ""
        return rec
    m = re.search(r"(\w+Diag::\w+)", text)
    rec["outcome"] = "does not compile (program's own diagnostic)"
    rec["reason"] = m.group(1) if m else (text.strip().splitlines()[0][:200] if text.strip() else "")
    return rec


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dirs", nargs="*")
    ap.add_argument("--from-json", help="print the tables of an earlier run instead of running")
    ap.add_argument("--passes", default="", help="passes to run before std::cpp")
    ap.add_argument("--timeout", type=float, default=60)
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--json")
    ap.add_argument("--list-ok", action="store_true", help="print the translated files")
    args = ap.parse_args()

    files = []
    for d in args.dirs:
        if os.path.isfile(d):
            files.append(d)
            continue
        for root, _, names in os.walk(d):
            for n in sorted(names):
                if n.endswith(".cml"):
                    files.append(os.path.join(root, n))
    files.sort()
    passes = args.passes.split()

    if args.from_json:
        with open(args.from_json) as fh:
            recs = json.load(fh)
        for r in recs:
            if r["outcome"] == "refused":
                annotate(r)
    else:
        with concurrent.futures.ThreadPoolExecutor(args.jobs) as ex:
            recs = list(ex.map(lambda f: classify(f, passes, args.timeout), files))

    if args.json:
        with open(args.json, "w") as fh:
            json.dump(recs, fh, indent=1)

    total = len(recs)
    by_outcome = collections.Counter(r["outcome"] for r in recs)
    print("programs: %d   passes before std::cpp: %s" % (total, " ".join(passes) or "(none)"))
    print("\n== outcome")
    for k, v in by_outcome.most_common():
        print("%6d  %5.1f%%  %s" % (v, 100.0 * v / total, k))

    reached = [r for r in recs if r["outcome"] in ("translated", "refused", "pass internal error")
               or r["outcome"].startswith("crash")]
    ok = [r for r in recs if r["outcome"] == "translated"]
    print("\nprograms that reach the emitter (compile and have a main): %d; translated: %d (%.1f%%)"
          % (len(reached), len(ok), 100.0 * len(ok) / max(1, len(reached))))

    refused = [r for r in recs if r["outcome"] == "refused"]
    print("\n== first reason of a refusal (%d)" % len(refused))
    for k, v in collections.Counter(r["class"] for r in refused).most_common():
        print("%6d  %5.1f%%  %s" % (v, 100.0 * v / max(1, len(refused)), k))
    print("\n== type named in the refusal")
    for k, v in collections.Counter(r["type_class"] for r in refused if r.get("type_class")).most_common():
        print("%6d  %s" % (v, k))

    print("\n== by top directory: translated / reaching the emitter / all")
    groups = collections.defaultdict(lambda: [0, 0, 0])
    for r in recs:
        parts = r["file"].split(os.sep)
        key = os.sep.join(parts[:3]) if parts[0] == "test" else parts[0]
        g = groups[key]
        g[2] += 1
        if r in reached:
            g[1] += 1
        if r["outcome"] == "translated":
            g[0] += 1
    for k in sorted(groups):
        print("%5d / %5d / %5d  %s" % (*groups[k], k))

    if ok:
        with_ops = sum(1 for r in ok if r["bridge_ops"])
        with_str = sum(1 for r in ok if r["uses_string"])
        ops = collections.Counter(o for r in ok for o in r["bridge_ops"])
        print("\n== translated programs: %d call operators through the bridge, %d hold a String *"
              % (with_ops, with_str))
        print("most used operators:", ", ".join("%s (%d)" % kv for kv in ops.most_common(15)))
    other = [r for r in recs if r["outcome"] == "pass internal error" or r["outcome"].startswith("crash")
             or r["outcome"] == "timeout"]
    if other:
        print("\n== failures of the pass itself")
        for k, v in collections.Counter(
                r["outcome"] + ": " + re.sub(r"'[^']*'", "'...'", r.get("reason", "")) for r in other).most_common():
            print("%6d  %s" % (v, k))
        for r in other[:12]:
            print("  %s" % r["file"])
    if args.list_ok:
        print("\n== translated")
        for r in ok:
            print(r["file"])


if __name__ == "__main__":
    sys.exit(main())
