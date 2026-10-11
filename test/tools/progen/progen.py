#!/usr/bin/env python3
"""progen: random Camel programs with a reference output, and differential runs over pipelines.

    progen.py show      --seed N [--features LIST]            print the program and its reference
    progen.py selfcheck --seeds A-B [--features LIST] [--jobs J] [--out DIR]
    progen.py diff      --seeds A-B [--matrix full|quick|frozen] [--backends LIST]
                        [--features LIST] [--jobs J] --out DIR [--reduce K]
    progen.py reduce    --seed N --cell ID [--matrix M] [--features LIST] --out DIR
    progen.py check                                           the fixed batch of the test plan

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
from model import emit  # noqa: E402
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


def one(seed: int, features, cells, out, keep_all=False):
    prog, expected, attempts = generate(seed, features)
    source = emit(prog)
    name = f"progen_{seed}"
    work = None if out is None else Path(out) / "work" / name
    res = run_program(source, prog.a["env"], expected, cells, work, name)
    bad = {cid: info for cid, info in res.items() if info["outcome"] in BAD}
    for cid, info in bad.items():                       # is it the same on two more runs?
        again = [run_program(source, prog.a["env"], expected, {cid: cells[cid]}, work, name)[cid] for _ in range(2)]
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
        (d / f"{name}.json").write_text(json.dumps({"seed": seed, "features": sorted(features), "env": prog.a["env"],
                                                    "expected": expected, "bad": bad}, indent=1), encoding="utf-8")
    return record


def run_batch(seeds, features, cells, out, jobs):
    records = []
    started = time.time()
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = {pool.submit(one, s, features, cells, out): s for s in seeds}
        for n, fut in enumerate(concurrent.futures.as_completed(futures), 1):
            records.append(fut.result())
            if n % 100 == 0:
                print(f"  {n}/{len(seeds)} programs, {time.time() - started:.0f} s", file=sys.stderr, flush=True)
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
    prog, expected, attempts = generate(args.seed, parse_features(args.features))
    print(emit(prog))
    print("// environment:", prog.a["env"])
    print(f"// reference output ({attempts} attempt(s)):")
    for line in expected:
        print("// " + line)
    return 0


def cmd_selfcheck(args):
    features = parse_features(args.features)
    cells = matrix("reference")
    records = run_batch(seeds_of(args.seeds), features, cells, args.out, args.jobs)
    text = summarize(records, cells, features, "progen selfcheck: reference against the run without passes (node VM)")
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
    records = run_batch(seeds_of(args.seeds), features, cells, out, args.jobs)
    with open(out / "results.jsonl", "w", encoding="utf-8") as f:
        for r in records:
            f.write(json.dumps(r) + "\n")
    text = summarize(records, cells, features, f"progen diff: matrix {args.matrix}, seeds {args.seeds}")
    (out / "summary.txt").write_text(text, encoding="utf-8")
    print(text)
    return 0 if not any(r["bad"] for r in records) else 1


def cmd_reduce(args):
    features = parse_features(args.features)
    workdir = None
    if args.from_dir:
        case = json.loads((Path(args.from_dir) / "failures" / f"progen_{args.seed}.json").read_text(encoding="utf-8"))
        features = frozenset(case["features"])
        workdir = Path(args.from_dir) / "work" / f"progen_{args.seed}"
    prog, expected, _ = generate(args.seed, features)
    cells = matrix(args.matrix)
    passes = args.passes.split() if args.passes is not None else cells[args.cell]
    target = None
    for _ in range(args.repeats):
        res = run_program(emit(prog), prog.a["env"], expected, {"fail": passes, "ref": []}, workdir, prog.a["module"])
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
        res = run_program(source, prog.a["env"], expected, {"fail": passes}, out, name)["fail"]
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


# The fixed batch of the test plan (test/plans/feat/opt/progen.plan.toml): seeds and cells that
# all pass today. Known failures are recorded in test/cases/opt/progen/ and are not run here.
CHECK_SEEDS = "1-24"
CHECK_CELLS = ["default", "none@fvm", "none@jit", "inline@nvm", "fold@nvm", "cse@nvm", "dce@nvm", "macro@nvm"]
CHECK_EXCLUDED_FEATURES = ""


def cmd_check(args):
    features = parse_features(CHECK_EXCLUDED_FEATURES)
    cells = {k: v for k, v in matrix("full").items() if k in CHECK_CELLS}
    records = run_batch(seeds_of(CHECK_SEEDS), features, cells, None, args.jobs)
    bad = [(r["seed"], cid, info["outcome"], info.get("detail", "")) for r in records for cid, info in r["bad"].items()]
    for seed, cid, outcome, detail in bad:
        print(f"seed {seed} [{cid}]: {outcome}: {detail}")
    n = sum(len(r["outcomes"]) for r in records)
    print(f"progen check: {len(records)} programs, {n} program-pipeline cells, {len(bad)} wrong")
    print("PASS" if not bad and n > 0 else "FAIL")
    return 0 if not bad and n > 0 else 1


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("show")
    p.add_argument("--seed", type=int, required=True)
    p.add_argument("--features")
    p.set_defaults(fn=cmd_show)
    p = sub.add_parser("selfcheck")
    p.add_argument("--seeds", required=True)
    p.add_argument("--features")
    p.add_argument("--jobs", type=int, default=4)
    p.add_argument("--out")
    p.set_defaults(fn=cmd_selfcheck)
    p = sub.add_parser("diff")
    p.add_argument("--seeds", required=True)
    p.add_argument("--matrix", default="full", choices=["full", "quick", "frozen", "reference"])
    p.add_argument("--backends", help="comma-separated, e.g. nvm,fvm (default: " + ",".join(BACKENDS) + ")")
    p.add_argument("--cells", help="comma-separated cell ids to keep")
    p.add_argument("--features")
    p.add_argument("--jobs", type=int, default=4)
    p.add_argument("--out", required=True)
    p.set_defaults(fn=cmd_diff)
    p = sub.add_parser("reduce")
    p.add_argument("--seed", type=int, required=True)
    p.add_argument("--cell", default="default")
    p.add_argument("--passes", help="the pass list as one string, instead of --cell")
    p.add_argument("--matrix", default="full")
    p.add_argument("--features")
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
    p = sub.add_parser("features")
    p.set_defaults(fn=lambda a: print("\n".join(f"{k:<10} {v}" for k, v in FEATURES.items())) or 0)
    args = ap.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
