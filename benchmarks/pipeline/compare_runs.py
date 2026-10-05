#!/usr/bin/env python3
"""Compares two runs of the pipeline experiment on the SAME cells.

    compare_runs.py --old OLD_SUMMARY --new NEW_SUMMARY --out DIR

OLD_SUMMARY and NEW_SUMMARY are summary directories written by classify.py (invariance.csv,
refusal.csv, references.csv, totals.json). Classify both runs with the same rule set first; a
difference in rule sets is printed and written into the report, because it then explains part
of the differences.

A cell is (program id, pipeline id). The runs are compared on the intersection: the cells that
both runs judged, with the same program path and the same passes behind the pipeline id. Cells
judged in one run only are listed apart and are not part of any before/after number:
  only_new.csv / only_old.csv, with the reason: the program is not in the other run, the program
  is in the other run but was not judged there (invalid reference, non-deterministic, excluded),
  the pipeline is not in the other run, or the pipeline id names other passes there.
The must-refuse cells are compared the same way (set, program id, pipeline id).

Writes into DIR: same_cells.csv (every cell of the intersection whose outcome differs),
transitions.csv (old outcome, new outcome, cells), only_new.csv, only_old.csv, by_program.csv
(violation cells per program in both runs, on the intersection), compare.json and compare.md.
Everything is preliminary, container on a dev machine.
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


def read_csv(path: Path) -> list:
    if not path.exists() or path.stat().st_size == 0:
        return []
    with open(path, newline="", encoding="utf-8") as fh:
        return list(csv.DictReader(fh))


def reading(row: dict) -> str:
    o = row["outcome"]
    if o in c.VIOLATIONS or o in ("REFUSED_UNLOCATED", "REFUSED_NOT_CLEAN") or (o == "REFUSED" and row.get("clean") == "False"):
        return "violation"
    if o in c.NOT_JUDGED:
        return "not judged"
    if o == "NOT_APPLICABLE":
        return "not applicable"
    return "conforming"


def refusal_reading(row: dict) -> str:
    if not row["set"].startswith("must-refuse"):
        return "removed"
    return "conforming" if row["outcome"] in c.REFUSAL_CONFORMING else "violation"


def load(summary: Path) -> dict:
    totals = json.loads((summary / "totals.json").read_text(encoding="utf-8")) if (summary / "totals.json").exists() else {}
    return {"rules": totals.get("rules", "?"),
            "cells": {(r["program_id"], r["pipeline_id"]): r for r in read_csv(summary / "invariance.csv")},
            "refs": {r["program_id"]: r for r in read_csv(summary / "references.csv")},
            "refusal": {(r["set"], r["program_id"], r["pipeline_id"]): r for r in read_csv(summary / "refusal.csv")}}


def why_missing(key: tuple, row: dict, other: dict, pipelines_other: dict) -> str:
    prog, pid = key
    if prog not in other["refs"]:
        return "program not in the other run"
    if other["refs"][prog]["reference"] != "REFERENCE":
        return f"program not judged in the other run ({other['refs'][prog]['reference']})"
    if pid not in pipelines_other:
        return "pipeline not in the other run"
    return "cell not in the other run (pipeline not applied to this program there)"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--old", required=True)
    ap.add_argument("--new", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    old, new = load(Path(args.old)), load(Path(args.new))
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    same_rules = old["rules"] == new["rules"]
    if not same_rules:
        print(f"[compare] WARNING: the runs were classified with different rule sets "
              f"({old['rules']} / {new['rules']}); reclassify one of them first", file=sys.stderr)
    passes_old = {pid: r["passes"] for (_, pid), r in old["cells"].items()}
    passes_new = {pid: r["passes"] for (_, pid), r in new["cells"].items()}
    redefined = {pid for pid in passes_old.keys() & passes_new.keys() if passes_old[pid] != passes_new[pid]}

    common, moved_path = [], []
    for key in sorted(old["cells"].keys() & new["cells"].keys()):
        if key[1] in redefined:
            continue
        if old["cells"][key]["program"] != new["cells"][key]["program"]:
            moved_path.append(key)
            continue
        common.append(key)
    common_set = set(common)

    changed, transitions = [], collections.Counter()
    by_program = collections.defaultdict(lambda: {"cells": 0, "old_violations": 0, "new_violations": 0})
    counts = {"old": collections.Counter(), "new": collections.Counter()}
    for key in common:
        a, b = old["cells"][key], new["cells"][key]
        ra, rb = reading(a), reading(b)
        counts["old"][ra] += 1
        counts["new"][rb] += 1
        p = by_program[key[0]]
        p["cells"] += 1
        p["old_violations"] += ra == "violation"
        p["new_violations"] += rb == "violation"
        if a["outcome"] != b["outcome"]:
            transitions[(a["outcome"], b["outcome"], ra, rb)] += 1
            changed.append({"program": b["program"], "program_id": key[0], "pipeline_id": key[1],
                            "passes": b["passes"], "old_outcome": a["outcome"], "new_outcome": b["outcome"],
                            "old_reading": ra, "new_reading": rb, "old_detail": a.get("detail", "")[:200],
                            "new_detail": b.get("detail", "")[:200]})

    def only(this: dict, other: dict, pipelines_other: dict) -> list:
        rows = []
        for key in sorted(this["cells"].keys() - common_set):
            r = this["cells"][key]
            reason = ("pipeline id names other passes in the other run" if key[1] in redefined
                      else "program id names another file in the other run" if key in moved_path
                      else why_missing(key, r, other, pipelines_other))
            rows.append({"program": r["program"], "program_id": key[0], "pipeline_id": key[1], "passes": r["passes"],
                         "outcome": r["outcome"], "reading": reading(r), "reason": reason,
                         "detail": r.get("detail", "")[:200]})
        return rows

    only_new, only_old = only(new, old, passes_old), only(old, new, passes_new)

    ref_common = sorted(old["refusal"].keys() & new["refusal"].keys())
    ref_changed, ref_transitions = [], collections.Counter()
    for key in ref_common:
        a, b = old["refusal"][key], new["refusal"][key]
        if a["passes"] != b["passes"]:
            continue
        if a["outcome"] != b["outcome"]:
            ref_transitions[(key[0], a["outcome"], b["outcome"])] += 1
            ref_changed.append({"set": key[0], "program": b["program"], "program_id": key[1], "pipeline_id": key[2],
                                "passes": b["passes"], "old_outcome": a["outcome"], "new_outcome": b["outcome"],
                                "old_reading": refusal_reading(a), "new_reading": refusal_reading(b)})

    def write(name: str, rows: list, fields: list) -> None:
        with open(out / name, "w", newline="", encoding="utf-8") as fh:
            w = csv.DictWriter(fh, fieldnames=fields)
            w.writeheader()
            w.writerows(rows)

    cell_fields = ["program", "program_id", "pipeline_id", "passes", "old_outcome", "new_outcome", "old_reading",
                   "new_reading", "old_detail", "new_detail"]
    write("same_cells.csv", changed, cell_fields)
    write("transitions.csv", [{"old_outcome": k[0], "new_outcome": k[1], "old_reading": k[2], "new_reading": k[3],
                               "cells": n} for k, n in sorted(transitions.items(), key=lambda kv: -kv[1])],
          ["old_outcome", "new_outcome", "old_reading", "new_reading", "cells"])
    only_fields = ["program", "program_id", "pipeline_id", "passes", "outcome", "reading", "reason", "detail"]
    write("only_new.csv", only_new, only_fields)
    write("only_old.csv", only_old, only_fields)
    write("by_program.csv", [{"program_id": k, **v} for k, v in sorted(by_program.items())
                             if v["old_violations"] or v["new_violations"]],
          ["program_id", "cells", "old_violations", "new_violations"])
    write("must_refuse_same_cells.csv", ref_changed, ["set", "program", "program_id", "pipeline_id", "passes",
                                                      "old_outcome", "new_outcome", "old_reading", "new_reading"])

    result = {
        "label": c.LABEL, "old": str(args.old), "new": str(args.new),
        "old_rules": old["rules"], "new_rules": new["rules"], "same_rule_set": same_rules,
        "cells_old": len(old["cells"]), "cells_new": len(new["cells"]), "cells_in_both": len(common),
        "pipeline_ids_with_other_passes": sorted(redefined),
        "program_ids_naming_another_file": len(moved_path),
        "readings_old_on_common_cells": dict(counts["old"]), "readings_new_on_common_cells": dict(counts["new"]),
        "common_cells_with_another_outcome": len(changed),
        "violation_to_conforming": sum(n for k, n in transitions.items() if k[2] == "violation" and k[3] == "conforming"),
        "conforming_to_violation": sum(n for k, n in transitions.items() if k[2] == "conforming" and k[3] == "violation"),
        "programs_with_violation_old_on_common_cells": sum(1 for v in by_program.values() if v["old_violations"]),
        "programs_with_violation_new_on_common_cells": sum(1 for v in by_program.values() if v["new_violations"]),
        "cells_only_new": len(only_new), "cells_only_new_by_reason": dict(collections.Counter(r["reason"] for r in only_new)),
        "cells_only_new_by_reading": dict(collections.Counter(r["reading"] for r in only_new)),
        "cells_only_old": len(only_old), "cells_only_old_by_reason": dict(collections.Counter(r["reason"] for r in only_old)),
        "cells_only_old_by_reading": dict(collections.Counter(r["reading"] for r in only_old)),
        "must_refuse_cells_in_both": len(ref_common), "must_refuse_cells_with_another_outcome": len(ref_changed),
        "must_refuse_transitions": {f"{k[0]}: {k[1]} -> {k[2]}": n for k, n in sorted(ref_transitions.items(), key=lambda kv: -kv[1])},
    }
    (out / "compare.json").write_text(json.dumps(result, indent=1), encoding="utf-8")
    lines = [f"# Same-cells comparison ({c.LABEL})", "",
             f"old: `{args.old}` (rules {old['rules']}); new: `{args.new}` (rules {new['rules']})"
             + ("" if same_rules else " -- **different rule sets: reclassify one run first**"), "",
             f"Cells judged: old {len(old['cells'])}, new {len(new['cells'])}, in both {len(common)}.", "",
             "| reading on the common cells | old | new |", "|---|---:|---:|"]
    for name in ("conforming", "violation", "not judged", "not applicable"):
        lines.append(f"| {name} | {counts['old'][name]} | {counts['new'][name]} |")
    lines += ["", "| old outcome | new outcome | cells |", "|---|---|---:|"]
    lines += [f"| {k[0]} | {k[1]} | {n} |" for k, n in sorted(transitions.items(), key=lambda kv: -kv[1])]
    lines += ["", f"Cells only in the new run: {len(only_new)}",
              *[f"- {k}: {n}" for k, n in collections.Counter((r['reason'], r['reading']) for r in only_new).most_common()],
              "", f"Cells only in the old run: {len(only_old)}",
              *[f"- {k}: {n}" for k, n in collections.Counter((r['reason'], r['reading']) for r in only_old).most_common()],
              "", f"Must-refuse cells in both runs: {len(ref_common)}; with another outcome: {len(ref_changed)}",
              *[f"- {k[0]}: {k[1]} -> {k[2]}: {n}" for k, n in sorted(ref_transitions.items(), key=lambda kv: -kv[1])]]
    (out / "compare.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())
