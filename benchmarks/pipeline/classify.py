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

Deviations from the frozen rules (CLASSIFIER-RULES.md section 12; each is a flag of a rule set,
so `--rules frozen` and `--rules frozen+D1` still give the earlier classifications):
  D1  type prefixes are removed from payload text before comparing.
  D2  an unlocated refusal is a violation of the refusal clause (C3) and its own outcome:
      REFUSED_LOCATED / REFUSED_UNLOCATED / REFUSED_NOT_CLEAN, in the agree set and in the
      must-refuse set.
  D3  (no rule change) finishing after the relative limit is RESOURCE_LIMIT, reported apart and
      not a violation; a run killed at the cap is HANG, a violation.
  D4  programs on the static exclusion list (exclusions.json) are EXCLUDED and not judged; the
      determinism check stays and reports programs it catches that are not on the list.
  D5  must-refuse programs whose plan expects a run-time failure are not in the must-refuse set.
  D6  (no rule of its own) benchmark models are judged through the timing mask and D7.
  D7  an event of a file-writing operator whose payload text is truncated is compared through
      the content of the files the run wrote, not through the truncated text or its hash.
  D8  (no rule change) XU2, the unknown pass after a scheduler, stays in the must-refuse set.
  D9  reporting: contract class per cell, signatures.csv (cause groups), resource.csv,
      excluded.csv; compare_runs.py compares two runs on their common cells.
  D10 PROPOSED, not an owner ruling and NOT in the default rule set: a large `.npy` file is
      judged by the comparison of all its elements that run.py records (`npy.vs_ref`) instead
      of the 4 096-element sample with the sum and the sum of absolute values. Select it with
      `--rules frozen+D1-D9+D10`.
The default rule set is the newest one (DEFAULT_RULES); the rule set in force is printed.

Usage: classify.py --records DIR/records [DIR2/records ...] --out DIR/summary
                   [--rules frozen+D1-D9|frozen+D1|frozen|frozen+D1+D4+...|v1|v2|v3|...]
                   [--exclusions exclusions.json] [--corpus corpus.json]
                   [--time-factor 20] [--time-floor 10]
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
# D4, second line of defence: a program whose zero-pass trace has one of these operators must be
# either on the exclusion list or on its reviewed list; otherwise it is reported.
CLOCK_OR_RANDOM_V2 = re.compile(r"^time:|rand|random")
# D7: operators that write a file. Their payload holds the per-run path and the printed value;
# when that text is truncated the event is compared through the files the run wrote.
FILE_WRITING = re.compile(r"^(tensor:save_npy|nn:save_params|onnx:export_model|fs:.*write.*)$")
HERE = Path(__file__).resolve().parent

# Worst first. A cell takes the worst outcome of its repeats. REFUSED_UNLOCATED is ranked only
# under D2 (see `rank`); the three REFUSED_* names are what a REFUSED cell is called under D2.
WORST_ORDER = ["VALUE_MISMATCH", "EFFECT_MISMATCH", "CRASH", "HANG", "RUN_FAIL", "LOAD_FAIL",
               "ERROR_NO_DIAGNOSTIC", "CAPTURE_FAIL", "REFUSED_NOT_CLEAN", "REFUSED_UNLOCATED",
               "RESOURCE_LIMIT", "UNDECIDED",
               "MACRO_EXCEPTION", "REFUSED", "CONSISTENT", "NOT_APPLICABLE"]
REFUSAL_ORDER = ["ACCEPTED_SHOULD_REFUSE", "FAILED_AFTER_START", "CRASH", "HANG", "ERROR_NO_DIAGNOSTIC",
                 "REFUSED_UNLOCATED", "REFUSED_AS_EXPECTED", "REFUSED_LOCATED"]
VIOLATIONS = {"HANG", "CRASH", "CAPTURE_FAIL", "RUN_FAIL", "LOAD_FAIL", "ERROR_NO_DIAGNOSTIC",
              "VALUE_MISMATCH", "EFFECT_MISMATCH"}
NOT_JUDGED = {"RESOURCE_LIMIT", "UNDECIDED", "MACRO_EXCEPTION"}
REFUSED_OUTCOMES = {"REFUSED", "REFUSED_LOCATED", "REFUSED_UNLOCATED", "REFUSED_NOT_CLEAN"}
# Must-refuse cells that conform to the refusal clause (everything else in that set violates it).
REFUSAL_CONFORMING = {"REFUSED_AS_EXPECTED", "REFUSED_LOCATED"}
REFUSAL_REFUSED = {"REFUSED_AS_EXPECTED", "REFUSED_LOCATED", "REFUSED_UNLOCATED"}

# The outcome classes of the paper's criterion (04-criterion.tex) and the outcomes they hold.
CONTRACT_CLASS = {
    "CONSISTENT": "agree",
    "REFUSED_LOCATED": "refused_located",
    "REFUSED_UNLOCATED": "refused_unlocated",
    "REFUSED_NOT_CLEAN": "refused_after_start",
    "CRASH": "run_time_error", "HANG": "run_time_error", "RUN_FAIL": "run_time_error",
    "LOAD_FAIL": "run_time_error", "ERROR_NO_DIAGNOSTIC": "run_time_error",
    "CAPTURE_FAIL": "run_time_error",
    "VALUE_MISMATCH": "silent_divergence", "EFFECT_MISMATCH": "silent_divergence",
    "RESOURCE_LIMIT": "resource_limit",
    "UNDECIDED": "not_judged", "MACRO_EXCEPTION": "not_judged",
    "NOT_APPLICABLE": "not_applicable",
}


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
    # Deviations D2..D9 (owner's provisional rulings after the after-run; CLASSIFIER-RULES.md section 12).
    unlocated_refusal_violation: bool = False   # D2
    exclusion_list: bool = False                # D4
    must_refuse_static_only: bool = False       # D5
    file_ops_by_content: bool = False           # D7
    report_v2: bool = False                     # D9 (also carries the reporting part of D3)
    npy_full_comparison: bool = False           # D10 (proposed; not in the default rule set)


# Deviation -> the flag it sets (None: a recorded ruling that changes no rule of the classifier).
DEVIATIONS = {
    "D1": ("payload_strip_types", "type prefixes removed from payload text"),
    "D2": ("unlocated_refusal_violation", "unlocated refusal = violation of the refusal clause (C3), own class"),
    "D3": (None, "late finish = RESOURCE_LIMIT, reported, not a violation; hard kill = HANG, violation (unchanged)"),
    "D4": ("exclusion_list", "static exclusion list (exclusions.json); determinism check reports additional offenders"),
    "D5": ("must_refuse_static_only", "expected run-time failures are not in the must-refuse set"),
    "D6": (None, "benchmark models judged through the timing mask and D7 (no rule of its own)"),
    "D7": ("file_ops_by_content", "file-writing operators with a truncated payload compared through written content"),
    "D8": (None, "XU2 (unknown pass after a scheduler) stays in the must-refuse set (unchanged)"),
    "D9": ("report_v2", "contract classes, violation signatures, resource and exclusion tables"),
    "D10": ("npy_full_comparison", "PROPOSED, not ruled: large .npy files judged on all elements (run.py's "
                                   "vs_ref) instead of the sample with its sums"),
}
# The deviations of the newest rule set. D10 is not among them: it is a proposal of the harness
# maintainer that removes violation cells and waits for the owner's decision.
NEWEST_DEVIATIONS = ["D1", "D2", "D3", "D4", "D5", "D6", "D7", "D8", "D9"]
NEWEST = "frozen+D1-D9"
DEFAULT_RULES = NEWEST

_TYPE_PREFIX = re.compile(r"(^|\|)[A-Za-z_][^=|]*=")
STRIP_PAYLOAD_TYPES = False  # set from the rule set in classify_all()
ACTIVE: Optional["Rules"] = None  # the rule set in force, set in classify_all()

RULESETS = {r.name: r for r in [
    Rules(NEWEST, payload_strip_types=True, unlocated_refusal_violation=True, exclusion_list=True,
          must_refuse_static_only=True, file_ops_by_content=True, report_v2=True),
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


def resolve_rules(name: str) -> Rules:
    """A named rule set, or `frozen+Dk+Dm...`: the frozen rules with exactly those deviations
    (used to count what one deviation reclassifies, and to revert one)."""
    if name in RULESETS:
        return RULESETS[name]
    parts = []
    for token in name.split("+"):
        span = re.fullmatch(r"D(\d+)-D(\d+)", token)       # frozen+D1-D9+D10
        parts += [f"D{i}" for i in range(int(span.group(1)), int(span.group(2)) + 1)] if span else [token]
    if parts[0] != "frozen" or not parts[1:] or any(p not in DEVIATIONS for p in parts[1:]):
        raise SystemExit(f"classify.py: unknown rule set '{name}'; known: {sorted(RULESETS)} "
                         f"or frozen+Dk+... with Dk in {sorted(DEVIATIONS)}")
    flags = {DEVIATIONS[p][0]: True for p in parts[1:] if DEVIATIONS[p][0]}
    rules = Rules(name, **flags)
    _NAMED_DEVIATIONS[name] = parts[1:]
    return rules


_NAMED_DEVIATIONS: Dict[str, List[str]] = {}


def deviations_of(rules: Rules) -> List[str]:
    """The deviations in force under a rule set (those without a flag count as in force under
    the newest rule set only, where they are recorded)."""
    if rules.name == NEWEST:
        return list(NEWEST_DEVIATIONS)
    if rules.name.startswith("frozen+"):
        named = _NAMED_DEVIATIONS.get(rules.name, rules.name.split("+"))
        return [d for d in sorted(DEVIATIONS, key=lambda d: int(d[1:]))
                if (DEVIATIONS[d][0] and getattr(rules, DEVIATIONS[d][0])) or d in named]
    return []

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


def payload_mode_name(used: set) -> str:
    if not used:
        return "none"
    if "files" in used:
        return "+".join(sorted(used))          # D7: files | files+text | files+hash | files+hash+text
    return "text" if used == {"text"} else "hash" if used == {"hash"} else "mixed"


def compare_payloads(ref: dict, rec: dict, rules: Rules, mask: str, files_state: str = "") -> Tuple[str, str, str]:
    """Compares the payloads of two runs whose effect sequences are equal. Returns
    (verdict, mode, detail): verdict equal | differ | undecided; mode text | hash | mixed | none,
    or, under D7, a `+`-joined list that contains `files`.

    D7: an event of a file-writing operator whose payload text is not complete on both sides is
    not compared through that text or its hash (the text is cut before the path, and the hash
    covers the per-run path): `files_state` says how the files the two runs wrote compare.
    `equal` makes the event equal; anything else leaves the cell UNDECIDED with that reason."""
    a, b = events(ref, rules), events(rec, rules)
    verdict, detail, used = "equal", "", set()
    for i, (x, y) in enumerate(zip(a, b)):
        tx, ty = payload_text(x, mask), payload_text(y, mask)
        if tx is not None and ty is not None:
            used.add("text")
            if not numeric_compare(tx, ty, STRICT, typed=True):
                return "differ", payload_mode_name(used), \
                    f"payload of effect #{i} ({x.get('uri')}): ref={tx[:60]!r} got={ty[:60]!r}"
            continue
        if rules.file_ops_by_content and FILE_WRITING.match(x.get("uri", "")):
            used.add("files")
            if files_state != "equal" and verdict == "equal":
                verdict = "undecided"
                detail = (f"payload of effect #{i} ({x.get('uri')}) is truncated and the written content "
                          f"could not be compared: {files_state or 'no file digests in the record'}")
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
                return "differ", "mixed" if "files" not in used else payload_mode_name(used), \
                    f"payload of effect #{i} ({x.get('uri')}) differs in the kept prefix"
        if verdict == "equal":
            verdict = "undecided"
            detail = f"payload of effect #{i} ({x.get('uri')}) differs by hash; no text to compare by value"
    return verdict, payload_mode_name(used), detail


def has_macro_body_effects(recs: List[dict]) -> bool:
    return any(e.get("stage") == "macro" and e.get("uri") not in MACRO_OPERATORS
               for r in recs for e in r.get("effects", []))


# ---------------------------------------------------------------------------------------------
# Files
# ---------------------------------------------------------------------------------------------

def compare_files(ref: dict, rec: dict, by_content: bool = False, full_arrays: bool = False) -> Tuple[str, str, bool]:
    """(verdict, detail, sampled): verdict equal | set | value | near | other. Written .npy
    files are compared by value; other files by hash (`other`: reported, not an outcome).

    With `by_content` (D7) the other files are compared by content where the record has it:
    a text file through its recorded text (numbers per the tolerances), an exported model through
    `vs_ref`, the comparison run.py made with the zero-pass run's file of the same name on ONNX
    Runtime. A file that differs by hash and has neither stays `other`."""
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
        if by_content and not (nx and ny):
            tx, ty, cmp = x.get("text"), y.get("text"), y.get("vs_ref")
            if isinstance(tx, str) and isinstance(ty, str):
                if numeric_compare(tx, ty, STRICT):
                    continue
                if not numeric_compare(tx, ty, NEAR):
                    return "value", f"file {name}: {first_diff(tx, ty)}", sampled
                if verdict in ("equal", "other"):
                    verdict, detail = "near", f"file {name}: text differs (within 1e-3): {first_diff(tx, ty)}"
                continue
            if isinstance(cmp, dict) and cmp.get("equivalent") is True:
                continue
            if isinstance(cmp, dict) and cmp.get("equivalent") is False:
                if not cmp.get("within_near"):
                    return "value", f"file {name}: model differs from the zero-pass run's on ONNX Runtime " \
                                    f"(beyond 1e-3): {cmp.get('detail', '')}"[:200], sampled
                if verdict in ("equal", "other"):
                    verdict, detail = "near", f"file {name}: model differs from the zero-pass run's on ONNX " \
                                              f"Runtime (within 1e-3): {cmp.get('detail', '')}"[:200]
                continue
        if not nx or not ny or nx.get("values", nx.get("sample")) is None:
            if verdict == "equal":
                verdict, detail = "other", f"file {name} differs (compared by hash only)"
            continue
        if nx["dtype"] != ny["dtype"] or nx["shape"] != ny["shape"]:
            return "value", f"file {name}: dtype/shape {nx['dtype']}{nx['shape']} vs {ny['dtype']}{ny['shape']}", sampled
        full = ny.get("vs_ref") if full_arrays else None
        if isinstance(full, dict) and full.get("within_strict") is not None:
            # D10: run.py compared every element of a large array with the zero-pass run's file.
            if full["within_strict"]:
                continue
            if not full.get("within_near"):
                return "value", f"file {name}: max scaled |diff| {full.get('max_scaled_err')} over all " \
                                f"{full.get('elements')} elements (beyond 1e-3)", sampled
            verdict, detail = "near", f"file {name}: max |diff| {full.get('max_scaled_err'):.3e} (within 1e-3)"
            continue
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
        elif rules.file_ops_by_content and rules.files and FILE_WRITING.match(e.get("uri", "")):
            payloads.append("<written files>")   # D7: the files (names and SHA-1) are compared below
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
    files_state = "the records have no file digests"
    if rules.files and "files" in ref and "files" in r:
        verdict, detail, sampled = compare_files(ref, r, rules.file_ops_by_content, rules.npy_full_comparison)
        info["file_sampled"] = sampled
        written = [f for f in ref.get("files", []) if not f["name"].startswith("gate-")]
        files_state = ("equal" if written else "the run wrote nothing into its run directory") \
            if verdict == "equal" else detail
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
        verdict, mode, detail = compare_payloads(ref, r, rules, mask, files_state)
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
    elif name == "REFUSED" and ACTIVE is not None and ACTIVE.unlocated_refusal_violation \
            and info["located"] is not True:
        name = "REFUSED_UNLOCATED"
    return WORST_ORDER.index(name)


def refusal_name(info: dict, rules: Rules) -> str:
    """D2: the name of a REFUSED outcome says whether it conforms to the refusal clause."""
    if not rules.unlocated_refusal_violation or info["outcome"] != "REFUSED":
        return info["outcome"]
    if info["clean"] is False:
        return "REFUSED_NOT_CLEAN"
    return "REFUSED_LOCATED" if info["located"] is True else "REFUSED_UNLOCATED"


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
    if rules.unlocated_refusal_violation:
        info["outcome"] = refusal_name(info, rules)
        infos = [dict(i, outcome=refusal_name(i, rules)) for i in infos]
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
    if ACTIVE is not None and ACTIVE.unlocated_refusal_violation and info["outcome"] == "REFUSED_AS_EXPECTED":
        info["outcome"] = "REFUSED_LOCATED" if info["located"] is True else "REFUSED_UNLOCATED"   # D2
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


def load_exclusions(path: Optional[str] = None) -> dict:
    """The static exclusion list (D4): {"excluded": {program path: entry}, "reviewed": {...}}."""
    data = json.loads(Path(path or HERE / "exclusions.json").read_text(encoding="utf-8"))
    return {"excluded": {e["path"]: e for e in data.get("excluded", [])},
            "reviewed": {e["path"]: e for e in data.get("reviewed_not_excluded", [])}}


def removed_kind(rec: dict, removed: Optional[dict]) -> str:
    """D5: why a must-refuse record is not in the must-refuse set ('' when it is in it). New
    records carry the corpus' decision (`expect_kind`); for records made before D5 the decision
    is looked up in a corpus built by the current corpus.py (`--corpus`)."""
    kind = rec.get("expect_kind")
    if kind is None and removed is not None:
        kind = removed.get(rec["program_id"], "")
    return kind if kind and kind not in pl.MUST_REFUSE_KINDS else ""


def classify_all(cells, rules: Rules, factor: float, floor: float, exclusions: Optional[dict] = None,
                 removed: Optional[dict] = None):
    global ACTIVE, STRIP_PAYLOAD_TYPES
    ACTIVE, STRIP_PAYLOAD_TYPES = rules, rules.payload_strip_types
    if rules.exclusion_list and exclusions is None:
        exclusions = load_exclusions()
    excluded = (exclusions or {}).get("excluded", {}) if rules.exclusion_list else {}
    reviewed = (exclusions or {}).get("reviewed", {}) if rules.exclusion_list else {}
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
                    row = refusal_row(cells[(prog, pid)])
                    why = removed_kind(any_rec, removed) if rules.must_refuse_static_only else ""
                    if why:
                        row["set"] = "removed from the must-refuse set (D5)"
                        row["removed_reason"] = why
                    refusal_rows.append(row)
            continue
        if (prog, pl.REFERENCE) not in cells:
            continue
        ref_recs = cells[(prog, pl.REFERENCE)]
        ref_outcome, ref_detail = classify_ref(ref_recs, rules)
        ref = ref_recs[0]
        check_outcome, check_detail = ref_outcome, ref_detail
        listed = excluded.get(ref["program"])
        if listed:
            ref_outcome, ref_detail = "EXCLUDED", listed["reason"]
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
        if rules.report_v2:
            ops = sorted({e.get("uri", "") for x in ref_recs for e in x.get("effects", [])
                          if CLOCK_OR_RANDOM_V2.search(e.get("uri", ""))})
            ref_rows[-1].update({
                "in_exclusion_list": bool(listed), "determinism_check": check_outcome,
                "determinism_detail": check_detail if listed else "",
                "clock_or_random_operators": " ".join(ops),
                "clock_or_random_reviewed": ("excluded" if listed else "reviewed" if ref["program"] in reviewed
                                             else "NOT REVIEWED" if ops and rules.exclusion_list else ""),
                "output_mask": ref.get("output_mask", "")})
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
            if rules.report_v2:
                rows[-1]["contract_class"] = CONTRACT_CLASS.get(info["outcome"], "")
                rows[-1]["signature"] = signature_of(rows[-1])
    if rules.must_refuse_static_only or rules.report_v2:
        for row in refusal_rows:
            row.setdefault("removed_reason", "")
    if rules.report_v2:
        for row in refusal_rows:
            row["signature"] = signature_of(row)
    return rows, ref_rows, refusal_rows, programs, pipeline_ids


# ---------------------------------------------------------------------------------------------
# Violation signatures (D9): cause groups first, cells second
# ---------------------------------------------------------------------------------------------

_SIG_RULES = [
    (re.compile(r"^near \(within 1e-3\); "), ""),
    (re.compile(r"file \S+?(\.\w+)?: "), r"file <*\1>: "),
    (re.compile(r"payload of effect #\d+ \(([^)]*)\).*"), r"payload of effect (\1) differs"),
    (re.compile(r"killed after [\d.]+ s"), "killed at the cap"),
    (re.compile(r"ref='.*' got='.*'$"), "ref/got differ"),
    (re.compile(r"^An error has occurred: "), ""),
    (re.compile(r"(?<![\w.:])[-+]?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?(?![\w])"), "N"),
]


def signature_of(row: dict) -> str:
    """The diagnostic name of a cell with the head of its message, or, without a diagnostic, its
    first difference; numbers, file names and the compared values are taken out. The message
    head is part of the signature because one diagnostic name (RuntimeDiag::RuntimeError) covers
    unrelated causes."""
    text = row.get("detail", "") or ""
    for pattern, repl in _SIG_RULES:
        text = pattern.sub(repl, text)
    text = text.strip()
    if row.get("diag_name"):
        return f"{row['diag_name']} | {text[:110]}"
    return text[:120]


def signature_tables(rows: List[dict], refusal_rows: List[dict]) -> Tuple[List[dict], List[dict]]:
    """signatures.csv: one row per (set, outcome, signature, passes common to all the failing
    pipelines of a program), grouped across programs. signature_causes.csv: the same without
    the passes. Only cells that violate the contract are in them."""
    failing: Dict[tuple, List[dict]] = collections.defaultdict(list)
    for r in rows:
        if is_violation(r):
            failing[("agree set", r["outcome"], r["contract_class"], r["signature"], r["program_id"])].append(r)
    for r in refusal_rows:
        if r["set"].startswith("must-refuse") and r["outcome"] not in REFUSAL_CONFORMING:
            failing[(r["set"], r["outcome"], "must_refuse_violation", r["signature"], r["program_id"])].append(r)
    groups: Dict[tuple, List[Tuple[str, List[dict]]]] = collections.defaultdict(list)
    for (kind, outcome, cls, sig, prog), cells_ in failing.items():
        common = set(cells_[0]["passes"].split())
        for c in cells_[1:]:
            common &= set(c["passes"].split())
        groups[(kind, outcome, cls, sig, " ".join(sorted(common)))].append((prog, cells_))

    def describe(key_prefix: tuple, members: List[Tuple[str, List[dict]]]) -> dict:
        cells_ = [c for _, cs in members for c in cs]
        progs = sorted({p for p, _ in members})
        shortest = min(cells_, key=lambda c: (len(c["passes"].split()), c["pipeline_id"]))
        return {"programs": len(progs), "cells": len(cells_),
                "pipelines": len({c["pipeline_id"] for c in cells_}),
                "shortest_failing_pipeline": f"{shortest['pipeline_id']}: {shortest['passes'] or '(no pass)'}",
                "program_list": " ".join(progs[:12]) + (f" ... (+{len(progs) - 12})" if len(progs) > 12 else ""),
                "example_detail": (cells_[0].get("detail") or "")[:200]}

    table = []
    for key, members in groups.items():
        kind, outcome, cls, sig, common = key
        table.append({"set": kind, "contract_class": cls, "outcome": outcome, "signature": sig,
                      "common_passes": common or "(none)", **describe(key, members)})
    table.sort(key=lambda t: (t["set"], -t["cells"], t["outcome"], t["signature"]))
    coarse: Dict[tuple, List[Tuple[str, List[dict]]]] = collections.defaultdict(list)
    for (kind, outcome, cls, sig, _), members in groups.items():
        coarse[(kind, outcome, cls, sig)] += members
    causes = [{"set": k[0], "contract_class": k[2], "outcome": k[1], "signature": k[3], **describe(k, m)}
              for k, m in coarse.items()]
    causes.sort(key=lambda t: (t["set"], -t["cells"], t["outcome"], t["signature"]))
    return table, causes


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
    return row["outcome"] in VIOLATIONS or (row["outcome"] == "REFUSED" and row["clean"] is False) \
        or row["outcome"] in ("REFUSED_UNLOCATED", "REFUSED_NOT_CLEAN")


def is_refused(row: dict) -> bool:
    return row["outcome"] in REFUSED_OUTCOMES


def load_removed(corpus_path: str) -> dict:
    """{must-refuse id: expectation kind} from a corpus.json written by the current corpus.py."""
    corpus = json.loads(Path(corpus_path).read_text(encoding="utf-8"))
    removed = {m["id"]: m.get("expect_kind", "") for m in corpus.get("must_refuse", [])}
    removed.update({m["id"]: m.get("expect_kind", "removed") for m in corpus.get("removed_from_must_refuse", [])})
    return removed


def announce(rules: Rules) -> List[str]:
    in_force = deviations_of(rules)
    print(f"[classify] rule set in force: {rules.name}" + (f" (deviations {', '.join(in_force)})" if in_force else "")
          + f"; {LABEL}", file=sys.stderr, flush=True)
    for d in in_force:
        print(f"[classify]   {d}: {DEVIATIONS[d][1]}", file=sys.stderr, flush=True)
    return in_force


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--records", required=True, nargs="+", help="one or more records directories (shards)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--rules", default=DEFAULT_RULES,
                    help=f"rule set (default: the newest, {DEFAULT_RULES}); a name of RULESETS or frozen+Dk+...")
    ap.add_argument("--exclusions", default="", help="the static exclusion list (default: exclusions.json "
                                                     "next to this file); used by rule sets with D4")
    ap.add_argument("--corpus", default="", help="a corpus.json of the current corpus.py: for records made "
                                                 "before D5, says which must-refuse entries were removed")
    ap.add_argument("--time-factor", type=float, default=20.0)
    ap.add_argument("--time-floor", type=float, default=10.0)
    args = ap.parse_args()
    rules = resolve_rules(args.rules)
    announce(rules)
    exclusions = load_exclusions(args.exclusions or None) if rules.exclusion_list else None
    removed = load_removed(args.corpus) if rules.must_refuse_static_only and args.corpus else None
    cells = load_cells(args.records)
    totals = summarize(cells, rules, Path(args.out), args.time_factor, args.time_floor, exclusions, removed)[3]
    print(json.dumps(totals, indent=1))
    return 0


def summarize(cells, rules: Rules, out: Path, time_factor: float = 20.0, time_floor: float = 10.0,
              exclusions: Optional[dict] = None, removed: Optional[dict] = None):
    """Classifies the cells under a rule set and writes the summary files into `out`. Returns
    (rows, ref_rows, refusal_rows, totals)."""
    args = argparse.Namespace(time_factor=time_factor, time_floor=time_floor)
    in_force = deviations_of(rules)
    rows, ref_rows, refusal_rows, programs, pipeline_ids = classify_all(
        cells, rules, args.time_factor, args.time_floor, exclusions, removed)
    out.mkdir(parents=True, exist_ok=True)
    v2 = rules.report_v2

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
        refused = [r for r in prs if is_refused(r)]
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
        if v2:
            cls = collections.Counter(r["contract_class"] for r in prs)
            by_pipeline[-1].update({f"class_{k}": cls[k] for k in (
                "agree", "refused_located", "refused_unlocated", "refused_after_start", "run_time_error",
                "silent_divergence", "resource_limit", "not_judged", "not_applicable")})
    write_csv("by_pipeline.csv", by_pipeline)

    refusal_by: List[dict] = []
    for key in sorted({(r["set"], r["pipeline_id"]) for r in refusal_rows}):
        rs = [r for r in refusal_rows if (r["set"], r["pipeline_id"]) == key]
        cnt = collections.Counter(r["outcome"] for r in rs)
        ok = [r for r in rs if r["outcome"] in REFUSAL_REFUSED]
        refusal_by.append({"set": key[0], "pipeline_id": key[1],
                           "passes": rs[0]["passes"] if key[1] != "PLAN" else "(per entry)", "cells": len(rs),
                           "refused_as_expected": len(ok),
                           "refused_located": sum(1 for r in ok if r["located"] is True),
                           "refused_unlocated": sum(1 for r in ok if r["located"] is not True),
                           "accepted_should_refuse": cnt["ACCEPTED_SHOULD_REFUSE"],
                           "failed_after_start": cnt["FAILED_AFTER_START"],
                           "error_no_diagnostic": cnt["ERROR_NO_DIAGNOSTIC"],
                           "crash": cnt["CRASH"], "hang": cnt["HANG"]})
        if v2:
            conforming = sum(1 for r in rs if r["outcome"] in REFUSAL_CONFORMING)
            refusal_by[-1].update({"conforming": conforming, "violations": len(rs) - conforming})
    write_csv("refusal_by_pipeline.csv", refusal_by)

    refs = collections.Counter(r["reference"] for r in ref_rows)
    judged_programs = sorted({r["program_id"] for r in rows})
    in_set = [r for r in refusal_rows if r["set"].startswith("must-refuse")]
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
        "refused_located": sum(1 for r in rows if is_refused(r) and r["located"] is True),
        "refused_unlocated": sum(1 for r in rows if is_refused(r) and r["located"] is not True),
        "refused_not_clean": sum(1 for r in rows if is_refused(r) and r["clean"] is False),
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
        "must_refuse_refused_located": sum(1 for r in refusal_rows if r["outcome"] in REFUSAL_REFUSED and r["located"] is True),
        "must_refuse_refused_unlocated": sum(1 for r in refusal_rows if r["outcome"] in REFUSAL_REFUSED and r["located"] is not True),
    }
    if v2:
        signatures, causes = signature_tables(rows, refusal_rows)
        write_csv("signatures.csv", signatures)
        write_csv("signature_causes.csv", causes)
        write_csv("resource.csv", [r for r in rows if r["outcome"] == "RESOURCE_LIMIT"])
        write_csv("excluded.csv", [r for r in ref_rows if r["reference"] == "EXCLUDED"])
        write_csv("removed_from_must_refuse.csv", [r for r in refusal_rows if not r["set"].startswith("must-refuse")])
        not_listed = sorted(r["program_id"] for r in ref_rows if r["reference"] == "NONDETERMINISTIC")
        not_reviewed = sorted(r["program_id"] for r in ref_rows if r["clock_or_random_reviewed"] == "NOT REVIEWED")
        old_plan_records = sum(1 for (p, q), recs in cells.items() if recs[0].get("role") == "must_refuse"
                               and recs[0].get("expect_kind") is None)
        classes = collections.Counter(r["contract_class"] for r in rows)
        totals.update({
            "deviations_in_force": {d: DEVIATIONS[d][1] for d in in_force},
            "contract_classes": dict(classes),
            "contract_classes_native": dict(collections.Counter(r["contract_class"] for r in rows if r["pipeline_kind"] == "native")),
            "contract_classes_onnx": dict(collections.Counter(r["contract_class"] for r in rows if r["pipeline_kind"] == "onnx")),
            "payload_events_compared_through_written_files_cells": sum(1 for r in rows if "files" in r["payload_mode"].split("+")),
            "programs_excluded_by_list": refs["EXCLUDED"],
            "programs_excluded_by_list_that_passed_the_determinism_check": sum(
                1 for r in ref_rows if r["reference"] == "EXCLUDED" and r["determinism_check"] == "REFERENCE"),
            "programs_nondeterministic_not_on_the_list": not_listed,
            "programs_with_clock_or_random_operators_not_reviewed": not_reviewed,
            "must_refuse_cells": len(in_set),
            "must_refuse_conforming_cells": sum(1 for r in in_set if r["outcome"] in REFUSAL_CONFORMING),
            "must_refuse_violation_cells": sum(1 for r in in_set if r["outcome"] not in REFUSAL_CONFORMING),
            "must_refuse_removed_cells": len(refusal_rows) - len(in_set),
            "must_refuse_pruning_applied": (not rules.must_refuse_static_only) or old_plan_records == 0
                                           or removed is not None,
            "violation_signatures": sum(1 for s in signatures if s["set"] == "agree set"),
            "violation_cause_groups": sum(1 for s in causes if s["set"] == "agree set"),
            "must_refuse_violation_signatures": sum(1 for s in signatures if s["set"] != "agree set"),
        })
        if rules.must_refuse_static_only and old_plan_records and removed is None:
            print(f"[classify] WARNING: {old_plan_records} must-refuse cells were recorded before D5 and no "
                  f"--corpus was given: the must-refuse set is NOT pruned in this summary", file=sys.stderr)
        if not_listed:
            print(f"[classify] determinism check: {len(not_listed)} program(s) differ between zero-pass repeats "
                  f"and are NOT on the exclusion list (not judged; add them with a reason): {not_listed}",
                  file=sys.stderr)
        if not_reviewed:
            print(f"[classify] {len(not_reviewed)} program(s) run clock/random operators and are neither excluded "
                  f"nor reviewed in exclusions.json: {not_reviewed}", file=sys.stderr)
    (out / "totals.json").write_text(json.dumps(totals, indent=1), encoding="utf-8")
    return rows, ref_rows, refusal_rows, totals


if __name__ == "__main__":
    sys.exit(main())
