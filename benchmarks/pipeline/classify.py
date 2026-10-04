#!/usr/bin/env python3
"""Classifies pipeline-runner records against the "agree or refuse" contract.

The rules are frozen in CLASSIFIER-RULES.md (artifacts/x1a/ of the paper repository); this
file implements exactly them, and that document is the reference when the two disagree in
wording. Outcomes are decided from the records only (no manual overrides).

The zero-pass run (P0) defines a program's observable behaviour: exit code, the program's own
output, the files it writes, and the ordered effect trace with the effect payloads.

Programs (references.csv)
  INVALID_REF       a P0 repeat did not finish, reported an error or exited non-zero.
  NONDETERMINISTIC  the P0 repeats differ in their observation. Not judged; listed in
                    nondeterministic.csv.
  REFERENCE         judged.

Cells of the agree set under a native pipeline (invariance.csv). Every repeat is classified,
first matching rule wins, and the cell takes the worst repeat (WORST_ORDER):
  NOT_APPLICABLE    the pipeline names a module-contributed pass (tensor::fuse, onnx::export)
                    and the program does not import that module ("pass not found").
  HANG              the run was killed at the absolute cap.
  RESOURCE_LIMIT    the run finished, but after the time limit L = max(floor, k * P0 time)
                    (plan C-10). Not judged; `late_result` says what it would have been.
  CRASH             the process died from a signal.
  CAPTURE_FAIL      a diagnostic of stage parse/typecheck.
  REFUSED           a diagnostic of stage pass:<name>. Flags: `located` (the diagnostic has a
                    file and a non-empty range), `clean` (no output and no run-stage effect
                    before it). A refusal that is not clean is a violation.
  RUN_FAIL          a diagnostic of any other stage (run, internal).
  ERROR_NO_DIAGNOSTIC  non-zero exit without a structured diagnostic although P0 succeeded.
  VALUE_MISMATCH    exit code, output or a written .npy file differs from P0 beyond the
                    strict tolerance 1e-5; tagged `near` when within 1e-3.
  EFFECT_MISMATCH   the sequence of effect events differs, or an effect payload differs by
                    value (floats beyond 1e-5, anything else at all), or the set of written
                    files differs.
  UNDECIDED         values and effect sequence agree, but an effect payload could only be
                    compared by hash (no payload text, or truncated) and the hashes differ.
  MACRO_EXCEPTION   a mismatch against P0 on a pipeline containing std::macro, for a program
                    whose macro bodies have effects, that agrees with `std::macro std::nvm`.
  CONSISTENT        none of the above.

Cells of the agree set under an ONNX pipeline (same file, pipeline_kind = onnx): the same
rules up to ERROR_NO_DIAGNOSTIC, then LOAD_FAIL (checker or session), RUN_FAIL (ONNX Runtime),
VALUE_MISMATCH / CONSISTENT on the outputs of the written model (onnx_gate.py), UNDECIDED when
there is no oracle. Effect order is not observable on this branch.

Cells that must refuse (refusal.csv): the pipelines XU1, XU2, XO1 on the agree set and the
command lines of the must-refuse set (pipeline id PLAN):
  REFUSED_AS_EXPECTED     non-zero exit with a diagnostic of a stage other than run, before
                          any output or run-stage effect; `located` as above.
  ACCEPTED_SHOULD_REFUSE  exit 0.
  FAILED_AFTER_START      a diagnostic of stage run, or output / run-stage effects before the
                          diagnostic.
  ERROR_NO_DIAGNOSTIC, CRASH, HANG as above.

Usage: classify.py --records DIR/records [DIR2/records ...] --out DIR/summary
                   [--rules frozen|v1|v2|v3|...] [--time-factor 20] [--time-floor 10]
"""

from __future__ import annotations

import argparse
import collections
import csv
import dataclasses
import json
import re
import statistics
import sys
from pathlib import Path
from typing import Dict, List, Optional, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pipelines as pl  # noqa: E402

STRICT, NEAR = 1e-5, 1e-3
LABEL = "preliminary, container on a dev machine"

# Operators registered with Modifier::Macro (modules/autodiff/operators.cpp). They build a
# function value; `std::macro` evaluates them ahead of time by design. Identified by URI, not
# by `stage == "macro"`: without `std::macro` the same operators run with stage "run", and
# stage "macro" also covers the effects of user macro bodies, which are real effects.
MACRO_OPERATORS = {"autodiff:grad", "autodiff:value_and_grad", "autodiff:vjp"}
PRINT_OPERATORS = {":io/println", ":io/print"}
# Reported per program (references.csv), not used by any rule: operators that read the clock or
# draw random numbers. A program that prints a rounded time can pass the determinism check by
# chance.
CLOCK_OR_RANDOM = re.compile(r"^time:now$|rand|random")

# Worst first. A cell takes the worst outcome of its repeats.
WORST_ORDER = ["VALUE_MISMATCH", "EFFECT_MISMATCH", "CRASH", "HANG", "RUN_FAIL", "LOAD_FAIL",
               "ERROR_NO_DIAGNOSTIC", "CAPTURE_FAIL", "REFUSED_NOT_CLEAN", "RESOURCE_LIMIT", "UNDECIDED",
               "MACRO_EXCEPTION", "REFUSED", "CONSISTENT", "NOT_APPLICABLE"]
REFUSAL_ORDER = ["ACCEPTED_SHOULD_REFUSE", "FAILED_AFTER_START", "CRASH", "HANG", "ERROR_NO_DIAGNOSTIC",
                 "REFUSED_AS_EXPECTED"]
VIOLATIONS = {"HANG", "CRASH", "CAPTURE_FAIL", "RUN_FAIL", "LOAD_FAIL", "ERROR_NO_DIAGNOSTIC",
              "VALUE_MISMATCH", "EFFECT_MISMATCH"}
NOT_JUDGED = {"RESOURCE_LIMIT", "UNDECIDED", "MACRO_EXCEPTION"}


@dataclasses.dataclass(frozen=True)
class Rules:
    """One rule set. `frozen` is the rule set of CLASSIFIER-RULES.md; the others reproduce the
    earlier classifier versions and the single steps between them (history)."""
    name: str
    exclude_macro_ops: bool = True
    payload: str = "value"          # "hash-in-sequence" (v1) | "ignore" (v2, v3) | "value" (frozen)
    numeric_tolerance: bool = True
    error_no_diagnostic: bool = True
    c10: bool = True                # RESOURCE_LIMIT relative to the zero-pass run
    worst_repeat: bool = True       # cell = worst repeat (else: first repeat)
    determinism_payload: bool = True
    files: bool = True
    macro_exception: bool = True
    # Deviation D1 (recorded in CLASSIFIER-RULES.md after the first run under the frozen rules):
    # the payload text carries the static type of each argument (`Tensor=...`), and passes that
    # refine types change that text (`Tensor<float32, [2, 4]>=...`) without changing the value.
    # With this flag the type prefixes are removed before payloads are compared.
    payload_strip_types: bool = False


_TYPE_PREFIX = re.compile(r"(^|\|)[A-Za-z_][^=|]*=")
STRIP_PAYLOAD_TYPES = False  # set from the rule set in main()

RULESETS = {r.name: r for r in [
    Rules("frozen+D1", payload_strip_types=True),
    Rules("v1", exclude_macro_ops=False, payload="hash-in-sequence", numeric_tolerance=False,
          error_no_diagnostic=False, c10=False, worst_repeat=False, determinism_payload=True, files=False,
          macro_exception=False),
    Rules("v1+macro-ops", payload="hash-in-sequence", numeric_tolerance=False, error_no_diagnostic=False,
          c10=False, worst_repeat=False, determinism_payload=True, files=False, macro_exception=False),
    Rules("v2", payload="ignore", numeric_tolerance=False, error_no_diagnostic=False, c10=False,
          worst_repeat=False, determinism_payload=False, files=False, macro_exception=False),
    Rules("v2+tolerance", payload="ignore", error_no_diagnostic=False, c10=False, worst_repeat=False,
          determinism_payload=False, files=False, macro_exception=False),
    Rules("v3", payload="ignore", c10=False, worst_repeat=False, determinism_payload=False, files=False,
          macro_exception=False),
    Rules("v3+payload", c10=False, worst_repeat=False, determinism_payload=False, files=False,
          macro_exception=False),
    Rules("v3+payload+c10", worst_repeat=False, determinism_payload=False, files=False, macro_exception=False),
    Rules("v3+payload+c10+worst", determinism_payload=False, files=False, macro_exception=False),
    Rules("frozen"),
]}

# ---------------------------------------------------------------------------------------------
# Text and number comparison
# ---------------------------------------------------------------------------------------------

_NUM = re.compile(r"[-+]?(?:\d+\.\d*|\.\d+|\d+)(?:[eE][-+]?\d+)?")
_FLOAT_TYPED = re.compile(r"(?:^|\|)(?:float|double|f32|f64|float32|float64)=$")
_BENCH_MS = re.compile(r"\b(\w*_ms)=\S+")


def floats_close(x: float, y: float, tol: float) -> bool:
    if x != x or y != y:                 # NaN equals NaN, and nothing else
        return x != x and y != y
    if x == y:
        return True
    return abs(x - y) <= tol * max(1.0, abs(x))


def numeric_compare(ref: str, got: str, tol: float, typed: bool = False) -> bool:
    """True when the two texts are equal except for floating-point numbers within `tol`:
    the text outside the numbers must be identical, a number written without `.` or exponent
    on both sides is an integer and must be equal, any other number may differ by
    |a-b| <= tol * max(1, |a|). With `typed` (effect payloads), a number that directly follows
    a scalar float type prefix (`float=`) is compared as a float even when written like an
    integer."""
    if ref == got:
        return True
    if _NUM.sub("#", ref) != _NUM.sub("#", got):
        return False
    for ma, mb in zip(_NUM.finditer(ref), _NUM.finditer(got)):
        a, b = ma.group(), mb.group()
        if a == b:
            continue
        if not any(c in a + b for c in ".eE"):
            if not (typed and _FLOAT_TYPED.search(ref[max(0, ma.start() - 9):ma.start()])):
                return False  # integers must match exactly
        if not floats_close(float(a), float(b), tol):
            return False
    return True


def mask_text(text: str, mask: str) -> str:
    """Output masks declared by the corpus. `camel_bench_ms`: on lines that start with
    CAMEL_BENCH, the value of every key ending in `_ms` (a wall-clock measurement of the
    benchmark protocol) is replaced by `<ms>`."""
    if mask == "camel_bench_ms" and "CAMEL_BENCH" in text:
        return "\n".join(_BENCH_MS.sub(r"\1=<ms>", line) if "CAMEL_BENCH" in line else line
                         for line in text.split("\n"))
    return text


def errors_of(rec: dict) -> List[dict]:
    return [d for d in (rec.get("diagnostics") or []) if d.get("severity") == "Error"]


def located(diag: dict) -> bool:
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


# ---------------------------------------------------------------------------------------------
# Effects
# ---------------------------------------------------------------------------------------------

def events(rec: dict, rules: Rules) -> List[dict]:
    """The effect events of a run: every executed operator that is not registered pure, minus
    the macro operators."""
    evs = rec.get("effects", [])
    if rules.exclude_macro_ops:
        evs = [e for e in evs if e.get("uri") not in MACRO_OPERATORS]
    return evs


def effect_key(rec: dict, rules: Rules) -> list:
    if rules.payload == "hash-in-sequence":
        return [(e.get("uri", "?"), e.get("payload_sha1", "?")) for e in events(rec, rules)]
    return [e.get("uri", "?") for e in events(rec, rules)]


def payload_text(event: dict, mask: str) -> Optional[str]:
    """The comparable payload text of an event, or None when only the hash can be used."""
    text = event.get("payload")
    if not isinstance(text, str) or event.get("payload_truncated"):
        return None
    if STRIP_PAYLOAD_TYPES:
        text = _TYPE_PREFIX.sub(lambda m: m.group(1), text)
    return mask_text(text, mask) if event.get("uri") in PRINT_OPERATORS else text


def compare_payloads(ref: dict, rec: dict, rules: Rules, mask: str) -> Tuple[str, str, str]:
    """Compares the payloads of two runs whose effect sequences are equal. Returns
    (verdict, mode, detail): verdict equal | differ | undecided; mode text | hash | mixed | none."""
    a, b = events(ref, rules), events(rec, rules)
    verdict, detail, used = "equal", "", set()
    for i, (x, y) in enumerate(zip(a, b)):
        tx, ty = payload_text(x, mask), payload_text(y, mask)
        if tx is not None and ty is not None:
            used.add("text")
            if not numeric_compare(tx, ty, STRICT, typed=True):
                return "differ", "text" if used == {"text"} else "mixed", \
                    f"payload of effect #{i} ({x.get('uri')}): ref={tx[:60]!r} got={ty[:60]!r}"
            continue
        used.add("hash")
        if x.get("payload_sha1") == y.get("payload_sha1") and x.get("payload_sha1") is not None:
            continue
        px, py = x.get("payload"), y.get("payload")
        if STRIP_PAYLOAD_TYPES and isinstance(px, str) and isinstance(py, str):
            px = _TYPE_PREFIX.sub(lambda m: m.group(1), px)
            py = _TYPE_PREFIX.sub(lambda m: m.group(1), py)
        if isinstance(px, str) and isinstance(py, str):
            # Both truncated (or one): a difference inside the common prefix is a difference.
            n = min(len(px), len(py))
            cut_x, cut_y = px[:n].rsplit("|", 1)[0], py[:n].rsplit("|", 1)[0]
            if not numeric_compare(cut_x, cut_y, STRICT, typed=True) and _NUM.sub("#", cut_x) == _NUM.sub("#", cut_y):
                return "differ", "mixed", f"payload of effect #{i} ({x.get('uri')}) differs in the kept prefix"
        if verdict == "equal":
            verdict = "undecided"
            detail = f"payload of effect #{i} ({x.get('uri')}) differs by hash; no text to compare by value"
    mode = "none" if not used else ("text" if used == {"text"} else "hash" if used == {"hash"} else "mixed")
    return verdict, mode, detail


def has_macro_body_effects(recs: List[dict]) -> bool:
    return any(e.get("stage") == "macro" and e.get("uri") not in MACRO_OPERATORS
               for r in recs for e in r.get("effects", []))


# ---------------------------------------------------------------------------------------------
# Files
# ---------------------------------------------------------------------------------------------

def compare_files(ref: dict, rec: dict) -> Tuple[str, str, bool]:
    """(verdict, detail, sampled): verdict equal | set | value | near | other. Written .npy
    files are compared by value; other files by hash (`other`: reported, not an outcome)."""
    fa = {f["name"]: f for f in ref.get("files", []) if not f["name"].startswith("gate-")}
    fb = {f["name"]: f for f in rec.get("files", []) if not f["name"].startswith("gate-")}
    if set(fa) != set(fb):
        return "set", f"files written: ref={sorted(fa)} got={sorted(fb)}", False
    verdict, detail, sampled = "equal", "", False
    for name in sorted(fa):
        x, y = fa[name], fb[name]
        if x["sha1"] == y["sha1"]:
            continue
        nx, ny = x.get("npy"), y.get("npy")
        if not nx or not ny or nx.get("values", nx.get("sample")) is None:
            if verdict == "equal":
                verdict, detail = "other", f"file {name} differs (compared by hash only)"
            continue
        if nx["dtype"] != ny["dtype"] or nx["shape"] != ny["shape"]:
            return "value", f"file {name}: dtype/shape {nx['dtype']}{nx['shape']} vs {ny['dtype']}{ny['shape']}", sampled
        is_float = "f" in nx["dtype"]
        va, vb = nx.get("values"), ny.get("values")
        if va is None:
            sampled = True
            va = nx["sample"] + [nx["sum"], nx["abs_sum"]]
            vb = ny["sample"] + [ny["sum"], ny["abs_sum"]]
        for tol, label in ((STRICT, "value"), (NEAR, None)):
            ok = all((floats_close(p, q, tol) if is_float else p == q) for p, q in zip(va, vb))
            if ok:
                break
            if label:
                worst = max((abs(p - q) for p, q in zip(va, vb) if p == p and q == q), default=0.0)
                verdict, detail = "near", f"file {name}: max |diff| {worst:.3e} (within 1e-3)"
            else:
                return "value", detail.replace("(within 1e-3)", "(beyond 1e-3)"), sampled
    return verdict, detail, sampled


# ---------------------------------------------------------------------------------------------
# Observation, reference
# ---------------------------------------------------------------------------------------------

def observation(rec: dict, rules: Rules) -> tuple:
    """What two repeats of a run must have in common to count as the same behaviour."""
    diags = tuple((d.get("data", {}).get("name"), d.get("data", {}).get("stage")) for d in errors_of(rec))
    if not rules.determinism_payload:
        return (rec["timed_out"], rec["exit_code"], rec["output_sha1"], tuple(effect_key(rec, rules)), diags)
    if rules.payload == "hash-in-sequence":
        return (rec["timed_out"], rec["exit_code"], rec["output_sha1"], tuple(effect_key(rec, rules)), diags)
    mask = rec.get("output_mask", "")
    payloads = []
    for e in events(rec, rules):
        text = payload_text(e, mask)
        if text is not None:
            payloads.append(text)
        elif mask and e.get("uri") in PRINT_OPERATORS:
            payloads.append("<masked output, hash only>")
        else:
            payloads.append(e.get("payload_sha1", "?"))
    files = tuple((f["name"], f["sha1"]) for f in rec.get("files", []) if not f["name"].startswith("gate-")) \
        if rules.files else ()
    return (rec["timed_out"], rec["exit_code"], mask_text(rec["output"], mask),
            tuple(effect_key(rec, rules)), tuple(payloads), diags, files)


def classify_ref(recs: List[dict], rules: Rules) -> Tuple[str, str]:
    r = recs[0]
    if any(x.get("skipped") for x in recs):
        return "INVALID_REF", "reference was not run"
    if any(x["timed_out"] for x in recs):
        return "INVALID_REF", "reference timed out"
    for x in recs if rules.worst_repeat else recs[:1]:
        if errors_of(x):
            d = errors_of(x)[0]["data"]
            return "INVALID_REF", f"reference reports {d.get('name')} (stage {d.get('stage')})"
        if x["exit_code"] != 0:
            return "INVALID_REF", f"reference exit code {x['exit_code']}"
    if len({observation(x, rules) for x in recs}) > 1:
        if rules.determinism_payload and rules.payload == "value":
            return "NONDETERMINISTIC", nondeterminism_detail(recs, rules)
        return "INVALID_REF", "reference is non-deterministic across repeats"
    return "REFERENCE", ""


def nondeterminism_detail(recs: List[dict], rules: Rules) -> str:
    a = observation(recs[0], rules)
    for other in recs[1:]:
        b = observation(other, rules)
        names = ("timed_out", "exit code", "output", "effect sequence", "effect payloads", "diagnostics", "files")
        for name, x, y in zip(names, a, b):
            if x != y:
                extra = first_diff(x, y) if name == "output" else ""
                return f"repeats differ in {name}" + (f": {extra}" if extra else "")
    return "repeats differ"


def time_limit(rec: dict, ref_ms: Optional[float], factor: float, floor: float) -> Optional[float]:
    if rec.get("time_limit_s") is not None:
        return rec["time_limit_s"]
    if ref_ms is None:
        return None
    return max(floor, factor * ref_ms / 1000.0)


# ---------------------------------------------------------------------------------------------
# Cells of the agree set
# ---------------------------------------------------------------------------------------------

def new_info() -> dict:
    return {"outcome": "", "detail": "", "located": "", "clean": "", "diag_stage": "", "diag_name": "",
            "near": False, "numeric_diff": False, "payload_mode": "", "payload_differs": False,
            "late_result": "", "files_differ": False, "file_sampled": False, "onnx_oracle": "",
            "max_abs_err": "", "export_ran_program": False, "macro_effects_before_refusal": False}


def not_applicable(r: dict, errs: List[dict]) -> str:
    if errs and errs[0].get("data", {}).get("name", "").endswith("UnrecognizedGraphPass"):
        missing = [p for p in r["passes"] if p in pl.MODULE_PASSES and f"'{p}'" in errs[0].get("message", "")]
        if missing:
            return f"{missing[0]} needs module {pl.MODULE_PASSES[missing[0]]}"
    return ""


def is_crash(r: dict, errs: List[dict]) -> bool:
    code = r["exit_code"]
    return code is None or code < 0 or (code != 0 and not errs and code >= 128)


def run_stage_effects(r: dict) -> List[dict]:
    return [e for e in r.get("effects", []) if e.get("stage") == "run"]


def failure_class(r: dict, ref: dict, errs: List[dict], info: dict, rules: Rules) -> bool:
    """Rules CRASH .. ERROR_NO_DIAGNOSTIC, shared by native and ONNX cells. True when one applied."""
    if is_crash(r, errs):
        info.update(outcome="CRASH", detail=f"exit code {r['exit_code']}")
    elif errs:
        stage = info["diag_stage"]
        if stage in ("parse", "typecheck"):
            info.update(outcome="CAPTURE_FAIL", detail=errs[0].get("message", "")[:160])
        elif stage.startswith("pass:"):
            ran = run_stage_effects(r)
            info.update(outcome="REFUSED", detail=errs[0].get("message", "")[:160], located=located(errs[0]),
                        clean=(r["output"] == "" and not ran),
                        macro_effects_before_refusal=any(e.get("stage") == "macro" for e in r.get("effects", [])))
        else:
            info.update(outcome="RUN_FAIL", detail=errs[0].get("message", "")[:160], located=located(errs[0]))
    elif rules.error_no_diagnostic and r["exit_code"] != 0 and ref["exit_code"] == 0:
        info.update(outcome="ERROR_NO_DIAGNOSTIC",
                    detail=f"exit {r['exit_code']}; " + (r["output"].strip().splitlines() or [""])[-1][:160],
                    clean=(not run_stage_effects(r)))
    else:
        return False
    return True


def classify_native(ref: dict, r: dict, rules: Rules, limit_s: Optional[float]) -> dict:
    info = new_info()
    errs = errors_of(r)
    if errs:
        info["diag_stage"] = errs[0].get("data", {}).get("stage", "")
        info["diag_name"] = errs[0].get("data", {}).get("name", "")
    na = not_applicable(r, errs)
    if na:
        info.update(outcome="NOT_APPLICABLE", detail=na)
        return info
    if r.get("skipped"):
        info.update(outcome="NOT_APPLICABLE", detail=f"not run: {r['skipped']}")
        return info
    if r["timed_out"]:
        info.update(outcome="HANG", detail=f"killed after {r.get('kill_after_s') or r.get('timeout_s')} s")
        return info
    late = rules.c10 and limit_s is not None and r["wall_ms"] > limit_s * 1000.0
    if not failure_class(r, ref, errs, info, rules):
        compare_values_and_effects(ref, r, rules, info)
    if late:
        info.update(late_result=info["outcome"], outcome="RESOURCE_LIMIT",
                    detail=f"{r['wall_ms'] / 1000.0:.1f} s > limit {limit_s:.1f} s; would be {info['outcome']}"
                           + (f" ({info['detail']})" if info["detail"] else ""))
    return info


def compare_values_and_effects(ref: dict, r: dict, rules: Rules, info: dict) -> None:
    mask = ref.get("output_mask", "")
    out_ref, out_got = mask_text(ref["output"], mask), mask_text(r["output"], mask)
    same_exit = r["exit_code"] == ref["exit_code"]
    same_output = out_ref == out_got
    same_sequence = effect_key(r, rules) == effect_key(ref, rules)
    legacy = rules.payload != "value"

    # Values: exit code and output.
    value_ok = same_exit and same_output
    if same_exit and not same_output and rules.numeric_tolerance and numeric_compare(out_ref, out_got, STRICT) \
            and (same_sequence or not legacy):
        value_ok = True
        info.update(numeric_diff=True, detail=first_diff(out_ref, out_got))
    if not value_ok:
        reorder = sorted(out_got.splitlines()) == sorted(out_ref.splitlines())
        near = rules.numeric_tolerance and same_exit and numeric_compare(out_ref, out_got, NEAR)
        detail = f"exit {ref['exit_code']} -> {r['exit_code']}; " if not same_exit else ""
        info.update(outcome="VALUE_MISMATCH", near=bool(near),
                    detail=detail + ("same lines, different order; " if reorder else "")
                    + ("near (within 1e-3); " if near else "") + first_diff(out_ref, out_got))
        return
    # Values: written files.
    file_set_differs = False
    if rules.files and "files" in ref and "files" in r:
        verdict, detail, sampled = compare_files(ref, r)
        info["file_sampled"] = sampled
        if verdict in ("value", "near"):
            info.update(outcome="VALUE_MISMATCH", near=(verdict == "near"),
                        detail=("near (within 1e-3); " if verdict == "near" else "") + detail)
            return
        if verdict == "other":
            info["files_differ"] = True
        file_set_differs = verdict == "set"
        file_detail = detail
    # Effects: sequence.
    if not same_sequence:
        a, b = effect_key(ref, rules), effect_key(r, rules)
        at = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]), min(len(a), len(b)))
        info.update(outcome="EFFECT_MISMATCH",
                    detail=f"{len(a)} effect(s) in reference, {len(b)} here; first difference at #{at}: "
                           f"ref={a[at] if at < len(a) else '<end>'} got={b[at] if at < len(b) else '<end>'}")
        return
    if file_set_differs:
        info.update(outcome="EFFECT_MISMATCH", detail=file_detail)
        return
    # Effects: payloads.
    if rules.payload == "ignore":
        pa = [e.get("payload_sha1", "?") for e in events(ref, rules)]
        pb = [e.get("payload_sha1", "?") for e in events(r, rules)]
        info.update(outcome="CONSISTENT", payload_differs=pa != pb)
    elif rules.payload == "hash-in-sequence":
        info.update(outcome="CONSISTENT")
    else:
        verdict, mode, detail = compare_payloads(ref, r, rules, mask)
        info["payload_mode"] = mode
        if verdict == "differ":
            info.update(outcome="EFFECT_MISMATCH", payload_differs=True, detail=detail)
        elif verdict == "undecided":
            info.update(outcome="UNDECIDED", payload_differs=True, detail=detail)
        else:
            info.update(outcome="CONSISTENT")
    if info["outcome"] == "CONSISTENT" and not info["detail"]:
        ro = [e.get("origin") for e in events(ref, rules)]
        po = [e.get("origin") for e in events(r, rules)]
        if any(a and b and a != b for a, b in zip(ro, po)):
            info["detail"] = "effect origins differ"


def classify_onnx(ref: dict, r: dict, rules: Rules, limit_s: Optional[float]) -> dict:
    info = new_info()
    errs = errors_of(r)
    if errs:
        info["diag_stage"] = errs[0].get("data", {}).get("stage", "")
        info["diag_name"] = errs[0].get("data", {}).get("name", "")
    na = not_applicable(r, errs)
    if na or r.get("skipped"):
        info.update(outcome="NOT_APPLICABLE", detail=na or f"not run: {r['skipped']}")
        return info
    if r["timed_out"]:
        info.update(outcome="HANG", detail=f"killed after {r.get('kill_after_s')} s")
        return info
    late = rules.c10 and limit_s is not None and r["wall_ms"] > limit_s * 1000.0
    if not failure_class(r, ref, errs, info, rules):
        gate = r.get("onnx") or {}
        info["onnx_oracle"] = gate.get("oracle", "")
        lines = [x for x in r["output"].splitlines() if x.strip() and not x.startswith("onnx::export: wrote ")]
        info["export_ran_program"] = bool(lines) or bool(run_stage_effects(r))
        cmps = gate.get("comparisons", [])
        errors = [c.get("max_abs_err") for c in cmps if c.get("max_abs_err") is not None]
        info["max_abs_err"] = max(errors) if errors else ""
        if not gate.get("model_written"):
            info.update(outcome="RUN_FAIL", detail="exit 0 but no model was written")
        elif not gate.get("available", True):
            info.update(outcome="UNDECIDED", detail=f"ONNX gate not available: {gate.get('error', '')}"[:200])
        elif gate.get("checker_ok") is False or gate.get("load_ok") is False:
            info.update(outcome="LOAD_FAIL", detail=str(gate.get("error", ""))[:200])
        elif gate.get("run_ok") is False:
            info.update(outcome="RUN_FAIL", detail=str(gate.get("error", ""))[:200])
        elif gate.get("oracle", "none") == "none" or not cmps:
            info.update(outcome="UNDECIDED", detail=f"no oracle: {gate.get('error', '')}"[:200])
        elif all(c.get("within_strict") for c in cmps):
            info.update(outcome="CONSISTENT", detail=f"oracle {gate['oracle']}")
        else:
            near = all(c.get("within_near") for c in cmps)
            bad = next(c for c in cmps if not c.get("within_strict"))
            info.update(outcome="VALUE_MISMATCH", near=near,
                        detail=("near (within 1e-3); " if near else "") + f"oracle {gate['oracle']}; output "
                               f"{bad.get('output')}: max |diff| {bad.get('max_abs_err')}")
    if late:
        info.update(late_result=info["outcome"], outcome="RESOURCE_LIMIT",
                    detail=f"{r['wall_ms'] / 1000.0:.1f} s > limit {limit_s:.1f} s; would be {info['outcome']}")
    return info


def rank(info: dict) -> int:
    name = info["outcome"]
    if name == "REFUSED" and info["clean"] is False:
        name = "REFUSED_NOT_CLEAN"
    return WORST_ORDER.index(name)


def classify_cell(ref: dict, recs: List[dict], rules: Rules, limit_of) -> dict:
    kind = recs[0].get("pipeline_kind") or pl.kind(recs[0]["pipeline_id"], recs[0]["passes"])
    fn = classify_onnx if kind == "onnx" else classify_native
    infos = [fn(ref, r, rules, limit_of(r)) for r in (recs if rules.worst_repeat else recs[:1])]
    info = dict(min(infos, key=rank))
    if rules.worst_repeat and any(i["outcome"] == "NOT_APPLICABLE" for i in infos):
        # Applicability is a static property of (program, pipeline): the pass does not exist
        # for this program. A repeat that timed out before reaching "pass not found" (the
        # name is only looked up when the pass is about to run) does not make the cell a HANG.
        info = dict(next(i for i in infos if i["outcome"] == "NOT_APPLICABLE"))
    info["deterministic"] = len({observation(x, rules) for x in recs}) == 1 and len({i["outcome"] for i in infos}) == 1
    info["repeat_outcomes"] = "/".join(i["outcome"] for i in infos)
    return info


# ---------------------------------------------------------------------------------------------
# Cells that must refuse
# ---------------------------------------------------------------------------------------------

def classify_refusal(r: dict) -> dict:
    info = {"outcome": "", "detail": "", "located": "", "diag_stage": "", "diag_name": "",
            "matches_expected_diagnostic": "", "matches_expected_line": "",
            "macro_effects_before_refusal": any(e.get("stage") == "macro" for e in r.get("effects", []))}
    errs = errors_of(r)
    expect = r.get("expect") or {}
    if errs:
        d = errs[0]
        info.update(diag_stage=d.get("data", {}).get("stage", ""), diag_name=d.get("data", {}).get("name", ""),
                    located=located(d), detail=d.get("message", "")[:160])
        if expect.get("diagnostic"):
            info["matches_expected_diagnostic"] = info["diag_name"] == expect["diagnostic"]
        if expect.get("line") not in ("", None):
            info["matches_expected_line"] = d.get("range", {}).get("start", {}).get("line", -2) + 1 == expect["line"]
    started = bool(r["output"].strip()) or bool(run_stage_effects(r))
    if r.get("skipped"):
        info.update(outcome="HANG", detail=f"not run: {r['skipped']}")
    elif r["timed_out"]:
        info.update(outcome="HANG", detail=f"killed after {r.get('kill_after_s')} s")
    elif is_crash(r, errs):
        info.update(outcome="CRASH", detail=f"exit code {r['exit_code']}")
    elif r["exit_code"] == 0 and not errs:
        info.update(outcome="ACCEPTED_SHOULD_REFUSE",
                    detail="exit 0" + ("; the program ran" if started else "; nothing ran"))
    elif errs and info["diag_stage"] != "run" and not started:
        info.update(outcome="REFUSED_AS_EXPECTED")
    elif errs:
        info.update(outcome="FAILED_AFTER_START",
                    detail=(f"stage {info['diag_stage']}; " + ("output/effects before the diagnostic; " if started else "")
                            + info["detail"])[:200])
    elif started:
        info.update(outcome="FAILED_AFTER_START", detail=f"exit {r['exit_code']} without a diagnostic, after output/effects")
    else:
        info.update(outcome="ERROR_NO_DIAGNOSTIC", detail=f"exit {r['exit_code']} without a diagnostic")
    return info


# ---------------------------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------------------------

def pipeline_sort_key(s: str):
    return (s[0], int("".join(c for c in s if c.isdigit()) or 0), s)


def load_cells(record_dirs: List[str]) -> Dict[Tuple[str, str], List[dict]]:
    cells: Dict[Tuple[str, str], List[dict]] = collections.defaultdict(list)
    for d in record_dirs:
        for path in sorted(Path(d).rglob("*.json")):
            rec = json.loads(path.read_text(encoding="utf-8"))
            cells[(rec["program_id"], rec["pipeline_id"])].append(rec)
    for recs in cells.values():
        recs.sort(key=lambda r: r["repeat"])
    return cells


def classify_all(cells, rules: Rules, factor: float, floor: float):
    programs = sorted({p for p, _ in cells})
    pipeline_ids = sorted({q for _, q in cells}, key=pipeline_sort_key)
    rows: List[dict] = []
    ref_rows: List[dict] = []
    refusal_rows: List[dict] = []
    for prog in programs:
        any_rec = next(recs[0] for (p, _), recs in cells.items() if p == prog)
        if any_rec.get("role", "agree") == "must_refuse":
            for pid in pipeline_ids:
                if (prog, pid) in cells:
                    refusal_rows.append(refusal_row(cells[(prog, pid)]))
            continue
        if (prog, pl.REFERENCE) not in cells:
            continue
        ref_recs = cells[(prog, pl.REFERENCE)]
        ref_outcome, ref_detail = classify_ref(ref_recs, rules)
        ref = ref_recs[0]
        walls = [x["wall_ms"] for x in ref_recs if not x["timed_out"]]
        ref_ms = statistics.median(walls) if walls else None
        ref_rows.append({"program": ref["program"], "program_id": prog, "corpus": ref["corpus"],
                         "origin": ref["origin"], "reference": ref_outcome, "detail": ref_detail,
                         "n_effects": len(ref.get("effects", [])), "wall_ms": ref["wall_ms"],
                         "time_limit_s": round(max(floor, factor * ref_ms / 1000.0), 3) if ref_ms is not None else "",
                         "payload_text_in_trace": any("payload" in e for e in ref.get("effects", [])),
                         "clock_or_random_operators": " ".join(sorted({e.get("uri", "") for e in ref.get("effects", [])
                                                                       if CLOCK_OR_RANDOM.search(e.get("uri", ""))})),
                         "output_has_nan_or_inf": bool(re.search(r"(?<![A-Za-z])[-+]?(?:nan|inf)(?![A-Za-z])",
                                                                 ref.get("output", ""), re.I))})
        for pid in pipeline_ids:
            if (prog, pid) in cells and pl.kind(pid, cells[(prog, pid)][0]["passes"]) == "refuse":
                refusal_rows.append(refusal_row(cells[(prog, pid)]))
        if ref_outcome != "REFERENCE":
            continue
        macro_recs = cells.get((prog, pl.MACRO_REFERENCE))
        macro_program = rules.macro_exception and has_macro_body_effects(
            [r for (p, _), recs in cells.items() if p == prog for r in recs[:1]])
        limit_of = lambda r: time_limit(r, ref_ms, factor, floor)  # noqa: E731
        for pid in pipeline_ids:
            if pid == pl.REFERENCE or (prog, pid) not in cells:
                continue
            recs = cells[(prog, pid)]
            kind = recs[0].get("pipeline_kind") or pl.kind(pid, recs[0]["passes"])
            if kind == "refuse":
                continue
            info = classify_cell(ref, recs, rules, limit_of)
            if (macro_program and kind == "native" and "std::macro" in recs[0]["passes"]
                    and info["outcome"] in ("VALUE_MISMATCH", "EFFECT_MISMATCH") and macro_recs
                    and classify_ref(macro_recs, rules)[0] == "REFERENCE"):
                against = classify_cell(macro_recs[0], recs, rules, limit_of)
                if against["outcome"] == "CONSISTENT":
                    info.update(outcome="MACRO_EXCEPTION",
                                detail=f"differs from P0 ({info['outcome']}: {info['detail']}"[:160]
                                       + f"); agrees with {pl.MACRO_REFERENCE}")
            rows.append({"program": recs[0]["program"], "program_id": prog, "corpus": recs[0]["corpus"],
                         "origin": recs[0]["origin"], "pipeline_id": pid, "pipeline_kind": kind,
                         "passes": " ".join(recs[0]["passes"]), "outcome": info["outcome"],
                         "repeat_outcomes": info["repeat_outcomes"], "deterministic": info["deterministic"],
                         "located": info["located"], "clean": info["clean"], "near": info["near"],
                         "numeric_diff": info["numeric_diff"], "payload_mode": info["payload_mode"],
                         "payload_differs": info["payload_differs"], "late_result": info["late_result"],
                         "files_differ": info["files_differ"], "file_sampled": info["file_sampled"],
                         "macro_program": macro_program, "onnx_oracle": info["onnx_oracle"],
                         "max_abs_err": info["max_abs_err"], "export_ran_program": info["export_ran_program"],
                         "effects_observable": kind != "onnx",
                         "macro_effects_before_refusal": info["macro_effects_before_refusal"],
                         "diag_stage": info["diag_stage"], "diag_name": info["diag_name"],
                         "max_wall_ms": max(r["wall_ms"] for r in recs), "detail": info["detail"]})
    return rows, ref_rows, refusal_rows, programs, pipeline_ids


def refusal_row(recs: List[dict]) -> dict:
    infos = [classify_refusal(r) for r in recs]
    info = min(infos, key=lambda i: REFUSAL_ORDER.index(i["outcome"]))
    r = recs[0]
    expect = r.get("expect") or {}
    source = r.get("expect_source") or pl.EXPECT_REFUSE.get(r["pipeline_id"], {}).get("source", "")
    return {"program": r["program"], "program_id": r["program_id"], "origin": r.get("origin", ""),
            "set": "must-refuse program" if r.get("role") == "must_refuse" else "must-refuse pipeline",
            "pipeline_id": r["pipeline_id"], "passes": " ".join(r["passes"]), "outcome": info["outcome"],
            "repeat_outcomes": "/".join(i["outcome"] for i in infos), "located": info["located"],
            "diag_stage": info["diag_stage"], "diag_name": info["diag_name"],
            "expected_diagnostic": expect.get("diagnostic", ""),
            "matches_expected_diagnostic": info["matches_expected_diagnostic"],
            "matches_expected_line": info["matches_expected_line"],
            "macro_effects_before_refusal": info["macro_effects_before_refusal"],
            "expectation_source": source, "detail": info["detail"]}


def is_violation(row: dict) -> bool:
    return row["outcome"] in VIOLATIONS or (row["outcome"] == "REFUSED" and row["clean"] is False)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--records", required=True, nargs="+", help="one or more records directories (shards)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--rules", default="frozen", choices=sorted(RULESETS))
    ap.add_argument("--time-factor", type=float, default=20.0)
    ap.add_argument("--time-floor", type=float, default=10.0)
    args = ap.parse_args()
    rules = RULESETS[args.rules]
    global STRIP_PAYLOAD_TYPES
    STRIP_PAYLOAD_TYPES = rules.payload_strip_types

    cells = load_cells(args.records)
    rows, ref_rows, refusal_rows, programs, pipeline_ids = classify_all(cells, rules, args.time_factor, args.time_floor)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    def write_csv(name: str, data: List[dict]) -> None:
        if not data:
            (out / name).write_text("", encoding="utf-8")
            return
        with open(out / name, "w", newline="", encoding="utf-8") as fh:
            w = csv.DictWriter(fh, fieldnames=list(data[0].keys()))
            w.writeheader()
            w.writerows(data)

    write_csv("references.csv", ref_rows)
    write_csv("nondeterministic.csv", [r for r in ref_rows if r["reference"] == "NONDETERMINISTIC"
                                       or "non-deterministic" in r["detail"]])
    write_csv("invariance.csv", rows)
    write_csv("refusal.csv", refusal_rows)
    write_csv("violations.csv", [r for r in rows if is_violation(r) or not r["deterministic"]])
    write_csv("not_judged.csv", [r for r in rows if r["outcome"] in NOT_JUDGED])

    by_pipeline: List[dict] = []
    for pid in pipeline_ids:
        prs = [r for r in rows if r["pipeline_id"] == pid]
        if not prs:
            continue
        cnt = collections.Counter(r["outcome"] for r in prs)
        applicable = len(prs) - cnt["NOT_APPLICABLE"]
        refused = [r for r in prs if r["outcome"] == "REFUSED"]
        accepted = applicable - len(refused)
        judged = accepted - sum(cnt[o] for o in NOT_JUDGED)
        viol = [r for r in prs if is_violation(r)]
        by_pipeline.append({
            "pipeline_id": pid, "pipeline_kind": prs[0]["pipeline_kind"], "passes": prs[0]["passes"],
            "programs": len(prs), "not_applicable": cnt["NOT_APPLICABLE"], "applicable": applicable,
            "accepted": accepted,
            "acceptance_rate": round(accepted / applicable, 4) if applicable else "",
            "accepted_and_judged": judged, "consistent": cnt["CONSISTENT"],
            "agreement_among_accepted": round(cnt["CONSISTENT"] / judged, 4) if judged else "",
            "refused": len(refused),
            "refused_located": sum(1 for r in refused if r["located"] is True),
            "refused_unlocated": sum(1 for r in refused if r["located"] is not True),
            "refused_not_clean": sum(1 for r in refused if r["clean"] is False),
            "violations": len(viol),
            "value_mismatch": cnt["VALUE_MISMATCH"],
            "value_mismatch_near": sum(1 for r in prs if r["outcome"] == "VALUE_MISMATCH" and r["near"]),
            "effect_mismatch": cnt["EFFECT_MISMATCH"],
            "run_fail": cnt["RUN_FAIL"], "load_fail": cnt["LOAD_FAIL"],
            "error_no_diagnostic": cnt["ERROR_NO_DIAGNOSTIC"],
            "crash": cnt["CRASH"], "hang": cnt["HANG"], "capture_fail": cnt["CAPTURE_FAIL"],
            "resource_limit": cnt["RESOURCE_LIMIT"], "undecided": cnt["UNDECIDED"],
            "macro_exception": cnt["MACRO_EXCEPTION"],
            "numeric_diff": sum(1 for r in prs if r["numeric_diff"]),
            "payload_differs": sum(1 for r in prs if r["payload_differs"]),
            "non_deterministic": sum(1 for r in prs if not r["deterministic"]),
        })
    write_csv("by_pipeline.csv", by_pipeline)

    refusal_by: List[dict] = []
    for key in sorted({(r["set"], r["pipeline_id"]) for r in refusal_rows}):
        rs = [r for r in refusal_rows if (r["set"], r["pipeline_id"]) == key]
        cnt = collections.Counter(r["outcome"] for r in rs)
        ok = [r for r in rs if r["outcome"] == "REFUSED_AS_EXPECTED"]
        refusal_by.append({"set": key[0], "pipeline_id": key[1],
                           "passes": rs[0]["passes"] if key[1] != "PLAN" else "(per entry)", "cells": len(rs),
                           "refused_as_expected": len(ok),
                           "refused_located": sum(1 for r in ok if r["located"] is True),
                           "refused_unlocated": sum(1 for r in ok if r["located"] is not True),
                           "accepted_should_refuse": cnt["ACCEPTED_SHOULD_REFUSE"],
                           "failed_after_start": cnt["FAILED_AFTER_START"],
                           "error_no_diagnostic": cnt["ERROR_NO_DIAGNOSTIC"],
                           "crash": cnt["CRASH"], "hang": cnt["HANG"]})
    write_csv("refusal_by_pipeline.csv", refusal_by)

    refs = collections.Counter(r["reference"] for r in ref_rows)
    judged_programs = sorted({r["program_id"] for r in rows})
    totals = {
        "label": LABEL,
        "rules": rules.name,
        "time_limit": {"factor": args.time_factor, "floor_s": args.time_floor},
        "programs_in_records": len(programs),
        "programs_agree_set": len(ref_rows),
        "programs_invalid_reference": refs["INVALID_REF"],
        "programs_nondeterministic": refs["NONDETERMINISTIC"],
        "programs_judged": len(judged_programs),
        "programs_judged_by_origin": dict(collections.Counter(r["origin"] for r in ref_rows if r["reference"] == "REFERENCE")),
        "pipelines": len({r["pipeline_id"] for r in rows}),
        "cells": len(rows),
        "cells_by_kind": dict(collections.Counter(r["pipeline_kind"] for r in rows)),
        "outcomes": dict(collections.Counter(r["outcome"] for r in rows)),
        "outcomes_native": dict(collections.Counter(r["outcome"] for r in rows if r["pipeline_kind"] == "native")),
        "outcomes_onnx": dict(collections.Counter(r["outcome"] for r in rows if r["pipeline_kind"] == "onnx")),
        "onnx_oracles": dict(collections.Counter(r["onnx_oracle"] for r in rows if r["pipeline_kind"] == "onnx" and r["onnx_oracle"])),
        "refused_located": sum(1 for r in rows if r["outcome"] == "REFUSED" and r["located"] is True),
        "refused_unlocated": sum(1 for r in rows if r["outcome"] == "REFUSED" and r["located"] is not True),
        "refused_not_clean": sum(1 for r in rows if r["outcome"] == "REFUSED" and r["clean"] is False),
        "violation_cells": sum(1 for r in rows if is_violation(r)),
        "not_judged_cells": sum(1 for r in rows if r["outcome"] in NOT_JUDGED),
        "resource_limit_late_results": dict(collections.Counter(r["late_result"] for r in rows if r["outcome"] == "RESOURCE_LIMIT")),
        "payload_modes": dict(collections.Counter(r["payload_mode"] for r in rows if r["payload_mode"])),
        "consistent_with_numeric_output_difference_within_1e-5": sum(1 for r in rows if r["numeric_diff"] and r["outcome"] == "CONSISTENT"),
        "consistent_with_payload_hash_difference_not_compared": sum(1 for r in rows if r["payload_differs"] and r["outcome"] == "CONSISTENT"),
        "non_deterministic_cells": sum(1 for r in rows if not r["deterministic"]),
        "programs_with_violation": len({r["program_id"] for r in rows if is_violation(r)}),
        "pipelines_with_violation": len({r["pipeline_id"] for r in rows if is_violation(r)}),
        "must_refuse_cells": len(refusal_rows),
        "must_refuse_outcomes": {k: dict(collections.Counter(r["outcome"] for r in refusal_rows if r["set"] == k))
                                 for k in sorted({r["set"] for r in refusal_rows})},
        "must_refuse_refused_located": sum(1 for r in refusal_rows if r["outcome"] == "REFUSED_AS_EXPECTED" and r["located"] is True),
        "must_refuse_refused_unlocated": sum(1 for r in refusal_rows if r["outcome"] == "REFUSED_AS_EXPECTED" and r["located"] is not True),
    }
    (out / "totals.json").write_text(json.dumps(totals, indent=1), encoding="utf-8")
    print(json.dumps(totals, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
