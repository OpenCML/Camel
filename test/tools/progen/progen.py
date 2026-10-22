#!/usr/bin/env python3
# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""progen: random Camel programs with a reference output, and differential runs over pipelines.

    progen.py show      --seed N [--features LIST]            print the program and its reference
    progen.py selfcheck --seeds A-B [--features LIST] [--jobs J] [--out DIR]
    progen.py diff      --seeds A-B [--matrix full|cross|quick|frozen] [--backends LIST]
                        [--features LIST] [--jobs J] --out DIR
    progen.py reduce    --seed N --cell ID [--matrix M] [--features LIST] --out DIR
    progen.py check                                           the fixed batch of the test plan
    progen.py findings [--runs N]                             re-run the archived findings
    progen.py fixed                                           the fixed findings, on every scheduler

`selfcheck` runs every program once, without passes (the node VM), and reports how often the
reference and the run agree: it tests the generator and its model of the semantics.
`diff` runs every program on every cell of the matrix and classifies each cell (runner.py);
it writes results.jsonl, summary.txt and the sources of the programs with a bad cell.
`reduce` shrinks one failing (program, cell) pair (reduce.py).

See README.md for what is generated, what is not, and why the reference is valid.
"""

from __future__ import annotations

import argparse
import collections
import concurrent.futures
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from generate import DEFAULT_FEATURES, FEATURES, generate, parse_features  # noqa: E402
from model import emit, emit_modules  # noqa: E402
from reduce import Reducer  # noqa: E402
from runner import BACKENDS, BAD, matrix, run_program, signature  # noqa: E402


def seeds_of(text: str):
    out = []
    for part in text.split(","):
        if "-" in part:
            a, b = part.split("-")
            out += list(range(int(a), int(b) + 1))
        else:
            out.append(int(part))
    return out


def one(seed: int, features, cells, out, keep_all=False, size=1):
    prog, expected, attempts = generate(seed, features, size=size)
    source = emit(prog)
    name = f"progen_{seed}"
    work = None if out is None else Path(out) / "work" / name
    res = run_program(source, prog.a["env"], expected, cells, work, name, modules=emit_modules(prog))
    bad = {cid: info for cid, info in res.items() if info["outcome"] in BAD}
    for n, (cid, info) in enumerate(bad.items()):       # is it the same on two more runs?
        if info["outcome"] in ("CRASH", "HANG") and n >= 2:
            continue                                    # crashes are slow to collect: two cells are enough
        again = [run_program(source, prog.a["env"], expected, {cid: cells[cid]}, work, name,
                             modules=emit_modules(prog))[cid] for _ in range(2)]
        info["repeat"] = [signature(a) for a in again]
        info["stable"] = all(signature(a) == signature(info) and a.get("got") == info.get("got") for a in again)
    refused = {cid: info for cid, info in res.items() if info["outcome"] == "REFUSED"}
    record = {"seed": seed, "attempts": attempts, "lines": len(source.splitlines()), "expected_lines": len(expected),
              "outcomes": {cid: info["outcome"] for cid, info in res.items()},
              "bad": bad, "refused": {cid: {k: info[k] for k in ("diag", "detail")} for cid, info in refused.items()}}
    if out is not None and (bad or keep_all):
        d = Path(out) / "failures"
        d.mkdir(parents=True, exist_ok=True)
        (d / f"{name}.cml").write_text(source, encoding="utf-8", newline="\n")
        for stem, text in emit_modules(prog).items():
            (d / f"{stem}.cml").write_text(text, encoding="utf-8", newline="\n")
        (d / f"{name}.json").write_text(json.dumps({"seed": seed, "features": sorted(features), "size": size,
                                                    "env": prog.a["env"],
                                                    "expected": expected, "bad": bad}, indent=1), encoding="utf-8")
    return record


def run_batch(seeds, features, cells, out, jobs, resume=False, size=1):
    records = []
    started = time.time()
    partial = None
    if out is not None:                  # records as they finish, in case the batch is stopped
        Path(out).mkdir(parents=True, exist_ok=True)
        path = Path(out) / "results.partial.jsonl"
        if resume and path.exists():
            records = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]
            done = {r["seed"] for r in records}
            seeds = [s for s in seeds if s not in done]
            print(f"  resuming: {len(records)} programs already recorded, {len(seeds)} to run", file=sys.stderr, flush=True)
        partial = open(path, "a" if resume else "w", encoding="utf-8")
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = {pool.submit(one, s, features, cells, out, False, size): s for s in seeds}
        for n, fut in enumerate(concurrent.futures.as_completed(futures), 1):
            records.append(fut.result())
            if partial:
                partial.write(json.dumps(records[-1]) + chr(10))
                partial.flush()
            if n % 100 == 0:
                print(f"  {n}/{len(seeds)} programs, {time.time() - started:.0f} s", file=sys.stderr, flush=True)
    if partial:
        partial.close()
    records.sort(key=lambda r: r["seed"])
    return records


def summarize(records, cells, features, title):
    counts = collections.Counter()
    per_cell = collections.defaultdict(collections.Counter)
    signatures = collections.defaultdict(list)
    for r in records:
        for cid, o in r["outcomes"].items():
            counts[o] += 1
            per_cell[cid][o] += 1
        for cid, info in r["bad"].items():
            signatures[signature(info)].append((r["seed"], cid))
    total = sum(counts.values())
    lines = [title, f"features: {', '.join(sorted(features))}",
             f"programs: {len(records)}   cells per program: {len(cells)}   cells: {total}",
             f"rejected attempts before a usable program: {sum(r['attempts'] - 1 for r in records)}",
             f"source lines per program: mean {sum(r['lines'] for r in records) / max(1, len(records)):.0f}", "",
             "outcome              cells"]
    for o, n in counts.most_common():
        lines.append(f"  {o:<18} {n}")
    bad_programs = [r["seed"] for r in records if r["bad"]]
    lines += ["", f"programs with a bad cell: {len(bad_programs)}"]
    if signatures:
        lines += ["", "bad cells by signature (seed:cell, first 12)"]
        for sig, where in sorted(signatures.items(), key=lambda kv: -len(kv[1])):
            lines.append(f"  {sig}: {len(where)} cells in {len({s for s, _ in where})} programs")
            lines.append("      " + " ".join(f"{s}:{c}" for s, c in where[:12]))
        lines += ["", "bad cells per cell id"]
        for cid in cells:
            bad = {o: n for o, n in per_cell[cid].items() if o in BAD}
            if bad:
                lines.append(f"  {cid:<28} " + ", ".join(f"{o} {n}" for o, n in sorted(bad.items())))
    refused = collections.Counter()
    for r in records:
        for cid, info in r["refused"].items():
            refused[info["diag"]] += 1
    if refused:
        lines += ["", "located refusals (legal) by diagnostic"] + [f"  {d}: {n}" for d, n in refused.most_common()]
    return "\n".join(lines) + "\n"


def cmd_show(args):
    prog, expected, attempts = generate(args.seed, parse_features(args.features), size=args.size)
    print(emit(prog))
    for stem, text in emit_modules(prog).items():
        print(f"// ---- {stem}.cml ----")
        print(text)
    print("// environment:", prog.a["env"])
    print(f"// reference output ({attempts} attempt(s)):")
    for line in expected:
        print("// " + line)
    return 0


def cmd_selfcheck(args):
    features = parse_features(args.features)
    cells = matrix("reference")
    records = run_batch(seeds_of(args.seeds), features, cells, args.out, args.jobs, size=args.size)
    text = summarize(records, cells, features, "progen selfcheck: reference against the run without passes (node VM)"
                     + (f", size {args.size}" if args.size != 1 else ""))
    agree = sum(1 for r in records if r["outcomes"]["default"] == "OK")
    text += f"\nagreement: {agree} of {len(records)} programs ({100.0 * agree / max(1, len(records)):.2f}%)\n"
    print(text)
    if args.out:
        (Path(args.out) / "summary.txt").write_text(text, encoding="utf-8")
    return 0 if agree == len(records) else 1


def cmd_diff(args):
    features = parse_features(args.features)
    backends = [b if "::" in b else "std::" + b for b in args.backends.split(",")] if args.backends else None
    cells = matrix(args.matrix, backends)
    if args.cells:
        cells = {k: v for k, v in cells.items() if k in args.cells.split(",")}
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    records = run_batch(seeds_of(args.seeds), features, cells, out, args.jobs, args.resume, size=args.size)
    with open(out / "results.jsonl", "w", encoding="utf-8") as f:
        for r in records:
            f.write(json.dumps(r) + "\n")
    text = summarize(records, cells, features, f"progen diff: matrix {args.matrix}, seeds {args.seeds}"
                     + (f", size {args.size}" if args.size != 1 else ""))
    (out / "summary.txt").write_text(text, encoding="utf-8")
    print(text)
    return 0 if not any(r["bad"] for r in records) else 1


def cmd_reduce(args):
    features = parse_features(args.features)
    size = args.size
    workdir = None
    if args.from_dir:
        case = json.loads((Path(args.from_dir) / "failures" / f"progen_{args.seed}.json").read_text(encoding="utf-8"))
        features = frozenset(case["features"])
        size = case.get("size", size)
        workdir = Path(args.from_dir) / "work" / f"progen_{args.seed}"
    prog, expected, _ = generate(args.seed, features, size=size)
    cells = matrix(args.matrix)
    passes = args.passes.split() if args.passes is not None else cells[args.cell]
    target = None
    for _ in range(args.repeats):
        res = run_program(emit(prog), prog.a["env"], expected, {"fail": passes, "ref": []}, workdir, prog.a["module"],
                          modules=emit_modules(prog))
        if res["fail"]["outcome"] in BAD:
            target = signature(res["fail"])
            break
    print(f"seed {args.seed}, pipeline [{' '.join(passes)}]: {signature(res['fail'])}; run without passes: {res['ref']['outcome']}")
    if target is None:
        print("nothing to reduce (the failure did not reproduce)")
        return 1
    reducer = Reducer(prog, passes, target, reference_ok=res["ref"]["outcome"] == "OK", log=print,
                      max_tests=args.max_tests, workdir=workdir, repeats=args.repeats)
    small, small_passes = reducer.run()
    write_case(Path(args.out), args.name or f"seed{args.seed}", small, small_passes, args.seed, features, reducer.last)
    return 0


def write_case(out: Path, name: str, prog, passes, seed, features, last, runs: int = 10):
    """Writes <name>.cml and <name>.json. The program is renamed to `name` and run `runs` times
    where it is written: a failure that depends on the path or does not happen on every run
    shows as fewer than `runs` reproductions (`reproduced`)."""
    from model import evaluate
    out.mkdir(parents=True, exist_ok=True)
    prog.a["module"] = name
    expected = evaluate(prog)
    source = emit(prog)
    seen = collections.Counter()
    shown = None
    for _ in range(runs):
        res = run_program(source, prog.a["env"], expected, {"fail": passes}, out, name,
                          modules=emit_modules(prog))["fail"]
        seen[res["outcome"]] += 1
        if res["outcome"] in BAD and shown is None:
            shown = res
    shown = shown or last or {}
    (out / f"{name}.json").write_text(json.dumps({
        "pipeline": passes, "env": prog.a["env"], "expected": expected, "outcome": shown.get("outcome", ""),
        "diagnostic": shown.get("diag", ""), "detail": shown.get("detail", ""), "actual": shown.get("got", []),
        "reproduced": dict(seen), "origin": {"seed": seed, "features": sorted(features)}}, indent=1) + "\n",
        encoding="utf-8", newline="\n")
    print(f"wrote {out / (name + '.cml')} ({len(source.splitlines())} lines), pipeline [{' '.join(passes)}]")
    print(f"expected: {expected}")
    print(f"actual ({shown.get('outcome')}): {shown.get('got')} {shown.get('detail', '')}")
    print(f"outcomes of {runs} runs of the written file: {dict(seen)}")


# The fixed batch of the test plan (test/plans/feat/opt/progen.plan.toml): the first seeds whose
# every cell of the `cross` matrix passes today, all features on. It is a regression guard, so a
# seed with a known bad cell is left out, and listed here with the finding it shows
# (test/cases/opt/progen/README.md); `progen.py findings` re-runs the findings themselves.
CHECK_RANGE = range(1, 26)
# With generate.VERSION 3 (the features comprehension, nullable, partial and ifnoelse) every
# seed of the range passes; the seed left out under VERSION 1 (5, finding F18) is another
# program now.
# Seed 16 (handover H15, literals take their type from the context): `e23` declares `float` and
# one arm of a `match` expression in it ends in `return 5 - 10`. That `return` is the value of
# its arm, and the arm's position (the `var v32 = match …` it is written in) expects no type, so
# the value is an `int`, as the other arms of the match are and as the generator's reference
# evaluator types it: the arm join holds. It is back in the batch since handover H24 (a `return`
# in an arm no longer takes the function's declared return type). See
# test/cases/opt/progen/README.md, J5.
CHECK_EXCLUDED = {}
CHECK_MATRIX = "cross"


def cmd_check(args):
    features = parse_features(None)
    cells = matrix(CHECK_MATRIX)
    seeds = [s for s in CHECK_RANGE if s not in CHECK_EXCLUDED]
    records = run_batch(seeds, features, cells, None, args.jobs)
    bad = [(r["seed"], cid, info["outcome"], info.get("detail", "")) for r in records for cid, info in r["bad"].items()]
    for seed, cid, outcome, detail in bad:
        print(f"seed {seed} [{cid}]: {outcome}: {detail}")
    n = sum(len(r["outcomes"]) for r in records)
    refused = sum(len(r["refused"]) for r in records)
    print(f"progen check: {len(records)} programs x {len(cells)} pipelines = {n} cells "
          f"({refused} located refusals), {len(CHECK_EXCLUDED)} seed(s) left out as known failures")
    print(f"{len(bad)} wrong")
    print("PASS" if not bad and n > 0 else "FAIL")
    return 0 if not bad and n > 0 else 1


FINDINGS = Path(__file__).resolve().parents[2] / "cases" / "opt" / "progen"


def cmd_findings(args):
    """Runs every archived finding (findings.json) and says whether it still fails as recorded."""
    from runner import run_cell
    root = Path(args.dir) if args.dir else FINDINGS
    listed = json.loads((root / "findings.json").read_text(encoding="utf-8"))
    still = 0
    for f in listed:
        seen = collections.Counter()
        for _ in range(args.runs):
            info = run_cell(root / f["file"], f["pipeline"], f.get("env", {}), f["expected"])
            seen[signature(info)] += 1
        fails = sum(n for sig, n in seen.items() if sig.split(":")[0] in BAD)
        still += fails > 0
        state = "fails" if fails == args.runs else "fails sometimes" if fails else "does not fail"
        print(f"{f['id']:<4} {state:<16} {f['file']:<46} [{' '.join(f['pipeline']) or 'no pass'}] "
              + ", ".join(f"{sig} x{n}" for sig, n in seen.most_common()))
    print(f"progen findings: {len(listed)} recorded, {still} still fail")
    return 0


# Rewrite sequences every fixed finding is run under besides its own (on all four schedulers).
FIXED_ALSO = [["std::opt::inline"], ["std::opt"], ["std::opt::inline", "std::opt::dce"],
              ["std::opt::specialize"]]


def cmd_fixed(args):
    """Regression guard of the test plan: every finding recorded as fixed (findings.json, key
    `fixed`) must print its expected output under its own rewrite sequence and under FIXED_ALSO,
    on every scheduler."""
    from runner import run_cell
    root = Path(args.dir) if args.dir else FINDINGS
    listed = [f for f in json.loads((root / "findings.json").read_text(encoding="utf-8")) if f.get("fixed")]
    jobs = []
    for f in listed:
        own = [p for p in f["pipeline"] if p not in BACKENDS]
        for rewrites in [own] + [r for r in FIXED_ALSO if r != own]:
            for backend in f.get("fixed_backends", BACKENDS):
                jobs.append((f, rewrites + [backend]))
    def go(job):
        f, passes = job
        return f, passes, run_cell(root / f["file"], passes, f.get("env", {}), f["expected"])
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = list(pool.map(go, jobs))
    wrong = 0
    for f, passes, info in results:
        if info["outcome"] != "OK":
            wrong += 1
            print(f"{f['id']} {f['file']} [{' '.join(passes)}]: {signature(info)}: {info.get('detail', '')}")
    print(f"progen fixed: {len(listed)} findings, {len(results)} runs")
    print(f"{wrong} wrong")
    print("PASS" if not wrong and results else "FAIL")
    return 0 if not wrong and results else 1


SIZE_HELP = ("multiplies the number of functions and of statements of main (default 1: the programs of about "
             "115 lines the recorded seeds refer to; a seed names another program at another size)")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("show")
    p.add_argument("--seed", type=int, required=True)
    p.add_argument("--features")
    p.add_argument("--size", type=int, default=1, help=SIZE_HELP)
    p.set_defaults(fn=cmd_show)
    p = sub.add_parser("selfcheck")
    p.add_argument("--seeds", required=True)
    p.add_argument("--features")
    p.add_argument("--size", type=int, default=1, help=SIZE_HELP)
    p.add_argument("--jobs", type=int, default=4)
    p.add_argument("--out")
    p.set_defaults(fn=cmd_selfcheck)
    p = sub.add_parser("diff")
    p.add_argument("--seeds", required=True)
    p.add_argument("--matrix", default="full", choices=["full", "cross", "quick", "frozen", "reference"])
    p.add_argument("--backends", help="comma-separated, e.g. nvm,fvm (default: " + ",".join(BACKENDS) + ")")
    p.add_argument("--cells", help="comma-separated cell ids to keep")
    p.add_argument("--features")
    p.add_argument("--size", type=int, default=1, help=SIZE_HELP)
    p.add_argument("--jobs", type=int, default=4)
    p.add_argument("--out", required=True)
    p.add_argument("--resume", action="store_true", help="keep the programs already in OUT/results.partial.jsonl")
    p.set_defaults(fn=cmd_diff)
    p = sub.add_parser("reduce")
    p.add_argument("--seed", type=int, required=True)
    p.add_argument("--cell", default="default")
    p.add_argument("--passes", help="the pass list as one string, instead of --cell")
    p.add_argument("--matrix", default="full")
    p.add_argument("--features")
    p.add_argument("--size", type=int, default=1, help=SIZE_HELP)
    p.add_argument("--out", required=True)
    p.add_argument("--name")
    p.add_argument("--max-tests", type=int, default=4000)
    p.add_argument("--from", dest="from_dir", help="the --out directory of the diff run that found the failure: "
                   "its features are used and candidates run in the directory of the failing run")
    p.add_argument("--repeats", type=int, default=1, help="runs per candidate for a failure that is not deterministic")
    p.set_defaults(fn=cmd_reduce)
    p = sub.add_parser("check")
    p.add_argument("--jobs", type=int, default=4)
    p.set_defaults(fn=cmd_check)
    p = sub.add_parser("findings")
    p.add_argument("--dir")
    p.add_argument("--runs", type=int, default=3)
    p.set_defaults(fn=cmd_findings)
    p = sub.add_parser("fixed")
    p.add_argument("--dir")
    p.add_argument("--jobs", type=int, default=4)
    p.set_defaults(fn=cmd_fixed)
    p = sub.add_parser("features")
    p.set_defaults(fn=lambda a: print("\n".join(f"{k:<10} {v}" for k, v in FEATURES.items())) or 0)
    args = ap.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
