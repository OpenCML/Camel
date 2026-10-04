#!/usr/bin/env python3
"""Classifies pipeline-runner records against the "agree or refuse" contract.

The zero-pass run (P0) defines a program's observable behaviour: exit code, the program's own
output, and the ordered effect trace. For every other pipeline the outcome is decided from
the records only (no manual overrides), first matching rule wins:

  INVALID_REF       P0 itself timed out, crashed, reported an error, exited non-zero, or its
                    repeats differ (non-deterministic program). The program is not judged.
  NOT_APPLICABLE    the pipeline names a module-contributed pass (e.g. tensor::fuse) and the
                    program does not import that module ("pass not found").
  HANG              a run exceeded the time limit.
  CRASH             the process died from a signal, or exited non-zero without a diagnostic.
  CAPTURE_FAIL      a diagnostic of stage parse/typecheck (cannot differ from P0 legitimately).
  REFUSED           a diagnostic of stage pass:<name>: a rewrite/translation pass refused.
                    Flags: `anchored` (the diagnostic has a file and a non-empty range) and
                    `clean` (nothing was printed and no run-stage effect happened before it).
  RUN_FAIL          a diagnostic of stage run (or any other stage): the program started
                    executing and failed, although P0 succeeded.
  ERROR_NO_DIAGNOSTIC  non-zero exit with an error text but no structured diagnostic (an
                    internal error surfaced as plain text), although P0 succeeded.
  VALUE_MISMATCH    exit code or program output differs from P0. Output is compared as text
                    with numeric tolerance (plan C-3): the non-numeric text must be identical,
                    integers equal, and every floating-point number within
                    |a-b| <= 1e-5 * max(1, |a|). Cells that differ in text but pass this test
                    are CONSISTENT with `numeric_diff` set (counted separately); cells that
                    fail at 1e-5 but pass at 1e-3 are VALUE_MISMATCH tagged `near`.
  EFFECT_MISMATCH   same exit code and output, but the ordered sequence of executed effectful
                    operators differs (see "Effect sequence" below).
  CONSISTENT        none of the above.

Effect sequence. The trace lists every executed operator that is not registered pure. Two
adjustments are made before comparing, both decided after the first run on existing programs
showed what the raw trace contains (the first classification is kept with the results):
  - macro operators (`autodiff:grad`, `autodiff:value_and_grad`, `autodiff:vjp`) are left out:
    they build a function value and have no effect outside the program; `std::macro` evaluates
    them once ahead of time by design, so their count is not an observable.
  - payload hashes are not part of the sequence. A payload hashes the argument values at full
    precision, so two runs whose printed output is identical can still differ in the last
    bits of a float argument. Cells whose sequence agrees but whose payloads differ are
    CONSISTENT with `payload_differs` set, and are counted separately.

`deterministic` is false when the repeats of that (program, pipeline) differ from each other;
such cells keep the outcome of the first repeat and are listed for investigation.

`macro_stage` marks a mismatch on a pipeline containing std::macro that is identical to the
plain `std::macro std::nvm` run (P4) while P4 itself differs from P0: the difference is then
attributable to ahead-of-time evaluation of macro calls alone, not to the other passes.

Contract view per cell: agree = CONSISTENT; refuse = REFUSED with clean (anchored or not);
violation = HANG, CRASH, CAPTURE_FAIL, RUN_FAIL, VALUE_MISMATCH, EFFECT_MISMATCH, or a REFUSED
that is not clean. `accepted` = the pipeline did not refuse (everything except REFUSED and
NOT_APPLICABLE).

Usage: classify.py --records DIR/records --out DIR/summary
"""

from __future__ import annotations

import argparse
import collections
import csv
import json
import re
import sys
from pathlib import Path
from typing import Dict, List, Optional, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pipelines as pl  # noqa: E402

VIOLATIONS = {"HANG", "CRASH", "CAPTURE_FAIL", "RUN_FAIL", "ERROR_NO_DIAGNOSTIC", "VALUE_MISMATCH",
              "EFFECT_MISMATCH"}

_NUM = re.compile(r"[-+]?(?:\d+\.\d*|\.\d+|\d+)(?:[eE][-+]?\d+)?")


def numeric_compare(ref: str, got: str, tol: float) -> bool:
    """True when the two texts are equal except for floating-point numbers within `tol`."""
    if _NUM.sub("#", ref) != _NUM.sub("#", got):
        return False
    for a, b in zip(_NUM.findall(ref), _NUM.findall(got)):
        if a == b:
            continue
        if not any(c in a + b for c in ".eE"):
            return False  # integers must match exactly
        x, y = float(a), float(b)
        if abs(x - y) > tol * max(1.0, abs(x)):
            return False
    return True


def errors_of(rec: dict) -> List[dict]:
    return [d for d in (rec.get("diagnostics") or []) if d.get("severity") == "Error"]


MACRO_OPERATORS = {"autodiff:grad", "autodiff:value_and_grad", "autodiff:vjp"}


def effect_key(rec: dict) -> List[str]:
    return [e.get("uri", "?") for e in rec.get("effects", []) if e.get("uri") not in MACRO_OPERATORS]


def effect_payloads(rec: dict) -> List[str]:
    return [e.get("payload_sha1", "?") for e in rec.get("effects", [])
            if e.get("uri") not in MACRO_OPERATORS]


def effect_origins(rec: dict) -> List[Optional[Tuple[str, int, int]]]:
    out = []
    for e in rec.get("effects", []):
        o = e.get("origin")
        out.append((o["file"], o["line"], o["col"]) if o else None)
    return out


def observation(rec: dict) -> tuple:
    return (rec["timed_out"], rec["exit_code"], rec["output_sha1"], tuple(effect_key(rec)),
            tuple((d.get("data", {}).get("name"), d.get("data", {}).get("stage")) for d in errors_of(rec)))


def anchored(diag: dict) -> bool:
    r = diag.get("range", {})
    nonzero = any(r.get(k, {}).get(f, 0) for k in ("start", "end") for f in ("line", "character"))
    return bool(diag.get("data", {}).get("modulePath")) and bool(nonzero)


def first_diff(a: str, b: str) -> str:
    la, lb = a.splitlines(), b.splitlines()
    for i in range(max(len(la), len(lb))):
        x = la[i] if i < len(la) else "<missing>"
        y = lb[i] if i < len(lb) else "<missing>"
        if x != y:
            return f"line {i + 1}: ref={x[:80]!r} got={y[:80]!r}"
    return ""


def classify_ref(recs: List[dict]) -> Tuple[str, str]:
    r = recs[0]
    if any(x["timed_out"] for x in recs):
        return "INVALID_REF", "reference timed out"
    if errors_of(r):
        d = errors_of(r)[0]["data"]
        return "INVALID_REF", f"reference reports {d.get('name')} (stage {d.get('stage')})"
    if r["exit_code"] != 0:
        return "INVALID_REF", f"reference exit code {r['exit_code']}"
    if len({observation(x) for x in recs}) > 1:
        return "INVALID_REF", "reference is non-deterministic across repeats"
    return "REFERENCE", ""


def classify_cell(ref: dict, recs: List[dict]) -> dict:
    r = recs[0]
    info = {"outcome": "", "detail": "", "anchored": "", "clean": "", "diag_stage": "", "diag_name": "",
            "payload_differs": False, "numeric_diff": False,
            "deterministic": len({observation(x) for x in recs}) == 1}
    errs = errors_of(r)
    if errs:
        info["diag_stage"] = errs[0].get("data", {}).get("stage", "")
        info["diag_name"] = errs[0].get("data", {}).get("name", "")
    if errs and info["diag_name"].endswith("UnrecognizedGraphPass"):
        missing = [p for p in r["passes"] if p in pl.MODULE_PASSES and f"'{p}'" in errs[0].get("message", "")]
        if missing:
            info.update(outcome="NOT_APPLICABLE", detail=f"{missing[0]} needs module {pl.MODULE_PASSES[missing[0]]}")
            return info
    if r["timed_out"]:
        info.update(outcome="HANG", detail=f"exceeded {r['timeout_s']} s")
    elif r["exit_code"] is None or r["exit_code"] < 0 or (r["exit_code"] != 0 and not errs and r["exit_code"] >= 128):
        info.update(outcome="CRASH", detail=f"exit code {r['exit_code']}")
    elif errs:
        stage = info["diag_stage"]
        if stage in ("parse", "typecheck"):
            info.update(outcome="CAPTURE_FAIL", detail=errs[0].get("message", "")[:160])
        elif stage.startswith("pass:"):
            ran = [e for e in r.get("effects", []) if e.get("stage") == "run"]
            info.update(outcome="REFUSED", detail=errs[0].get("message", "")[:160],
                        anchored=anchored(errs[0]), clean=(r["output"] == "" and not ran))
        else:
            info.update(outcome="RUN_FAIL", detail=errs[0].get("message", "")[:160], anchored=anchored(errs[0]))
    elif r["exit_code"] != 0 and ref["exit_code"] == 0:
        info.update(outcome="ERROR_NO_DIAGNOSTIC",
                    detail=f"exit {r['exit_code']}; " + (r["output"].strip().splitlines() or [""])[-1][:160])
    elif (r["exit_code"] == ref["exit_code"] and r["output_sha1"] != ref["output_sha1"]
          and effect_key(r) == effect_key(ref) and numeric_compare(ref["output"], r["output"], 1e-5)):
        info.update(outcome="CONSISTENT", numeric_diff=True, detail=first_diff(ref["output"], r["output"]),
                    payload_differs=effect_payloads(r) != effect_payloads(ref))
    elif r["exit_code"] != ref["exit_code"] or r["output_sha1"] != ref["output_sha1"]:
        reorder = sorted(r["output"].splitlines()) == sorted(ref["output"].splitlines())
        detail = f"exit {ref['exit_code']} -> {r['exit_code']}; " if r["exit_code"] != ref["exit_code"] else ""
        info.update(outcome="VALUE_MISMATCH",
                    detail=detail + ("same lines, different order; " if reorder else "")
                    + ("near (within 1e-3); " if numeric_compare(ref["output"], r["output"], 1e-3) else "")
                    + first_diff(ref["output"], r["output"]))
    elif effect_key(r) != effect_key(ref):
        a, b = effect_key(ref), effect_key(r)
        at = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]), min(len(a), len(b)))
        info.update(outcome="EFFECT_MISMATCH",
                    detail=f"{len(a)} effect(s) in reference, {len(b)} here; first difference at #{at}: "
                           f"ref={a[at] if at < len(a) else '<end>'} got={b[at] if at < len(b) else '<end>'}")
    else:
        info.update(outcome="CONSISTENT", payload_differs=effect_payloads(r) != effect_payloads(ref))
        ro, po = effect_origins(ref), effect_origins(r)
        if any(a and b and a != b for a, b in zip(ro, po)):
            info["detail"] = "effect origins differ"
    return info


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--records", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    cells: Dict[Tuple[str, str], List[dict]] = collections.defaultdict(list)
    for path in sorted(Path(args.records).rglob("*.json")):
        rec = json.loads(path.read_text(encoding="utf-8"))
        cells[(rec["program_id"], rec["pipeline_id"])].append(rec)
    for recs in cells.values():
        recs.sort(key=lambda r: r["repeat"])

    programs = sorted({p for p, _ in cells})
    pipeline_ids = sorted({q for _, q in cells}, key=lambda s: (s[0], int("".join(c for c in s if c.isdigit()) or 0), s))
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    rows: List[dict] = []
    ref_rows: List[dict] = []
    for prog in programs:
        if (prog, pl.REFERENCE) not in cells:
            continue
        ref_recs = cells[(prog, pl.REFERENCE)]
        ref_outcome, ref_detail = classify_ref(ref_recs)
        ref = ref_recs[0]
        ref_rows.append({"program": ref["program"], "corpus": ref["corpus"], "origin": ref["origin"],
                         "reference": ref_outcome, "detail": ref_detail,
                         "n_effects": len(ref.get("effects", [])),
                         "wall_ms": ref["wall_ms"]})
        if ref_outcome == "INVALID_REF":
            continue
        macro_ref = cells.get((prog, pl.MACRO_REFERENCE), [None])[0]
        for pid in pipeline_ids:
            if pid == pl.REFERENCE or (prog, pid) not in cells:
                continue
            recs = cells[(prog, pid)]
            info = classify_cell(ref, recs)
            macro_stage = False
            if (info["outcome"] in ("VALUE_MISMATCH", "EFFECT_MISMATCH") and "std::macro" in recs[0]["passes"]
                    and macro_ref is not None and observation(macro_ref) != observation(ref)
                    and observation(recs[0]) == observation(macro_ref)):
                macro_stage = True
            rows.append({"program": recs[0]["program"], "corpus": recs[0]["corpus"], "origin": recs[0]["origin"],
                         "pipeline_id": pid, "passes": " ".join(recs[0]["passes"]),
                         "outcome": info["outcome"], "deterministic": info["deterministic"],
                         "anchored": info["anchored"], "clean": info["clean"],
                         "macro_stage": macro_stage, "payload_differs": info["payload_differs"],
                         "numeric_diff": info["numeric_diff"],
                         "diag_stage": info["diag_stage"],
                         "diag_name": info["diag_name"], "detail": info["detail"]})

    def write_csv(name: str, data: List[dict]) -> None:
        if not data:
            (out / name).write_text("", encoding="utf-8")
            return
        with open(out / name, "w", newline="", encoding="utf-8") as fh:
            w = csv.DictWriter(fh, fieldnames=list(data[0].keys()))
            w.writeheader()
            w.writerows(data)

    write_csv("references.csv", ref_rows)
    write_csv("invariance.csv", rows)

    def is_violation(row: dict) -> bool:
        return row["outcome"] in VIOLATIONS or (row["outcome"] == "REFUSED" and row["clean"] is False)

    write_csv("violations.csv", [r for r in rows if is_violation(r) or not r["deterministic"]])

    by_pipeline: List[dict] = []
    for pid in pipeline_ids:
        prs = [r for r in rows if r["pipeline_id"] == pid]
        if not prs:
            continue
        cnt = collections.Counter(r["outcome"] for r in prs)
        applicable = len(prs) - cnt["NOT_APPLICABLE"]
        accepted = applicable - cnt["REFUSED"]
        refused = [r for r in prs if r["outcome"] == "REFUSED"]
        viol = [r for r in prs if is_violation(r)]
        by_pipeline.append({
            "pipeline_id": pid, "passes": prs[0]["passes"], "programs": len(prs),
            "not_applicable": cnt["NOT_APPLICABLE"], "applicable": applicable, "accepted": accepted,
            "acceptance_rate": round(accepted / applicable, 4) if applicable else "",
            "consistent": cnt["CONSISTENT"],
            "agreement_among_accepted": round(cnt["CONSISTENT"] / accepted, 4) if accepted else "",
            "refused": len(refused),
            "refused_anchored": sum(1 for r in refused if r["anchored"] is True),
            "refused_unanchored": sum(1 for r in refused if r["anchored"] is not True),
            "refused_not_clean": sum(1 for r in refused if r["clean"] is False),
            "violations": len(viol),
            "violations_macro_stage": sum(1 for r in viol if r["macro_stage"]),
            "value_mismatch": cnt["VALUE_MISMATCH"], "effect_mismatch": cnt["EFFECT_MISMATCH"],
            "run_fail": cnt["RUN_FAIL"], "error_no_diagnostic": cnt["ERROR_NO_DIAGNOSTIC"],
            "crash": cnt["CRASH"], "hang": cnt["HANG"],
            "capture_fail": cnt["CAPTURE_FAIL"],
            "numeric_diff": sum(1 for r in prs if r["numeric_diff"]),
            "payload_differs": sum(1 for r in prs if r["payload_differs"]),
            "non_deterministic": sum(1 for r in prs if not r["deterministic"]),
        })
    write_csv("by_pipeline.csv", by_pipeline)

    judged = sorted({r["program"] for r in rows})
    prog_with_violation = sorted({r["program"] for r in rows if is_violation(r)})
    totals = {
        "programs_in_records": len(programs),
        "programs_invalid_reference": sum(1 for r in ref_rows if r["reference"] == "INVALID_REF"),
        "programs_judged": len(judged),
        "pipelines": len(pipeline_ids) - 1,
        "cells": len(rows),
        "outcomes": dict(collections.Counter(r["outcome"] for r in rows)),
        "refused_anchored": sum(1 for r in rows if r["outcome"] == "REFUSED" and r["anchored"] is True),
        "refused_unanchored": sum(1 for r in rows if r["outcome"] == "REFUSED" and r["anchored"] is not True),
        "refused_not_clean": sum(1 for r in rows if r["outcome"] == "REFUSED" and r["clean"] is False),
        "violation_cells": sum(1 for r in rows if is_violation(r)),
        "violation_cells_macro_stage": sum(1 for r in rows if is_violation(r) and r["macro_stage"]),
        "consistent_with_numeric_output_difference_within_1e-5": sum(1 for r in rows if r["numeric_diff"]),
        "consistent_with_payload_difference": sum(1 for r in rows if r["payload_differs"]),
        "non_deterministic_cells": sum(1 for r in rows if not r["deterministic"]),
        "programs_with_violation": len(prog_with_violation),
        "pipelines_with_violation": len({r["pipeline_id"] for r in rows if is_violation(r)}),
    }
    (out / "totals.json").write_text(json.dumps(totals, indent=1), encoding="utf-8")
    print(json.dumps(totals, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
