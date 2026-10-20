#!/usr/bin/env python3
"""Reclassifies one set of records under each rule set of classify.py, one rule change at a
time, and reports how many cells each change moved from which outcome to which.

    history.py --records DIR/records --out DIR/history [--steps v1,v1+macro-ops,...]

Writes `transitions.csv` (step, rule change, from, to, cells), `steps.csv` (per rule set:
judged programs, cells, violation cells, outcome counts) and `transitions.md`. A cell of a
program that a rule set does not judge has the outcome `(program not judged)`.
"""

from __future__ import annotations

import argparse
import collections
import csv
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import classify  # noqa: E402

STEPS = [
    ("v1", "first classification: effect events compared as (operator, payload hash)"),
    ("v1+macro-ops", "macro operators (autodiff:grad, with_grad, vjp) left out of the effect sequence"),
    ("v2", "payload hashes no longer compared (only flagged)"),
    ("v2+tolerance", "printed floats compared with tolerance 1e-5 (plan C-3)"),
    ("v3", "non-zero exit without a structured diagnostic becomes ERROR_NO_DIAGNOSTIC"),
    ("v3+payload", "payloads compared again: by value where the trace has the text, else by hash; "
                   "a hash difference without text is UNDECIDED"),
    ("v3+payload+c10", "time limit max(10 s, 20 x zero-pass time): a run that finishes later is RESOURCE_LIMIT"),
    ("v3+payload+c10+worst", "a cell takes the worst outcome of its repeats (was: the first repeat); "
                             "NOT_APPLICABLE when any repeat reports the pass as not found"),
    ("frozen", "determinism of the zero-pass run includes effect payloads and written files; "
               "non-deterministic programs are NONDETERMINISTIC (listed), separate from INVALID_REF; "
               "written files compared; MACRO_EXCEPTION (the last two need data the first run did not record)"),
]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--records", required=True, nargs="+")
    ap.add_argument("--out", required=True)
    ap.add_argument("--time-factor", type=float, default=20.0)
    ap.add_argument("--time-floor", type=float, default=10.0)
    args = ap.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    cells = classify.load_cells(args.records)
    all_keys = sorted(k for k, recs in cells.items()
                      if k[1] != classify.pl.REFERENCE and recs[0].get("role", "agree") == "agree"
                      and classify.pl.kind(k[1], recs[0]["passes"]) != "refuse")
    results, steps = {}, []
    for name, _ in STEPS:
        rules = classify.RULESETS[name]
        rows, ref_rows, _, _, _ = classify.classify_all(cells, rules, args.time_factor, args.time_floor)
        outcome = {(r["program_id"], r["pipeline_id"]): ("REFUSED_NOT_CLEAN" if r["outcome"] == "REFUSED"
                                                         and r["clean"] is False else r["outcome"]) for r in rows}
        results[name] = {k: outcome.get(k, "(program not judged)") for k in all_keys}
        refs = collections.Counter(r["reference"] for r in ref_rows)
        steps.append({"rules": name, "programs_judged": refs["REFERENCE"],
                      "programs_invalid_reference": refs["INVALID_REF"],
                      "programs_nondeterministic": refs["NONDETERMINISTIC"], "cells": len(rows),
                      "violation_cells": sum(1 for r in rows if classify.is_violation(r)),
                      "outcomes": json.dumps(dict(collections.Counter(r["outcome"] for r in rows)), sort_keys=True)})

    transitions = []
    for (prev, _), (name, change) in zip(STEPS, STEPS[1:]):
        moved = collections.Counter((results[prev][k], results[name][k]) for k in all_keys
                                    if results[prev][k] != results[name][k])
        if not moved:
            transitions.append({"from_rules": prev, "to_rules": name, "rule_change": change,
                                "from": "", "to": "", "cells": 0})
        for (a, b), n in sorted(moved.items(), key=lambda kv: -kv[1]):
            transitions.append({"from_rules": prev, "to_rules": name, "rule_change": change,
                                "from": a, "to": b, "cells": n})

    for fname, data in (("steps.csv", steps), ("transitions.csv", transitions)):
        with open(out / fname, "w", newline="", encoding="utf-8") as fh:
            w = csv.DictWriter(fh, fieldnames=list(data[0].keys()))
            w.writeheader()
            w.writerows(data)
    lines = ["| step | rule change | cells moved | violation cells after |", "|---|---|---|---:|"]
    viol = {s["rules"]: s["violation_cells"] for s in steps}
    for (prev, _), (name, change) in zip(STEPS, STEPS[1:]):
        moved = [t for t in transitions if t["to_rules"] == name and t["cells"]]
        text = "; ".join(f"{t['from']} -> {t['to']}: {t['cells']}" for t in moved) or "none"
        lines.append(f"| {prev} -> {name} | {change} | {text} | {viol[name]} |")
    (out / "transitions.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines))
    for s in steps:
        print(s)
    return 0


if __name__ == "__main__":
    sys.exit(main())
