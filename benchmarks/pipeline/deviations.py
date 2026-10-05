#!/usr/bin/env python3
"""Counts what each recorded deviation (D2, D4, D5, D7; see classify.DEVIATIONS) reclassifies
on one set of records, and writes the summary of every rule set involved.

    deviations.py --records DIR/records [...] --out DIR [--corpus corpus.json]
                  [--exclusions exclusions.json]

Each deviation that changes a rule is measured three ways:
  alone       frozen+D1 against frozen+D1+Dk
  last        the newest rule set without Dk against the newest rule set
  cumulative  the chain frozen+D1 -> +D2 -> +D4 -> +D5 -> +D7, each step against the previous
A cell is counted when its outcome or its reading changes. The reading of a cell is one of
conforming (agrees, or a clean located refusal; before D2 any clean refusal) / violation /
not judged / not applicable / (program not judged) / (removed from the must-refuse set).
D3, D6, D8 change no rule and D9 only adds tables, so they move no cell by construction; D6 is
reported as the benchmark cells that D7 makes judged.

Writes <out>/<rule set>/ (the full summary of classify.py) for frozen, frozen+D1, the rule sets
of the chain and the newest one, and <out>/deviations.csv, deviations.md, deviations.json.
`--corpus` is needed for D5 on records made before it (see classify.py). Everything is
preliminary, container on a dev machine.
"""

from __future__ import annotations

import argparse
import collections
import csv
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import classify as c  # noqa: E402

FLAGGED = ["D2", "D4", "D5", "D7"]
REPORTING = "D9"


def reading(row: dict, refusal: bool) -> str:
    o = row["outcome"]
    if refusal:
        if not row["set"].startswith("must-refuse"):
            return "(removed from the must-refuse set)"
        return "conforming" if o in c.REFUSAL_CONFORMING else "violation"
    if c.is_violation(row):
        return "violation"
    if o in c.NOT_JUDGED:
        return "not judged"
    if o == "NOT_APPLICABLE":
        return "not applicable"
    return "conforming"


def states(cells, rules, exclusions, removed) -> dict:
    rows, ref_rows, refusal_rows, _, _ = c.classify_all(cells, rules, 20.0, 10.0, exclusions, removed)
    out = {}
    for r in rows:
        out[("agree", r["program_id"], r["pipeline_id"])] = (r["outcome"], reading(r, False), r["origin"])
    for r in refusal_rows:
        out[(r["set"].split(" (")[0] if r["set"].startswith("must-refuse") else "must-refuse program",
             r["program_id"], r["pipeline_id"])] = (
            r["outcome"] if r["set"].startswith("must-refuse") else "(removed)", reading(r, True), r["origin"])
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--records", required=True, nargs="+")
    ap.add_argument("--out", required=True)
    ap.add_argument("--corpus", default="")
    ap.add_argument("--exclusions", default="")
    args = ap.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    exclusions = c.load_exclusions(args.exclusions or None)
    removed = c.load_removed(args.corpus) if args.corpus else None
    cells = c.load_cells(args.records)
    print(f"[deviations] {sum(len(v) for v in cells.values())} records, {len(cells)} cells; {c.LABEL}", flush=True)

    def name(devs):
        return "frozen+D1" + "".join(f"+{d}" for d in devs)

    chain = [name(FLAGGED[:i]) for i in range(len(FLAGGED) + 1)]
    wanted = {"frozen", "frozen+D1", c.NEWEST, *chain}
    for d in FLAGGED:
        wanted.add(name([d]))
        wanted.add(name([x for x in FLAGGED if x != d] + [REPORTING]))
    state = {}
    keys = set()
    for rs in sorted(wanted):
        rules = c.resolve_rules(rs)
        state[rs] = states(cells, rules, exclusions, removed)
        keys |= set(state[rs])
    for rs in ["frozen", "frozen+D1", *chain[1:], c.NEWEST]:
        c.summarize(cells, c.resolve_rules(rs), out / rs, 20.0, 10.0, exclusions, removed)
    missing = ("(program not judged)", "(program not judged)", "")

    def diff(a: str, b: str):
        moved = collections.Counter()
        for k in keys:
            x, y = state[a].get(k, missing), state[b].get(k, missing)
            if x[:2] != y[:2]:
                moved[(k[0], x[0], y[0], x[1], y[1], "benchmarks" if (x[2] or y[2]) == "benchmarks" else "other")] += 1
        return moved

    table, summary = [], []
    comparisons = []
    for d in FLAGGED:
        comparisons.append((d, "alone", "frozen+D1", name([d])))
        comparisons.append((d, "last", name([x for x in FLAGGED if x != d] + [REPORTING]), c.NEWEST))
    for prev, cur in zip(chain, chain[1:]):
        comparisons.append((cur.rsplit("+", 1)[1], "cumulative", prev, cur))
    comparisons.append(("D1", "alone", "frozen", "frozen+D1"))
    comparisons.append(("D2-D9", "all", "frozen+D1", c.NEWEST))
    for dev, how, a, b in comparisons:
        moved = diff(a, b)
        for (kind, o1, o2, r1, r2, part), n in sorted(moved.items(), key=lambda kv: -kv[1]):
            table.append({"deviation": dev, "comparison": how, "from_rules": a, "to_rules": b, "set": kind,
                          "corpus_part": part, "from_outcome": o1, "to_outcome": o2, "from_reading": r1,
                          "to_reading": r2, "cells": n})
        reading_changes = sum(n for k, n in moved.items() if k[3] != k[4])
        summary.append({"deviation": dev, "comparison": how, "from_rules": a, "to_rules": b,
                        "cells_reclassified": sum(moved.values()),
                        "of_which_agree_set": sum(n for k, n in moved.items() if k[0] == "agree"),
                        "of_which_must_refuse": sum(n for k, n in moved.items() if k[0] != "agree"),
                        "of_which_benchmark_cells": sum(n for k, n in moved.items() if k[5] == "benchmarks"),
                        "cells_whose_reading_changes": reading_changes,
                        "to_violation": sum(n for k, n in moved.items() if k[4] == "violation" and k[3] != "violation"),
                        "from_violation": sum(n for k, n in moved.items() if k[3] == "violation" and k[4] != "violation"),
                        "to_conforming": sum(n for k, n in moved.items() if k[4] == "conforming" and k[3] != "conforming"),
                        "to_not_judged_or_removed": sum(n for k, n in moved.items() if k[4] not in ("violation", "conforming")
                                                        and k[3] != k[4])})

    for fname, data in (("deviations.csv", table), ("deviations_summary.csv", summary)):
        with open(out / fname, "w", newline="", encoding="utf-8") as fh:
            w = csv.DictWriter(fh, fieldnames=list(data[0].keys()))
            w.writeheader()
            w.writerows(data)
    lines = [f"Reclassification per deviation ({c.LABEL}).", "",
             "| deviation | comparison | from -> to rules | cells reclassified (agree / must-refuse) | "
             "reading changes | to violation | from violation | to conforming | to not judged or removed | moves |",
             "|---|---|---|---:|---:|---:|---:|---:|---:|---|"]
    for s in summary:
        moves = [t for t in table if (t["deviation"], t["comparison"]) == (s["deviation"], s["comparison"])]
        merged = collections.Counter()
        for t in moves:
            merged[(t["set"], t["from_outcome"], t["to_outcome"])] += t["cells"]
        text = "; ".join(f"{k[1]} -> {k[2]}: {n}" + ("" if k[0] == "agree" else f" ({k[0]})")
                         for k, n in sorted(merged.items(), key=lambda kv: -kv[1])[:8]) or "none"
        lines.append(f"| {s['deviation']} | {s['comparison']} | {s['from_rules']} -> {s['to_rules']} | "
                     f"{s['cells_reclassified']} ({s['of_which_agree_set']} / {s['of_which_must_refuse']}) | "
                     f"{s['cells_whose_reading_changes']} | {s['to_violation']} | {s['from_violation']} | "
                     f"{s['to_conforming']} | {s['to_not_judged_or_removed']} | {text} |")
    (out / "deviations.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    (out / "deviations.json").write_text(json.dumps({"label": c.LABEL, "summary": summary}, indent=1), encoding="utf-8")
    print("\n".join(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())
