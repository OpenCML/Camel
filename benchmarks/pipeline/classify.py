#!/usr/bin/env python3
# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

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
  D10 written arrays are compared element by element at the stated tolerance (1e-5 per
      element, `near` at 1e-3), never through whole-array sums at the tolerance of one element.
      A large `.npy` file is judged by the comparison of all its elements that run.py records
      (`npy.vs_ref`). Where a record only holds the 4 096-element sample with the sum and the
      sum of absolute values (records made before that), the sampled elements are compared
      element by element and the two sums relative to the sum of absolute values; the cell says
      so (`file_compare_mode`) and is flagged as needing a rerun (`needs_rerun`).
  D11 (2026-10-07) the order of PURE READS in the effect trace is not part of a program's observable
      behaviour. Reason: the language leaves independent computations unordered, a read changes
      no state, and since the restoration of the parallel scheduler (graphs run as task graphs)
      independent reads are executed in an unspecified order; optimisation passes may reorder them
      too. Without this rule every such program is EFFECT_MISMATCH although exit code, output and
      written files agree. Pure reads are the operators of PURE_READ_OPERATORS: `env:get_env` (no
      operator of the libraries writes the environment) and `tensor:load_npy`. What stays
      order-bearing: every other effect (prints, file writes, random numbers, the clock, thread
      settings, ...), in their order among themselves; WHICH reads happen and with which arguments
      (compared as a multiset); a read whose payload text is missing or truncated; and the
      position of a FILE read relative to the file-writing operators of the run (FILE_WRITING):
      a file read may only move among the effects between the same two file writes, because a
      read of a file the program itself writes is not independent of that write. (Coarser than
      comparing paths, on purpose: the payload of a file-writing operator is often cut before
      its path, see D7.) Implemented in `events()`: under D11 the events are put in a canonical
      order before anything is compared.
  R1  owner-directed reframing (CLASSIFIER-RULES.md section 13). What is tested: pipelines of
      the standard optimisation passes and schedulers preserve results relative to the default
      pipeline (P0, no pass on the command line). "Any pass sequence behaves the same" is not a
      property of Camel and is not what a cell is charged with. Defect classes charged to the
      standard passes / schedulers (`defect_class`):
        silent_result_change          VALUE_MISMATCH, EFFECT_MISMATCH
        crash_hang_or_error           CRASH, HANG, ERROR_NO_DIAGNOSTIC, RUN_FAIL, LOAD_FAIL,
                                      CAPTURE_FAIL
        diagnostic_unlocated_or_late  REFUSED_UNLOCATED, REFUSED_NOT_CLEAN
      A failure whose diagnostic comes from a library precondition (message prefix in
      LIBRARY_PRECONDITIONS, initially `autodiff:`) is LIBRARY_PRECONDITION, not charged to a
      standard pass; `library_precondition` says `located_before_execution` (conforming) or
      `late_or_obscure` (a usability defect of the library). A program without a result on the
      default pipeline because of such a precondition, that runs under `std::macro std::nvm`,
      is REQUIRES_STD_MACRO (listed in requires_std_macro.csv), not INVALID_REF. Reporting:
      by_standard_pass.csv (defect cells among the pipelines containing each pass; an
      association, no causal claim), and the refusals without a location split by what was
      refused: a program, or a command line (unknown pass name: no source construct to locate).
The default rule set is the newest one (DEFAULT_RULES); the rule set in force is printed.

Usage: classify.py --records DIR/records [DIR2/records ...] --out DIR/summary
                   [--rules frozen+D1-D10+R1|frozen+D1-D9|frozen+D1|frozen|frozen+D1+D4+...|v1|...]
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
MACRO_OPERATORS = {"autodiff:grad", "autodiff:with_grad", "autodiff:vjp"}
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
# D11: operators that only read. ENV: nothing can write what they read. FILE: reads a file, so its
# position relative to the file-writing operators of the same run stays order-bearing.
PURE_READ_ENV = {"env:get_env"}
PURE_READ_FILE = {"tensor:load_npy"}
PURE_READ_OPERATORS = PURE_READ_ENV | PURE_READ_FILE
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
    "LIBRARY_PRECONDITION": "library_precondition",
}

# ---- R1 (owner-directed reframing; CLASSIFIER-RULES.md section 13) -------------------------------
# Library preconditions: a failure whose first Error diagnostic starts with one of these prefixes
# (after the generic "An error has occurred: ") comes from a library, not from a standard pass or
# the language core. The list is part of the rule set; extend it only with a recorded revision.
LIBRARY_PRECONDITIONS = [
    {"prefix": "autodiff:", "library": "autodiff", "requires_pass": "std::macro",
     "precondition": "std::macro must run before grad / with_grad / vjp "
                     "(docs/design-principles.cn.md section 9)"},
]
_GENERIC_ERROR_HEAD = re.compile(r"^An error has occurred: ")
# Outcomes a library precondition can show as (a diagnostic exists and names the library).
LIBRARY_PRECONDITION_SOURCES = {"RUN_FAIL", "CAPTURE_FAIL", "REFUSED", "REFUSED_LOCATED", "REFUSED_UNLOCATED",
                                "REFUSED_NOT_CLEAN"}
# Defect classes charged to the standard optimisation passes / schedulers, and the other classes.
DEFECT_CLASS = {
    "CONSISTENT": "agrees",
    "REFUSED_LOCATED": "refused_located",
    "VALUE_MISMATCH": "silent_result_change", "EFFECT_MISMATCH": "silent_result_change",
    "CRASH": "crash_hang_or_error", "HANG": "crash_hang_or_error", "RUN_FAIL": "crash_hang_or_error",
    "LOAD_FAIL": "crash_hang_or_error", "ERROR_NO_DIAGNOSTIC": "crash_hang_or_error",
    "CAPTURE_FAIL": "crash_hang_or_error",
    "REFUSED_UNLOCATED": "diagnostic_unlocated_or_late", "REFUSED_NOT_CLEAN": "diagnostic_unlocated_or_late",
    "LIBRARY_PRECONDITION": "library_precondition",
    "RESOURCE_LIMIT": "resource_limit",
    "UNDECIDED": "not_judged", "MACRO_EXCEPTION": "not_judged",
    "NOT_APPLICABLE": "not_applicable",
}
DEFECT_CLASSES = ["silent_result_change", "crash_hang_or_error", "diagnostic_unlocated_or_late"]
ALL_CLASSES_R1 = ["agrees", "refused_located", *DEFECT_CLASSES, "library_precondition", "resource_limit",
                  "not_judged", "not_applicable"]
# Defect classes of the must-refuse set (R1 vocabulary; the outcomes are unchanged).
MUST_REFUSE_DEFECT_CLASS = {
    "REFUSED_UNLOCATED": "diagnostic_unlocated", "FAILED_AFTER_START": "diagnostic_late",
    "ACCEPTED_SHOULD_REFUSE": "invalid_command_line_accepted",
    "CRASH": "crash_hang_or_error", "HANG": "crash_hang_or_error", "ERROR_NO_DIAGNOSTIC": "crash_hang_or_error",
}
BY_PASS_NOTE = ("Association only, no causal claim: a pipeline has several passes, so one defect cell is counted "
                "under every pass of its pipeline, and the pipelines containing a pass are not a random sample "
                "(module passes run only on programs that import the module). A count here does not say that "
                "the pass causes the defect.")
D10_RERUN = ("D10: a written array differs by hash from the default pipeline's and the record holds a "
             "4 096-element sample with two sums, not all elements")


def library_of(message: str) -> Optional[dict]:
    """The library whose precondition a diagnostic message reports, or None."""
    text = _GENERIC_ERROR_HEAD.sub("", message or "")
    return next((e for e in LIBRARY_PRECONDITIONS if text.startswith(e["prefix"])), None)


def macro_position(passes: List[str]) -> str:
    """Where `std::macro` is in a pipeline (evidence for the autodiff precondition)."""
    if "std::macro" not in passes:
        return "no std::macro in the pipeline"
    at = passes.index("std::macro")
    return "std::macro first" if at == 0 else "std::macro after " + " ".join(passes[:at])


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
    npy_full_comparison: bool = False           # D10: arrays element by element; sums only relative to abs_sum
    read_order_free: bool = False               # D11: the order of pure reads is not observable behaviour
    r1: bool = False                            # R1: defect classes, LIBRARY_PRECONDITION, REQUIRES_STD_MACRO


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
    "D10": ("npy_full_comparison", "written arrays compared element by element (1e-5, near 1e-3): all elements "
                                   "where the record has them (values, or run.py's vs_ref); otherwise the sample "
                                   "element by element and the sums relative to the sum of absolute values, "
                                   "flagged as needing a rerun"),
    "D11": ("read_order_free", "2026-10-07: the order of pure reads (" + ", ".join(sorted(PURE_READ_OPERATORS))
                               + ") in the effect trace is not a defect: independent computations are unordered "
                               "by design and a read changes no state; which reads happen, their arguments, and "
                               "the position of a file read relative to the file writes stay compared"),
    "R1": ("r1", "owner-directed reframing: defect classes charged to the standard passes / schedulers; "
                 "LIBRARY_PRECONDITION for diagnostics of a library precondition (prefixes: "
                 + ", ".join(e["prefix"] for e in LIBRARY_PRECONDITIONS) + "); REQUIRES_STD_MACRO programs listed; "
                 "per-standard-pass table; unlocated refusals split into programs and command lines"),
}
# The same rulings in the vocabulary of R1 (used in the summaries of rule sets with R1; the rules
# are unchanged, the earlier texts above stay as they were archived).
DEVIATIONS_R1_TEXT = {
    "D2": "a refusal without a source location is a defect of the diagnostics (class "
          "diagnostic_unlocated_or_late), reported as REFUSED_UNLOCATED",
    "D3": "late finish = RESOURCE_LIMIT, reported, not a defect; hard kill = HANG, a defect (unchanged)",
    "D9": "classes per cell, defect signatures and cause groups, resource and exclusion tables",
}
# The revisions of the newest rule set, in the order they were recorded.
NEWEST_DEVIATIONS = ["D1", "D2", "D3", "D4", "D5", "D6", "D7", "D8", "D9", "D10", "D11", "R1"]
NEWEST = "frozen+D1-D11+R1"
DEFAULT_RULES = NEWEST


def _revision_key(name: str) -> tuple:
    return (0 if name[0] == "D" else 1, int(name[1:]))


_TYPE_PREFIX = re.compile(r"(^|\|)[A-Za-z_][^=|]*=")
STRIP_PAYLOAD_TYPES = False  # set from the rule set in classify_all()
ACTIVE: Optional["Rules"] = None  # the rule set in force, set in classify_all()

# `frozen+D1-D9` (the default until R1) and the newest rule set are resolved from their names
# like every `frozen+Dk+...` (resolve_rules).
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
    if flags.get("r1") and not (flags.get("unlocated_refusal_violation") and flags.get("report_v2")):
        raise SystemExit(f"classify.py: rule set '{name}': R1 names its classes on top of D2 and D9; add them")
    rules = Rules(name, **flags)
    _NAMED_DEVIATIONS[name] = parts[1:]
    return rules


_NAMED_DEVIATIONS: Dict[str, List[str]] = {}


def deviations_of(rules: Rules) -> List[str]:
    """The deviations in force under a rule set (those without a flag count as in force only
    where the name of the rule set lists them)."""
    if rules.name.startswith("frozen+"):
        named = _NAMED_DEVIATIONS.get(rules.name, rules.name.split("+"))
        return [d for d in sorted(DEVIATIONS, key=_revision_key)
                if (DEVIATIONS[d][0] and getattr(rules, DEVIATIONS[d][0])) or d in named]
    return []


def revision_text(name: str, rules: Rules) -> str:
    return DEVIATIONS_R1_TEXT.get(name, DEVIATIONS[name][1]) if rules.r1 else DEVIATIONS[name][1]

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
    if rules.read_order_free:
        evs = order_free_reads(evs)
    return evs


def _read_sort_key(event: dict) -> Optional[tuple]:
    """The key a pure read is ordered by under D11, or None when the event must keep its place
    (not a pure read, or its payload text is missing or truncated)."""
    if event.get("uri") not in PURE_READ_OPERATORS:
        return None
    text = event.get("payload")
    if not isinstance(text, str) or event.get("payload_truncated"):
        return None
    if STRIP_PAYLOAD_TYPES:
        text = _TYPE_PREFIX.sub(lambda m: m.group(1), text)
    return (event.get("uri"), text)


def order_free_reads(evs: List[dict]) -> List[dict]:
    """D11: the canonical order of an effect trace in which the order of pure reads does not count.
    Reads of the environment come first, sorted. The rest is cut at every file-writing event; inside
    each piece the file reads come first, sorted, then the other events in their recorded order. Two
    traces have the same canonical order iff they agree on the ordered sequence of the events that
    are not pure reads, on the multiset of environment reads, and on the multiset of file reads
    between each pair of consecutive file writes."""
    if not any(e.get("uri") in PURE_READ_OPERATORS for e in evs):
        return evs
    env_reads, out, file_reads, others = [], [], [], []
    for e in evs:
        key = _read_sort_key(e)
        if key is not None and e.get("uri") in PURE_READ_ENV:
            env_reads.append((key, len(env_reads), e))
        elif key is not None:
            file_reads.append((key, len(file_reads), e))
        else:
            others.append(e)
            if FILE_WRITING.match(e.get("uri") or ""):
                out += [x[2] for x in sorted(file_reads, key=lambda x: x[:2])] + others
                file_reads, others = [], []
    out += [x[2] for x in sorted(file_reads, key=lambda x: x[:2])] + others
    return [x[2] for x in sorted(env_reads, key=lambda x: x[:2])] + out


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

def _scaled_diff(p: float, q: float) -> float:
    """|p - q| / max(1, |p|), p the reference; NaN equals NaN only."""
    if p != p or q != q:
        return 0.0 if (p != p and q != q) else float("inf")
    return 0.0 if p == q else abs(p - q) / max(1.0, abs(p))


def _relative_diff(a: float, b: float, scale: float) -> float:
    if a != a or b != b:
        return 0.0 if (a != a and b != b) else float("inf")
    return 0.0 if a == b else abs(a - b) / max(1.0, abs(scale))


def compare_files(ref: dict, rec: dict, by_content: bool = False, full_arrays: bool = False,
                  evidence: Optional[dict] = None) -> Tuple[str, str, bool]:
    """(verdict, detail, sampled): verdict equal | set | value | near | other. Written .npy
    files are compared by value; other files by hash (`other`: reported, not an outcome).

    With `by_content` (D7) the other files are compared by content where the record has it:
    a text file through its recorded text (numbers per the tolerances), an exported model through
    `vs_ref`, the comparison run.py made with the zero-pass run's file of the same name on ONNX
    Runtime. A file that differs by hash and has neither stays `other`.

    With `full_arrays` (D10) an array is compared element by element at the tolerance and never
    through whole-array sums at the tolerance of one element: all elements when the record has
    them (`values`, or run.py's `vs_ref`); otherwise the record holds 4 096 evenly spaced
    elements with the sum and the sum of absolute values of the whole array, and then the
    sampled elements are compared element by element and the two sums relative to the sum of
    absolute values of the reference (|d| <= tol * max(1, abs_sum)).

    `evidence` (optional) receives what the comparison rested on: `modes`, the set of
    identical | all_elements | all_elements_vs_ref | sample_and_relative_sums |
    sample_and_sums_at_element_tolerance | text | model_on_onnxruntime | hash_only over the files
    looked at, and under D10 `max_sampled_scaled_diff` / `max_sum_diff_relative_to_abs_sum`."""
    modes = evidence.setdefault("modes", set()) if evidence is not None else set()
    fa = {f["name"]: f for f in ref.get("files", []) if not f["name"].startswith("gate-")}
    fb = {f["name"]: f for f in rec.get("files", []) if not f["name"].startswith("gate-")}
    if set(fa) != set(fb):
        return "set", f"files written: ref={sorted(fa)} got={sorted(fb)}", False
    verdict, detail, sampled = "equal", "", False
    for name in sorted(fa):
        x, y = fa[name], fb[name]
        if x["sha1"] == y["sha1"]:
            modes.add("identical")
            continue
        nx, ny = x.get("npy"), y.get("npy")
        if by_content and not (nx and ny):
            tx, ty, cmp = x.get("text"), y.get("text"), y.get("vs_ref")
            if isinstance(tx, str) and isinstance(ty, str):
                modes.add("text")
                if numeric_compare(tx, ty, STRICT):
                    continue
                if not numeric_compare(tx, ty, NEAR):
                    return "value", f"file {name}: {first_diff(tx, ty)}", sampled
                if verdict in ("equal", "other"):
                    verdict, detail = "near", f"file {name}: text differs (within 1e-3): {first_diff(tx, ty)}"
                continue
            if isinstance(cmp, dict) and cmp.get("equivalent") is not None:
                modes.add("model_on_onnxruntime")
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
            modes.add("hash_only")
            if verdict == "equal":
                verdict, detail = "other", f"file {name} differs (compared by hash only)"
            continue
        if nx["dtype"] != ny["dtype"] or nx["shape"] != ny["shape"]:
            return "value", f"file {name}: dtype/shape {nx['dtype']}{nx['shape']} vs {ny['dtype']}{ny['shape']}", sampled
        full = ny.get("vs_ref") if full_arrays else None
        if isinstance(full, dict) and full.get("within_strict") is not None:
            # D10: run.py compared every element of a large array with the zero-pass run's file.
            modes.add("all_elements_vs_ref")
            if full["within_strict"]:
                continue
            if not full.get("within_near"):
                return "value", f"file {name}: max scaled |diff| {full.get('max_scaled_err')} over all " \
                                f"{full.get('elements')} elements (beyond 1e-3)", sampled
            verdict, detail = "near", f"file {name}: max |diff| {full.get('max_scaled_err'):.3e} (within 1e-3)"
            continue
        is_float = "f" in nx["dtype"]
        va, vb = nx.get("values"), ny.get("values")
        if va is None and full_arrays:
            # D10 on a record without all elements: the sample element by element, the sums of the
            # whole array relative to the sum of absolute values. Not a comparison of all elements.
            sampled = True
            modes.add("sample_and_relative_sums")
            sa, sb = nx["sample"], ny["sample"]
            if is_float:
                worst = max((_scaled_diff(p, q) for p, q in zip(sa, sb)), default=0.0)
                sums = max(_relative_diff(nx["sum"], ny["sum"], nx["abs_sum"]),
                           _relative_diff(nx["abs_sum"], ny["abs_sum"], nx["abs_sum"]))
            else:
                worst = 0.0 if sa == sb else float("inf")
                sums = 0.0 if (nx["sum"], nx["abs_sum"]) == (ny["sum"], ny["abs_sum"]) else float("inf")
            if evidence is not None:
                evidence["max_sampled_scaled_diff"] = max(evidence.get("max_sampled_scaled_diff", 0.0), worst)
                evidence["max_sum_diff_relative_to_abs_sum"] = max(
                    evidence.get("max_sum_diff_relative_to_abs_sum", 0.0), sums)
            if worst <= STRICT and sums <= STRICT:
                continue
            what = (f"file {name}: sampled elements max scaled |diff| {worst:.3e}; sums differ by {sums:.3e} "
                    f"of the sum of absolute values")
            if worst <= NEAR and sums <= NEAR:
                verdict, detail = "near", what + " (within 1e-3)"
                continue
            return "value", what + " (beyond 1e-3)", sampled
        modes.add("all_elements" if va is not None else "sample_and_sums_at_element_tolerance")
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
            "max_abs_err": "", "export_ran_program": False, "macro_effects_before_refusal": False,
            # R1 / D10 (written into the summaries of rule sets with R1 only)
            "diag_message": "", "underlying_outcome": "", "library": "", "library_precondition": "",
            "file_compare_mode": "", "file_max_sampled_scaled_diff": "", "file_max_sum_diff_rel_abs_sum": ""}


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
        info["diag_message"] = errs[0].get("message", "")
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
        evidence: dict = {}
        verdict, detail, sampled = compare_files(ref, r, rules.file_ops_by_content, rules.npy_full_comparison,
                                                 evidence)
        info["file_sampled"] = sampled
        info["file_compare_mode"] = "+".join(sorted(evidence.get("modes", ()))) or "none"
        info["file_max_sampled_scaled_diff"] = evidence.get("max_sampled_scaled_diff", "")
        info["file_max_sum_diff_rel_abs_sum"] = evidence.get("max_sum_diff_relative_to_abs_sum", "")
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
        info["diag_message"] = errs[0].get("message", "")
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
    if rules.r1:
        apply_library_precondition(info)
    return info


def apply_library_precondition(info: dict) -> None:
    """R1: a failure whose diagnostic comes from a library precondition is LIBRARY_PRECONDITION,
    whatever it was called before (`underlying_outcome` keeps that name). It conforms when the
    diagnostic has a location and came before anything executed (a front-end stage, or a pass
    stage with no output and no run-stage effect before it); otherwise it is late or obscure.
    Applied to the cell after the worst repeat was chosen, so the order of section 9 is unchanged."""
    info["underlying_outcome"] = info["outcome"]
    if info["outcome"] not in LIBRARY_PRECONDITION_SOURCES:
        return
    entry = library_of(info["diag_message"])
    if entry is None:
        return
    stage = info["diag_stage"]
    before = stage in ("parse", "typecheck") or (stage.startswith("pass:") and info["clean"] is True)
    info.update(outcome="LIBRARY_PRECONDITION", library=entry["library"],
                library_precondition="located_before_execution" if before and info["located"] is True
                else "late_or_obscure")


# ---------------------------------------------------------------------------------------------
# Cells that must refuse
# ---------------------------------------------------------------------------------------------

def classify_refusal(r: dict) -> dict:
    info = {"outcome": "", "detail": "", "located": "", "diag_stage": "", "diag_name": "", "library": "",
            "matches_expected_diagnostic": "", "matches_expected_line": "",
            "macro_effects_before_refusal": any(e.get("stage") == "macro" for e in r.get("effects", []))}
    errs = errors_of(r)
    expect = r.get("expect") or {}
    if errs:
        d = errs[0]
        info.update(diag_stage=d.get("data", {}).get("stage", ""), diag_name=d.get("data", {}).get("name", ""),
                    located=located(d), detail=d.get("message", "")[:160])
        info["library"] = (library_of(d.get("message", "")) or {}).get("library", "")
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


def program_record_paths(record_dirs: List[str]) -> Dict[str, List[Path]]:
    """The record files of each program: run.py writes records/<program id>/<pipeline>.r<k>.json."""
    groups: Dict[str, List[Path]] = collections.defaultdict(list)
    for d in record_dirs:
        for path in sorted(Path(d).rglob("*.json")):
            groups[path.parent.name].append(path)
    return groups


def classify_stream(record_dirs: List[str], rule_sets: List["Rules"], factor: float, floor: float,
                    exclusions: Optional[dict] = None, removed: Optional[dict] = None) -> Dict[str, dict]:
    """Classifies the records one program at a time, under every rule set of `rule_sets`, so that
    only one program's records are in memory (all records of the after-run need about 8 GB at
    once). A cell is judged from the records of its own program only, and classify_all visits the
    programs in sorted order, so the result is the same as classifying everything at once.
    Returns {rule set name: {rows, ref_rows, refusal_rows, programs, pipeline_ids,
    old_plan_records, records, cells}}."""
    groups = program_record_paths(record_dirs)
    result = {r.name: {"rows": [], "ref_rows": [], "refusal_rows": [], "programs": [], "pipeline_ids": set(),
                       "old_plan_records": 0, "records": 0, "cells": 0} for r in rule_sets}
    for name in sorted(groups):
        cells: Dict[Tuple[str, str], List[dict]] = collections.defaultdict(list)
        for path in groups[name]:
            rec = json.loads(path.read_text(encoding="utf-8"))
            cells[(rec["program_id"], rec["pipeline_id"])].append(rec)
        for recs in cells.values():
            recs.sort(key=lambda r: r["repeat"])
        if {p for p, _ in cells} != {name}:
            raise SystemExit(f"classify.py: records under '{name}/' belong to {sorted({p for p, _ in cells})}; "
                             f"expected records/<program id>/<pipeline>.r<k>.json")
        old_plan = sum(1 for recs in cells.values() if recs[0].get("role") == "must_refuse"
                       and recs[0].get("expect_kind") is None)
        for rules in rule_sets:
            rows, ref_rows, refusal_rows, programs, pipeline_ids = classify_all(
                cells, rules, factor, floor, exclusions, removed)
            part = result[rules.name]
            part["rows"] += rows
            part["ref_rows"] += ref_rows
            part["refusal_rows"] += refusal_rows
            part["programs"] += programs
            part["pipeline_ids"] |= set(pipeline_ids)
            part["old_plan_records"] += old_plan
            part["records"] += sum(len(v) for v in cells.values())
            part["cells"] += len(cells)
        del cells
    for part in result.values():
        part["pipeline_ids"] = sorted(part["pipeline_ids"], key=pipeline_sort_key)
    return result


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
        if rules.r1 and ref_outcome == "INVALID_REF":
            need = requires_macro(ref_recs, cells.get((prog, pl.MACRO_REFERENCE)))
            if need:
                ref_outcome, ref_detail = "REQUIRES_STD_MACRO", need
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
        if rules.r1:
            ref_rows[-1]["library_precondition"] = next(
                ((library_of(errors_of(x)[0].get("message", "")) or {}).get("library", "")
                 for x in ref_recs if errors_of(x)), "")
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
            if rules.r1:
                modes = info["file_compare_mode"].split("+")
                rerun = []
                if "sample_and_relative_sums" in modes:
                    rerun.append(D10_RERUN)
                if info["files_differ"] or (info["outcome"] == "UNDECIDED" and "hash_only" in modes):
                    rerun.append("a written file differs by hash from the default pipeline's and the record "
                                 "has no comparison of its content")
                lp = info["outcome"] == "LIBRARY_PRECONDITION"
                module_passes = [p for p in recs[0]["passes"] if p in pl.MODULE_PASSES]
                rows[-1].update({
                    "underlying_outcome": info["underlying_outcome"],
                    "defect_class": DEFECT_CLASS.get(info["outcome"], ""),
                    "library": info["library"], "library_precondition": info["library_precondition"],
                    "std_macro_in_pipeline": macro_position(recs[0]["passes"]) if lp else "",
                    "pipeline_passes_kind": ("with module pass: " + " ".join(module_passes)) if module_passes
                    else "standard passes only",
                    "file_compare_mode": info["file_compare_mode"],
                    "file_max_sampled_scaled_diff": info["file_max_sampled_scaled_diff"],
                    "file_max_sum_diff_rel_abs_sum": info["file_max_sum_diff_rel_abs_sum"],
                    "needs_rerun": "; ".join(rerun)})
    if rules.must_refuse_static_only or rules.report_v2:
        for row in refusal_rows:
            row.setdefault("removed_reason", "")
    if rules.report_v2:
        for row in refusal_rows:
            row["signature"] = signature_of(row)
    for row in refusal_rows:
        library = row.pop("_library", "")
        if rules.r1:
            in_set = row["set"].startswith("must-refuse")
            row["refusal_subject"] = refusal_subject(row)
            row["defect_class"] = MUST_REFUSE_DEFECT_CLASS.get(row["outcome"], "") if in_set else ""
            row["library"] = library
    return rows, ref_rows, refusal_rows, programs, pipeline_ids


def requires_macro(ref_recs: List[dict], macro_recs: Optional[List[dict]]) -> str:
    """R1: why a program without a result on the default pipeline is REQUIRES_STD_MACRO ('' when
    it is not). Both must hold in the records: every repeat of the default pipeline reports a
    diagnostic of a library precondition that names `std::macro` as the pass it needs, and every
    repeat of `std::macro std::nvm` (P4) finishes with exit code 0 and no Error diagnostic."""
    entries = []
    for x in ref_recs:
        if x.get("skipped") or x["timed_out"] or not errors_of(x):
            return ""
        entry = library_of(errors_of(x)[0].get("message", ""))
        if entry is None or entry.get("requires_pass") != "std::macro":
            return ""
        entries.append(entry)
    if not macro_recs or any(x.get("skipped") or x["timed_out"] or errors_of(x) or x["exit_code"] != 0
                             for x in macro_recs):
        return ""
    message = _GENERIC_ERROR_HEAD.sub("", errors_of(ref_recs[0])[0].get("message", ""))
    return (f"requires std::macro: no result on the default pipeline ({entries[0]['library']} precondition: "
            f"{message[:110]}); runs under {' '.join(pl.FIXED[pl.MACRO_REFERENCE])} ({pl.MACRO_REFERENCE})")


def refusal_subject(row: dict) -> str:
    """What a must-refuse cell is expected to refuse: a command line (it names a pass that does
    not exist, so there is no source construct a diagnostic could point at) or a program."""
    unknown = pl.UNKNOWN_PASS in row["passes"].split() or row["diag_name"].endswith("UnrecognizedGraphPass")
    return "command line (no source construct to locate)" if unknown else "program"


def pass_kind(name: str) -> str:
    if name in pl.SCHEDULING_PASSES:
        return "standard scheduler"
    if name in pl.MODULE_PASSES:
        return f"module-contributed pass ({pl.MODULE_PASSES[name]})"
    return "standard rewrite pass" if name.startswith("std::") else "other pass"


def standard_pass_table(rows: List[dict]) -> List[dict]:
    """R1: per pass, the cells of the agree set on pipelines that contain it, and how many of
    them are defect cells, next to the same for the pipelines that do not contain it. An
    association, not a cause (BY_PASS_NOTE). A defect signature here is (outcome, signature)."""
    order = ["standard rewrite pass", "standard scheduler"]
    names = sorted({p for r in rows for p in r["passes"].split()},
                   key=lambda n: (order.index(pass_kind(n)) if pass_kind(n) in order else 2, n))
    table = []
    for name in names:
        inside = [r for r in rows if name in r["passes"].split()]
        outside = [r for r in rows if name not in r["passes"].split()]

        def side(cells_: List[dict], suffix: str) -> dict:
            defects = [r for r in cells_ if r["defect_class"] in DEFECT_CLASSES]
            out = {f"pipelines{suffix}": len({r["pipeline_id"] for r in cells_}),
                   f"cells{suffix}": len(cells_),
                   f"applicable_cells{suffix}": sum(1 for r in cells_ if r["outcome"] != "NOT_APPLICABLE"),
                   f"defect_cells{suffix}": len(defects)}
            if not suffix:
                out.update({f"defect_{k}": sum(1 for r in defects if r["defect_class"] == k) for k in DEFECT_CLASSES})
            out[f"defect_signatures{suffix}"] = len({(r["outcome"], r["signature"]) for r in defects})
            out[f"programs_with_defect{suffix}"] = len({r["program_id"] for r in defects})
            out[f"library_precondition_cells{suffix}"] = sum(1 for r in cells_ if r["outcome"] == "LIBRARY_PRECONDITION")
            return out

        table.append({"pass": name, "kind": pass_kind(name), **side(inside, ""),
                      **side(outside, "_in_pipelines_without_it")})
    return table


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


LIBRARY_SET = "agree set: library precondition, late or obscure (not charged to a standard pass)"


def signature_tables(rows: List[dict], refusal_rows: List[dict], r1: bool = False) -> Tuple[List[dict], List[dict]]:
    """signatures.csv: one row per (set, outcome, signature, passes common to all the failing
    pipelines of a program), grouped across programs. signature_causes.csv: the same without
    the passes. Only cells that violate the contract are in them (R1: defect cells; the late or
    obscure library-precondition cells are listed as a set of their own and are not counted
    among the defect signatures)."""
    failing: Dict[tuple, List[dict]] = collections.defaultdict(list)
    for r in rows:
        if is_violation(r):
            failing[("agree set", r["outcome"], r["contract_class"], r["signature"], r["program_id"])].append(r)
        elif r1 and r["outcome"] == "LIBRARY_PRECONDITION" and r["library_precondition"] == "late_or_obscure":
            failing[(LIBRARY_SET, r["outcome"], r["contract_class"], r["signature"], r["program_id"])].append(r)
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
        out = {"programs": len(progs), "cells": len(cells_),
               "pipelines": len({c["pipeline_id"] for c in cells_}),
               "shortest_failing_pipeline": f"{shortest['pipeline_id']}: {shortest['passes'] or '(no pass)'}",
               "program_list": " ".join(progs[:12]) + (f" ... (+{len(progs) - 12})" if len(progs) > 12 else ""),
               "example_detail": (cells_[0].get("detail") or "")[:200]}
        if r1:
            out["defect_class"] = cells_[0].get("defect_class", "")
            out["refusal_subject"] = cells_[0].get("refusal_subject", "")
            out["cells_needing_rerun"] = sum(1 for c in cells_ if c.get("needs_rerun"))
        return out

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
            "expectation_source": source, "detail": info["detail"], "_library": info["library"]}


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
        print(f"[classify]   {d}: {revision_text(d, rules)}", file=sys.stderr, flush=True)
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
    part = classify_stream(args.records, [rules], args.time_factor, args.time_floor, exclusions, removed)[rules.name]
    totals = write_summary(part, rules, Path(args.out), args.time_factor, args.time_floor, removed)[3]
    print(json.dumps(totals, indent=1))
    return 0


def summarize(cells, rules: Rules, out: Path, time_factor: float = 20.0, time_floor: float = 10.0,
              exclusions: Optional[dict] = None, removed: Optional[dict] = None):
    """Classifies the cells under a rule set and writes the summary files into `out`. Returns
    (rows, ref_rows, refusal_rows, totals). Needs all records in memory; classify_stream with
    write_summary gives the same files one program at a time."""
    rows, ref_rows, refusal_rows, programs, pipeline_ids = classify_all(
        cells, rules, time_factor, time_floor, exclusions, removed)
    part = {"rows": rows, "ref_rows": ref_rows, "refusal_rows": refusal_rows, "programs": programs,
            "pipeline_ids": pipeline_ids,
            "old_plan_records": sum(1 for recs in cells.values() if recs[0].get("role") == "must_refuse"
                                    and recs[0].get("expect_kind") is None)}
    return write_summary(part, rules, out, time_factor, time_floor, removed)


def write_summary(part: dict, rules: Rules, out: Path, time_factor: float = 20.0, time_floor: float = 10.0,
                  removed: Optional[dict] = None):
    """Writes the summary files of one classification (a value of classify_stream, or what
    summarize built) into `out`. Returns (rows, ref_rows, refusal_rows, totals)."""
    args = argparse.Namespace(time_factor=time_factor, time_floor=time_floor)
    in_force = deviations_of(rules)
    rows, ref_rows, refusal_rows = part["rows"], part["ref_rows"], part["refusal_rows"]
    programs, pipeline_ids = part["programs"], part["pipeline_ids"]
    out.mkdir(parents=True, exist_ok=True)
    v2 = rules.report_v2
    r1 = rules.r1

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
        if r1:
            dc = collections.Counter(r["defect_class"] for r in prs)
            lp = collections.Counter(r["library_precondition"] for r in prs if r["outcome"] == "LIBRARY_PRECONDITION")
            by_pipeline[-1].update({
                "defect_cells": sum(dc[k] for k in DEFECT_CLASSES),
                **{f"defect_{k}": dc[k] for k in DEFECT_CLASSES},
                "defect_signatures": len({(r["outcome"], r["signature"]) for r in prs
                                          if r["defect_class"] in DEFECT_CLASSES}),
                "library_precondition": dc["library_precondition"],
                "library_precondition_located_before_execution": lp["located_before_execution"],
                "library_precondition_late_or_obscure": lp["late_or_obscure"],
                "needs_rerun": sum(1 for r in prs if r["needs_rerun"])})
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
        if r1:
            unlocated = [r for r in rs if r["outcome"] == "REFUSED_UNLOCATED"]
            refusal_by[-1].update({
                "defect_cells": len(rs) - conforming if key[0].startswith("must-refuse") else 0,
                "unlocated_refusals_of_programs": sum(1 for r in unlocated if r["refusal_subject"] == "program"),
                "unlocated_refusals_of_command_lines": sum(1 for r in unlocated if r["refusal_subject"] != "program")})
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
        signatures, causes = signature_tables(rows, refusal_rows, r1)
        write_csv("signatures.csv", signatures)
        write_csv("signature_causes.csv", causes)
        write_csv("resource.csv", [r for r in rows if r["outcome"] == "RESOURCE_LIMIT"])
        write_csv("excluded.csv", [r for r in ref_rows if r["reference"] == "EXCLUDED"])
        write_csv("removed_from_must_refuse.csv", [r for r in refusal_rows if not r["set"].startswith("must-refuse")])
        not_listed = sorted(r["program_id"] for r in ref_rows if r["reference"] == "NONDETERMINISTIC")
        not_reviewed = sorted(r["program_id"] for r in ref_rows if r["clock_or_random_reviewed"] == "NOT REVIEWED")
        old_plan_records = part["old_plan_records"]
        classes = collections.Counter(r["contract_class"] for r in rows)
        totals.update({
            "deviations_in_force": {d: revision_text(d, rules) for d in in_force},
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
            "must_refuse_violation_signatures": sum(1 for s in signatures if s["set"].startswith("must-refuse")),
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
    if r1:
        totals.update(r1_report(rows, ref_rows, refusal_rows, signatures, causes, write_csv, out))
    (out / "totals.json").write_text(json.dumps(totals, indent=1), encoding="utf-8")
    return rows, ref_rows, refusal_rows, totals


def r1_report(rows: List[dict], ref_rows: List[dict], refusal_rows: List[dict], signatures: List[dict],
              causes: List[dict], write_csv, out: Path) -> dict:
    """R1: the tables and totals in the vocabulary of the owner-directed reframing. The keys of
    the earlier rule sets stay in totals.json with their old names (`vocabulary` says what each
    means now); these are added."""
    defects = [r for r in rows if r["defect_class"] in DEFECT_CLASSES]
    library = [r for r in rows if r["outcome"] == "LIBRARY_PRECONDITION"]
    needs = [r for r in rows if r["needs_rerun"]]
    requires = [r for r in ref_rows if r["reference"] == "REQUIRES_STD_MACRO"]
    in_set = [r for r in refusal_rows if r["set"].startswith("must-refuse")]
    unlocated = [dict(r, set="agree set") for r in rows if r["outcome"] == "REFUSED_UNLOCATED"] \
        + [r for r in in_set if r["outcome"] == "REFUSED_UNLOCATED"]
    by_pass = standard_pass_table(rows)

    write_csv("defects.csv", defects)
    write_csv("library_precondition.csv", library)
    write_csv("requires_std_macro.csv", requires)
    write_csv("needs_rerun.csv", needs)
    write_csv("by_standard_pass.csv", by_pass)
    write_csv("unlocated_refusals.csv", [
        {"set": r["set"], "refusal_subject": r.get("refusal_subject", "program"), "program": r["program"],
         "program_id": r["program_id"], "pipeline_id": r["pipeline_id"], "passes": r["passes"],
         "diag_stage": r["diag_stage"], "diag_name": r["diag_name"], "detail": r["detail"]} for r in unlocated])
    lines = [f"# Defect cells per pass ({LABEL})", "", BY_PASS_NOTE, "",
             "Cells of the agree set (program x pipeline, judged programs). `applicable` leaves out NOT_APPLICABLE "
             "(a module pass on a program that does not import the module). A defect signature is "
             "(outcome, signature). Library-precondition cells are not defect cells.", "",
             "| pass | kind | pipelines with it | cells (applicable) | defect cells | silent result change | "
             "crash, hang or error | diagnostic unlocated or late | defect signatures | programs with a defect | "
             "library precondition | without it: pipelines | cells (applicable) | defect cells | defect signatures |",
             "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|"]
    w = "_in_pipelines_without_it"
    for t in by_pass:
        lines.append(f"| `{t['pass']}` | {t['kind']} | {t['pipelines']} | {t['cells']} ({t['applicable_cells']}) | "
                     f"{t['defect_cells']} | {t['defect_silent_result_change']} | {t['defect_crash_hang_or_error']} | "
                     f"{t['defect_diagnostic_unlocated_or_late']} | {t['defect_signatures']} | "
                     f"{t['programs_with_defect']} | {t['library_precondition_cells']} | {t['pipelines' + w]} | "
                     f"{t['cells' + w]} ({t['applicable_cells' + w]}) | {t['defect_cells' + w]} | "
                     f"{t['defect_signatures' + w]} |")
    (out / "by_standard_pass.md").write_text("\n".join(lines) + "\n", encoding="utf-8")

    def subject_counts(cells_: List[dict]) -> dict:
        return dict(collections.Counter(r.get("refusal_subject", "program") for r in cells_))

    agree_unlocated = [r for r in unlocated if r["set"] == "agree set"]
    refuse_unlocated = [r for r in unlocated if r["set"] != "agree set"]
    sampled = [r for r in rows if "sample_and_relative_sums" in r["file_compare_mode"].split("+")]
    return {
        "what_is_tested": "pipelines of the standard optimisation passes and schedulers preserve results relative "
                          "to the default pipeline (P0: no pass on the command line); order-sensitivity as such "
                          "is not a defect (R1)",
        "library_precondition_prefixes": [e["prefix"] for e in LIBRARY_PRECONDITIONS],
        "programs_requires_std_macro": len(requires),
        "programs_requires_std_macro_list": sorted(r["program"] for r in requires),
        "classes": dict(collections.Counter(r["defect_class"] for r in rows)),
        "defect_cells": len(defects),
        "defect_cells_by_class": {k: sum(1 for r in defects if r["defect_class"] == k) for k in DEFECT_CLASSES},
        "defect_cells_by_outcome": dict(collections.Counter(r["outcome"] for r in defects)),
        "defect_cells_by_pipeline_passes_kind": dict(collections.Counter(r["pipeline_passes_kind"] for r in defects)),
        "defect_signatures": sum(1 for t in signatures if t["set"] == "agree set"),
        "defect_cause_groups": sum(1 for t in causes if t["set"] == "agree set"),
        "defect_cause_groups_by_class": dict(collections.Counter(t["defect_class"] for t in causes
                                                                 if t["set"] == "agree set")),
        "programs_with_defect": len({r["program_id"] for r in defects}),
        "pipelines_with_defect": len({r["pipeline_id"] for r in defects}),
        "library_precondition_cells": {
            "total": len(library),
            "located_before_execution": sum(1 for r in library if r["library_precondition"] == "located_before_execution"),
            "late_or_obscure": sum(1 for r in library if r["library_precondition"] == "late_or_obscure"),
            "by_library": dict(collections.Counter(r["library"] for r in library)),
            "by_underlying_outcome": dict(collections.Counter(r["underlying_outcome"] for r in library)),
            "by_std_macro_in_pipeline": dict(collections.Counter(
                "std::macro after another pass" if " after " in r["std_macro_in_pipeline"]
                else r["std_macro_in_pipeline"] for r in library)),
            "programs": len({r["program_id"] for r in library}),
            "cause_groups": sum(1 for t in causes if t["set"] == LIBRARY_SET)},
        "unlocated_refusals": {
            "note": "D2 is unchanged: every refusal without a source location is a defect of the diagnostics. "
                    "Reported apart for the owner: a refusal of a command line (unknown pass name) has no source "
                    "construct to locate.",
            "of_programs": len([r for r in unlocated if r.get("refusal_subject", "program") == "program"]),
            "of_command_lines": len([r for r in unlocated if r.get("refusal_subject", "program") != "program"]),
            "agree_set": subject_counts(agree_unlocated),
            "must_refuse_set": subject_counts(refuse_unlocated),
            "agree_set_by_terminal": dict(collections.Counter(r["passes"].split()[-1] for r in agree_unlocated)),
            "must_refuse_set_by_diagnostic": dict(collections.Counter(
                f"{r.get('refusal_subject')}: {r['diag_name']}" for r in refuse_unlocated))},
        "must_refuse_defect_cells_by_class": dict(collections.Counter(
            r["defect_class"] for r in in_set if r["outcome"] not in REFUSAL_CONFORMING)),
        "file_compare_modes": dict(collections.Counter(r["file_compare_mode"] for r in rows if r["file_compare_mode"])),
        "cells_judged_on_sample_and_relative_sums": {
            "total": len(sampled),
            "by_outcome": dict(collections.Counter(r["outcome"] for r in sampled)),
            "max_sampled_scaled_diff": max((r["file_max_sampled_scaled_diff"] for r in sampled
                                            if r["file_max_sampled_scaled_diff"] != ""), default=""),
            "max_sum_diff_relative_to_abs_sum": max((r["file_max_sum_diff_rel_abs_sum"] for r in sampled
                                                     if r["file_max_sum_diff_rel_abs_sum"] != ""), default="")},
        "cells_needing_rerun": {
            "total": len(needs),
            "by_reason_and_outcome": dict(collections.Counter(f"{r['outcome']}: {r['needs_rerun']}" for r in needs))},
        "effect_mismatch_cells_sync_scope_not_observable": {
            "cells": sum(1 for r in defects if r["outcome"] == "EFFECT_MISMATCH"),
            "note": "R1 charges an effect difference only inside a sync scope; the trace does not record the "
                    "scope (no in_sync_scope), so these cells are counted as silent_result_change without "
                    "that check"},
        "by_standard_pass_note": BY_PASS_NOTE,
        "vocabulary": {
            "violation_cells": "= defect_cells (cells of the three defect classes; library-precondition cells are "
                               "not among them)",
            "violation_signatures / violation_cause_groups": "= defect_signatures / defect_cause_groups",
            "contract_class (invariance.csv), class_* (by_pipeline.csv), contract_classes": "kept for scripts; read "
                               "defect_class, defect_* and classes instead",
            "must_refuse_violation_cells": "defect cells of the must-refuse set (must_refuse_defect_cells_by_class)",
            "programs_invalid_reference": "programs without a usable result on the default pipeline, NOT counting "
                                          "programs_requires_std_macro",
        },
    }


if __name__ == "__main__":
    sys.exit(main())
