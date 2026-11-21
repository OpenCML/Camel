#!/usr/bin/env python3
# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""The E9 differential: programs from a seed x pipelines x back ends x collectors, against model.py.

    e9diff.py run     --matrix slow [--n 50]  [--start 1] --out DIR [--jobs 4]
    e9diff.py run     --matrix full [--n 500] [--start 1] --out DIR [--jobs 4] [--update-baseline]
                      [--baseline FILE]
    e9diff.py compare --out DIR [--baseline FILE]
    e9diff.py analyze --out DIR
    e9diff.py reduce  --out DIR [--max-groups 12]
    e9diff.py collect --from DIR [--dry-run]

The campaign runs in three tiers, and only the last one is a ratchet:

    fast     the reduced programs of a campaign as ordinary cases: `test/cases/opt/e9/` with
             `test/plans/feat/opt/e9.plan.toml`, eight cells per program (no pass and `std::opt`
             on four back ends), seconds, on every merge. It is a plan and not this script; what
             this script adds is `collect`, which takes the programs a `reduce` run produced,
             proves them green, installs them and says which ones are not collectable yet.
    slow     `--matrix slow`: 50 seeds, the 11 pipelines on `std::nvm` (generational) and
             `std::prl` (mark-sweep) -- 22 cells per program, 1,100 cells, a minute or two at
             `--jobs 4`. For a run that is worth a few minutes, and for a first reading of a
             branch.
    full     `--matrix full`: 500 seeds, all 99 cells (11 pipelines x 4 back ends x 2 collectors
             + `std::cpp`), about an hour at `--jobs 4`. Deliberate: before a release, after a
             change to an optimization pass or to a back end, or on an idle machine overnight.
             **The ratchet is this one.** No automatic gate starts it.

`run` prints the three numbers a merge is judged by -- **wrong values**, **refusals**, **crashes**
-- and compares them with `baseline.json` beside this script: a number that rose fails the run, a
number that fell is printed as news to record. Every class's member seeds are printed on a line of
their own beside its count, because a count says a class moved and a list of seeds says which
programs moved it, and a seed is where a reproduction starts. `compare --out DIR` re-reads a
finished run and judges it again (with `--baseline FILE`, against another baseline), so a judgement
that costs 25 minutes to produce costs a second to repeat. `analyze` groups the failing programs by root cause
(the first failing pipeline prefix on the node VM: the attribution the author fixed for this
campaign); `reduce` shrinks one representative of each group.

The matrix: `p00` no pass; `p01` `std::macro`; `p02`..`p09` `std::macro` plus the first k of
`devirtualize specialize inline fold cse algebra dce order`, each as its own `std::cxx::<name>`
pass (one round of `simplifyGraph`, `order` last); `p10` `std::macro std::opt`. Back ends
`std::nvm`, `std::fvm`, `std::jit`, `std::prl` under both collectors in the full matrix, plus
`std::cpp` translated, built and run with no collector. Every cell is compared with the output the
generator's independent evaluator computed. The judgment:

    wrong value  MISMATCH                     the run printed something else
    refusal      REFUSED_UNLOCATED             a pass declined without a source position
    crash        CRASH HANG ERROR_NO_DIAG      died, did not finish, failed without a structured
                 CPP_COMPILE_ERROR             diagnostic, or the C++ compiler rejected the code

A located diagnostic of a rewrite pass, with nothing printed before it (`REFUSED`), is the legal
way for a pipeline to decline and is not a finding. A program the compiler rejects on the first
cell (stage parse or typecheck) is a generator reject: listed, not part of the matrix. `RUN_ERROR`
and a `FRONTEND` after the first cell are findings too and are reported beside the three numbers.
A program counts once in every class it reaches.

**The gate is per program, over the programs both runs had in their matrix.** A population that
moved used to end the comparison and with it the gate: on 2026-11-17 `programs_in_matrix` went
`227->225` (two seeds stopped crashing and started being legally rejected) and `-Check e9-full`
exited 0 while the wrong class went from 7 programs to 12. So the population is not a reason to stop
comparing -- the programs that are still measurable are compared one by one, each against its own
outcome in `baseline.json`, and *any* one of them reaching a class it did not reach before fails the
run. The two ways a program can leave the matrix are told apart, because they are not the same
thing: a program the generator now **rejects** is a legal reduction (the compiler declining a
program it used to accept -- printed, counted, not worse), while a program the run **did not reach**
(no record of it at all) is a measurement that went missing, and a cell nobody ran is not a cell
that passed. An **empty intersection** is a failure too: "there is nothing to compare" is the
strongest possible statement that this run says nothing about the baseline, and a gate that reads it
as a pass is the gate this rule exists to replace.

What this replaces from the reference implementation it was ported from, which carried a module
`common`: `SRC` is `runner.REPO_ROOT`, `CAMEL` is `runner.camel_exe()` behind `--camel` (one rule,
`test/tools/camel_arg.py`), `PINDIR` is `install_root()`, `ENV` is the child environment
`runner.run_cell` builds, `run` is `test/tools/cppexec/run.py:run`, `pmap` is the
`ThreadPoolExecutor` of `progen.py`, `write_json`/`write_jsonl` are the writes `progen.py` does,
`EVAL`/`OUT` are `--out`, and the programs, their reference output, the classification and the
reducer are `generate.py`, `model.py`, `runner.py` and `reduce.py` of `test/tools/progen/`.
"""

from __future__ import annotations

import argparse
import collections
import concurrent.futures
import datetime
import hashlib
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[2]
BASELINE = HERE / "baseline.json"
CASES = REPO_ROOT / "test" / "cases" / "opt" / "e9"
PLAN = REPO_ROOT / "test" / "plans" / "feat" / "opt" / "e9.plan.toml"

sys.path.insert(0, str(HERE.parents[0] / "progen"))
import runner as R  # noqa: E402        (puts benchmarks/pipeline on the path too)
from generate import generate  # noqa: E402
from model import emit, emit_modules  # noqa: E402


def load_module(name: str, path: Path):
    """A file loaded under a name of its own. Two tools of this tree both call their module `run`
    (`benchmarks/pipeline/run.py`, which `runner.py` imports, and `test/tools/cppexec/run.py`), so
    a plain `import run` would hand back whichever was imported first."""
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


CPP = load_module("e9diff_cppexec", HERE.parent / "cppexec" / "run.py")
EXE = ".exe" if os.name == "nt" else ""
COMPILER = CPP.find_compiler()

OPT_STAGES = ["devirtualize", "specialize", "inline", "fold", "cse", "algebra", "dce", "order"]
PIPES: list = [("p00_none", []), ("p01_macro", ["std::macro"])]
for k in range(1, len(OPT_STAGES) + 1):
    PIPES.append((f"p{k + 1:02d}_+{OPT_STAGES[k - 1]}",
                  ["std::macro"] + [f"std::cxx::{s}" for s in OPT_STAGES[:k]]))
PIPES.append(("p10_opt", ["std::macro", "std::opt"]))
PASSES = dict(PIPES)

BACKENDS = list(R.BACKENDS)                    # std::nvm, std::fvm, std::jit, std::prl
CPP_BACKEND = "std::cpp"
FAST_BACKENDS = ["nvm", "fvm", "jit", "prl"]

# The three tiers, the two of them this script runs. `collectors` is the list per back end: the
# slow matrix gives each of its two back ends one collector and covers both that way, which is the
# half of the collector matrix a merge can afford; the full matrix gives every back end both.
MATRICES = {
    "slow": dict(n=50, backends=["std::nvm", "std::prl"],
                 collectors={"std::nvm": ["generational"], "std::prl": ["mark-sweep"]},
                 cpp=False),
    "full": dict(n=500, backends=BACKENDS,
                 collectors={b: ["generational", "mark-sweep"] for b in BACKENDS}, cpp=True),
}

WRONG = {"MISMATCH"}
REFUSED = {"REFUSED_UNLOCATED"}
CRASHED = {"CRASH", "HANG", "ERROR_NO_DIAG", "CPP_COMPILE_ERROR"}
# Every outcome that is a finding, including the two the three numbers do not count.
BAD = set(R.BAD) | {"CPP_COMPILE_ERROR"}

# The classes one program can reach, as one word each, in the order the member lines print them.
CLASS_TOKENS = [("wrong", WRONG), ("refused", REFUSED), ("crashed", CRASHED)]

# A cell that was never run, because the program's earlier cells had already crashed `MAX_CRASHES`
# times. It is not an outcome and not a class: it is the measurement stopping, and a class that
# disappeared into it has not gone away -- which is why it is printed beside the three numbers and
# why a program whose last classes are `NOT_RUN` is worse than one whose classes are the same.
NOT_RUN = "NOT_RUN"


def program_state(rec: dict) -> list:
    """The classes one program in the matrix reached, as a sorted list of tokens; empty when every
    cell is `OK`.

    The three numbers are three of them. The rest are the outcomes that are findings of their own
    (`RUN_ERROR`, and a `FRONTEND` after the first cell): a program that exchanges one finding for
    another has not improved, and a comparison that knew only the three numbers would read that
    exchange as a fix -- seed 453 did exactly that on 2026-11-17, leaving the wrong class by turning
    all forty-eight of its wrong cells into `RUN_ERROR`."""
    if not rec["cells"]:
        # A record with no cell at all is a program the run did not measure, and it must not read as
        # a clean one: `[]` is what every `OK` program has.
        return ["unmeasured"]
    outcomes = collections.Counter(rec["cells"].values())
    tokens = [name for name, names in CLASS_TOKENS if any(o in names for o in outcomes)]
    tokens += sorted({o.lower() for o in outcomes
                      if o in BAD and o not in (WRONG | REFUSED | CRASHED)})
    return tokens


def state_text(tokens) -> str:
    """A program's classes in one word: `ok`, `wrong`, `crashed+wrong`."""
    return "+".join(tokens) if tokens else "ok"


# The cells of one program that may crash before the rest are not run: a crashed process can take
# many seconds to be collected, and 99 cells of a program whose passes crash would spend the run on
# process exits. The bound is the reference implementation's own.
MAX_CRASHES = 6
CELL_TIMEOUT = 30.0
CPP_TRANSLATE_TIMEOUT = 60.0
CPP_BUILD_TIMEOUT = 300.0
CPP_RUN_TIMEOUT = 60.0

OUT = Path(".")                                # --out of the current command
WORK = Path(".")                               # OUT / "work": the sources of the run in flight
CELLS: list = []                               # the cells of the matrix in force


def cells_of(matrix: str) -> list:
    """The cells of one program: (pipeline, back end, collector). 22 in the slow matrix, 99 in the
    full one (the `std::cpp` cell of a pipeline has no collector and is spelled `-`)."""
    spec = MATRICES[matrix]
    out = []
    for pid, _ in PIPES:
        for backend in spec["backends"]:
            for collector in spec["collectors"][backend]:
                out.append((pid, backend, collector))
        if spec["cpp"]:
            out.append((pid, CPP_BACKEND, "-"))
    return out


def cell_id(pid: str, backend: str, collector: str) -> str:
    return f"{pid}|{backend}|{collector}"


def install_root() -> Path:
    """`$CAMEL_HOME`: the install the measured binary belongs to. The same expression
    `runner.run_cell` sets for the children it starts -- one rule, in one place."""
    return Path(R.camel_exe()).resolve().parents[1]


def child_env(env_values: dict) -> dict:
    """The environment a `std::cpp` cell runs in: this process's, with the install under test named
    and the program's own environment values (the generator's `env` feature) on top. The four
    interpreting cells get theirs from `runner.run_cell`."""
    env = dict(os.environ)
    env["CAMEL_HOME"] = str(install_root())
    env.update(env_values)
    return env


def lines_of(text: str) -> list:
    got = text.split("\n")
    if got and got[-1] == "":
        got.pop()
    return got


def run_cpp(path: Path, passes: list, env_values: dict, expected: list, cache: dict) -> dict:
    """The `std::cpp` cell of a pipeline: translate, build, run, compare.

    The translation is classified by `runner.run_cell`, the same reader as every other cell, so a
    refused graph arrives as the located diagnostic it is (`RuntimeDiag::TranslationRefused`, as
    `REFUSED`) instead of as a failure nobody can place. A translation that succeeded prints the
    C++ it wrote, which is a `MISMATCH` against an empty output -- and `run_cell` keeps only the
    first sixty lines of what a cell printed, while the compiler needs all of it, so the source
    comes from one more run of the same command. `cache` holds the outcome by the digest of that
    source: two pipelines of one program translate to the same C++ when their passes leave the same
    graph, and the compiler is the slow part."""
    info = R.run_cell(path, [*passes, CPP_BACKEND], env_values, [], CPP_TRANSLATE_TIMEOUT)
    if info["outcome"] not in ("MISMATCH", "OK"):
        return info
    source = ""
    if info["outcome"] == "MISMATCH":
        code, source, err, _ = CPP.run([R.camel_exe(), path.name, *passes, CPP_BACKEND],
                                       CPP_TRANSLATE_TIMEOUT, child_env(env_values),
                                       cwd=str(path.parent))
        if code != 0 or not source.strip():
            return dict(outcome="ERROR_NO_DIAG",
                        detail=f"the translation that printed C++ failed on the second run: exit "
                               f"{code}: {(err or source).strip()[-160:]}")
    digest = hashlib.sha1(source.encode()).hexdigest()
    if digest in cache:
        return dict(cache[digest], cached=True)
    cpp = path.parent / f"x_{digest[:10]}.cpp"
    exe = path.parent / f"x_{digest[:10]}{EXE}"
    cpp.write_text(source, encoding="utf-8")
    env = child_env(env_values)
    # The written C++ says whether it uses an operator of a runtime module: bridged means it links
    # libcamel, and `cppexec`'s build line is the one that gets that link right on this host and on
    # the other (include/, the library directory, an rpath). No second copy of those flags here.
    compiler, msvc = COMPILER
    if compiler is None:
        result = dict(outcome="CPP_COMPILE_ERROR", detail="no C++ compiler found",
                      cpp_sha1=digest[:10])
    else:
        build = CPP.build_command(compiler, msvc, str(cpp), str(exe),
                                  "camel/standalone/bridge.h" in source, str(install_root()))
        bcode, bout, berr, _ = CPP.run(build, CPP_BUILD_TIMEOUT, env, cwd=str(path.parent))
        if bcode != 0:
            result = dict(outcome="CPP_COMPILE_ERROR", detail=(berr or bout).strip()[:300],
                          cpp_sha1=digest[:10])
        else:
            xcode, xout, xerr, _ = CPP.run([str(exe)], CPP_RUN_TIMEOUT, env, cwd=str(path.parent))
            got = lines_of(xout)
            if xcode is None:
                result = dict(outcome="HANG", detail="executable timed out", cpp_sha1=digest[:10])
            elif xcode < 0:
                result = dict(outcome="CRASH", detail=f"signal {-xcode}", cpp_sha1=digest[:10])
            elif xcode != 0:
                result = dict(outcome="RUN_ERROR", got=R.capped(got), cpp_sha1=digest[:10],
                              detail=f"exit {xcode}: {(xerr or xout).strip()[-200:]}")
            elif got == expected:
                result = dict(outcome="OK", cpp_sha1=digest[:10])
            else:
                result = dict(outcome="MISMATCH", got=R.capped(got), cpp_sha1=digest[:10],
                              detail=R.first_difference(expected, got))
    cache[digest] = result
    for f in (cpp, exe):
        try:
            f.unlink()
        except OSError:
            pass
    return result


# -------------------------------------------------------------------------------------- one program
def write_failure(seed: int, source: str, modules: dict, expected: list, env_values: dict,
                  bad: dict, attempts: int) -> None:
    """The failing program where the rest of `test/tools/progen` looks for it: `failures/
    progen_<seed>.cml` and `.json` in the shape `progen.py` writes. One form for the programs of
    both tools is what lets the same reader (`triage.py`) look at either."""
    d = OUT / "failures"
    d.mkdir(parents=True, exist_ok=True)
    name = f"progen_{seed}"
    (d / f"{name}.cml").write_text(source, encoding="utf-8", newline="\n")
    for stem, text in modules.items():
        (d / f"{stem}.cml").write_text(text, encoding="utf-8", newline="\n")
    (d / f"{name}.json").write_text(json.dumps(
        {"seed": seed, "features": "default", "size": 1, "attempts": attempts, "env": env_values,
         "expected": expected, "bad": bad}, indent=1) + "\n", encoding="utf-8", newline="\n")


def one(seed: int) -> dict:
    """The matrix of one program. The program is written under `--out/work`; it is kept as
    `failures/` only when a cell found something, and its scratch directory is dropped either way."""
    prog, expected, attempts = generate(seed)
    source = emit(prog)
    modules = emit_modules(prog)
    d = WORK / f"seed_{seed}"
    d.mkdir(parents=True, exist_ok=True)
    name = f"progen_{seed}"
    path = d / f"{name}.cml"
    path.write_text(source, encoding="utf-8", newline="\n")
    for stem, text in modules.items():
        (d / f"{stem}.cml").write_text(text, encoding="utf-8", newline="\n")
    env_values = prog.a["env"]
    rec = dict(seed=seed, attempts=attempts, lines=len(source.splitlines()),
               expected_lines=len(expected), cells={}, bad={}, other={})
    crashes, cache = 0, {}
    first = True
    for pid, backend, collector in CELLS:
        cid = cell_id(pid, backend, collector)
        if crashes >= MAX_CRASHES:
            rec["cells"][cid] = "NOT_RUN"
            continue
        if backend == CPP_BACKEND:
            info = run_cpp(path, PASSES[pid], env_values, expected, cache)
        else:
            info = R.run_cell(path, ["--gc", collector, *PASSES[pid], backend], env_values,
                              expected, CELL_TIMEOUT)
        if first:
            first = False
            if info["outcome"] == "FRONTEND":
                # The generator wrote a program the compiler does not accept. It is a generator
                # matter, not a cell of the matrix, and it is named with its diagnostic.
                rec.update(rejected=info.get("diag", ""), reject_detail=info.get("detail", "")[:200])
                shutil.rmtree(d, ignore_errors=True)
                return rec
        rec["cells"][cid] = info["outcome"]
        if info["outcome"] != "OK":
            tgt = rec["bad"] if info["outcome"] in BAD else rec["other"]
            tgt[cid] = {k: (v[:240] if isinstance(v, str) else v) for k, v in info.items()
                        if k in ("outcome", "diag", "detail", "stage", "located", "exit", "cpp_sha1")}
            if "got" in info:
                tgt[cid]["got"] = info["got"]
        crashes += info["outcome"] in ("CRASH", "HANG")
    rec["expected"] = expected
    if rec["bad"]:
        write_failure(seed, source, modules, expected, env_values, rec["bad"], attempts)
    shutil.rmtree(d, ignore_errors=True)
    return rec


def pmap(fn, items: list, jobs: int) -> list:
    """`pmap` of the reference implementation: one thread per program, since every cell is a
    process this tool waits on. The order of the result is the order of `items`."""
    results = [None] * len(items)
    started, done = time.monotonic(), 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = {pool.submit(fn, item): i for i, item in enumerate(items)}
        for future in concurrent.futures.as_completed(futures):
            results[futures[future]] = future.result()
            done += 1
            if done % 25 == 0:
                print(f"  {done}/{len(items)} programs, {time.monotonic() - started:.0f} s",
                      file=sys.stderr, flush=True)
    return results


# ----------------------------------------------------------------------------------------- numbers
def three_numbers(rows: list) -> dict:
    """The counts a merge is judged by, per program and per cell. A program counts once in every
    class it reaches: one program can hold a wrong value on one cell and a crash on another, and
    hiding one of the two behind the other is how a defect stays unfixed."""
    outcomes = collections.Counter()
    for r in rows:
        for o in r["cells"].values():
            outcomes[o] += 1
    numbers = {}
    for cls, names in (("wrong", WRONG), ("refused", REFUSED), ("crashed", CRASHED)):
        numbers[f"programs_{cls}"] = sum(1 for r in rows
                                         if any(o in names for o in r["cells"].values()))
        numbers[f"cells_{cls}"] = sum(n for o, n in outcomes.items() if o in names)
    numbers["cell_outcomes"] = dict(sorted(outcomes.items()))
    numbers["other_findings"] = {o: n for o, n in sorted(outcomes.items())
                                 if o in BAD and o not in (WRONG | REFUSED | CRASHED)}
    return numbers


def build_id() -> str:
    """Which build the numbers came from, in the two readings `test/tools/provenance.mjs` makes:
    the tree's HEAD, or the count of files that are not in it when the tree is dirty (a sha names a
    commit, and the code that ran is that commit plus those files), and the binary's own mtime --
    not the build's, which a failed link or a stale install leaves looking new."""
    def git(*args):
        try:
            p = subprocess.run(["git", *args], cwd=str(REPO_ROOT), capture_output=True,
                               text=True, timeout=60)
            return p.stdout.strip() if p.returncode == 0 else ""
        except (OSError, subprocess.SubprocessError):
            return ""

    sha = git("rev-parse", "HEAD")
    dirty = len([l for l in git("status", "--porcelain").splitlines() if l.strip()])
    exe = Path(R.camel_exe())
    built = (datetime.datetime.fromtimestamp(exe.stat().st_mtime).isoformat(timespec="seconds")
             if exe.exists() else "missing")
    tip = f"(dirty: {dirty} files)" if dirty else (sha or "unknown")
    return f"{tip} {exe.name} built {built}"


def read_baseline(path=None):
    """The baseline file, or None when it is not there. `path` is `--baseline`, and it exists so a
    judgement can be repeated against another file (a copy of an older one, a recording of another
    run) without editing the one the gate reads: a comparison whose right-hand side is hard-wired is
    a comparison nobody can test."""
    target = Path(path) if path else BASELINE
    try:
        return json.loads(target.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None


RATCHET = ["programs_wrong", "programs_refused", "programs_crashed",
           "cells_wrong", "cells_refused", "cells_crashed"]

# The three classes, each with its own two numbers. They are reported and judged apart because they
# say different things: a wrong value and a refusal both say "this program's answer is not usable",
# and a crash says "there is no answer" -- one class coming down does not pay for another going up,
# and a single combined sentence ("3 below, 3 above") reads as if it did.
CLASSES = [("wrong", "programs_wrong", "cells_wrong"),
           ("refused", "programs_refused", "cells_refused"),
           ("crashed", "programs_crashed", "cells_crashed")]

# What decides whether two runs are the same measurement at all. The seed range is not enough: the
# generator decides which seeds are rejects, and it changes -- on 2026-11-16 the same `seeds 1..500`
# held 320 programs in the matrix under the 11-14 generator and 227 under the current one -- so a
# comparison that only checked `matrix` and `seeds` compared two different populations and reported
# the result as a trend. A moved population is *not* a reason to stop comparing any more: it is the
# reason to compare the programs both runs had in their matrix and nothing else (`judge`), because a
# program that left the matrix is a program whose outcome there cannot be compared with today's.
POPULATION = ["programs_in_matrix", "generator_rejects"]


def population_break(baseline, numbers: dict) -> str:
    """How this run's program population differs from that baseline's, or '' -- a reading, no longer
    a refusal to compare. `judge` uses it to decide *what* may be compared (the intersection), and
    to say why the three aggregate counts above are not one trend."""
    moved, unrecorded = [], []
    for key in POPULATION:
        old, new = baseline.get(key), numbers.get(key)
        if old is None:
            unrecorded.append(key)
        elif new is not None and old != new:
            moved.append(f"{key} {old}->{new}")
    if unrecorded:
        return (f"the baseline records no {', '.join(unrecorded)}, so the programs it counted cannot "
                f"be compared with the {numbers.get('programs_in_matrix')} this run put in the "
                f"matrix; record the baseline again (--update-baseline)")
    if moved:
        return (f"the program population changed: {', '.join(moved)} -- the generator decides which "
                f"seeds are rejects, so {numbers.get('programs_in_matrix')} programs today and "
                f"{baseline.get('programs_in_matrix')} when the baseline was taken are not one trend")
    return ""


def class_verdicts(baseline, numbers: dict) -> list:
    """One (class, old, new, verdict) row per class: each class is judged on its own, so a class
    that got worse is named even when another got better."""
    rows = []
    for name, pk, ck in CLASSES:
        old = (baseline.get(pk), baseline.get(ck))
        new = (numbers.get(pk), numbers.get(ck))
        if None in old or None in new:
            rows.append((name, old, new, "unrecorded"))
            continue
        worse = new[0] > old[0] or new[1] > old[1]
        better = new[0] < old[0] or new[1] < old[1]
        verdict = "worse" if worse else ("better" if better else "held")
        rows.append((name, old, new, verdict))
    return rows


def format_class_rows(rows: list, news: bool = False) -> list:
    """One line per class. `news` is for the case where the two runs' populations differ: the counts
    are then over two different populations and saying `verdict=worse` about them would read as a
    trend that was not measured -- the per-program comparison is the verdict there, and these lines
    are what it is a verdict about."""
    lines = []
    for name, old, new, verdict in rows:
        if verdict == "unrecorded":
            lines.append(f"[E9] class {name:<8} not recorded by the baseline")
            continue
        tail = ("news, not a verdict: the populations differ" if news
                else f"verdict={verdict}")
        lines.append(f"[E9] class {name:<8} programs {old[0]}->{new[0]}   cells {old[1]}->{new[1]}"
                     f"   {tail}")
    return lines


def member_lines(in_matrix: list) -> list:
    """Every class's member seeds, one line per class: the evidence a count is not.

    A count says a class moved; a list says which programs moved it, and a seed is where the next
    step starts (`progen.py show --seed N` draws the program, `--out/failures/` holds it). The counts
    alone are why every attribution of the 2026-11-17 reading had to be made on the host, by hand,
    from a directory the gate does not bring back."""
    lines = []
    for name, names in CLASS_TOKENS:
        seeds = sorted(r["seed"] for r in in_matrix
                       if any(o in names for o in r["cells"].values()))
        lines.append(f"[E9] {name}: {' '.join(map(str, seeds)) if seeds else '(none)'}")
    beside = collections.defaultdict(set)
    for r in in_matrix:
        for outcome in set(r["cells"].values()):
            if outcome in BAD and outcome not in (WRONG | REFUSED | CRASHED):
                beside[outcome].add(r["seed"])
    for outcome in sorted(beside):
        lines.append(f"[E9] {outcome}: {' '.join(map(str, sorted(beside[outcome])))}")
    unrun = sorted(r["seed"] for r in in_matrix if NOT_RUN in r["cells"].values())
    lines.append(f"[E9] {NOT_RUN}: {' '.join(map(str, unrun)) if unrun else '(none)'}")
    return lines


def compare_states(states: dict, summary: dict) -> dict:
    """The programs both runs had in their matrix, compared one by one.

    `states` is the baseline's `programs_by_class` (seed -> the classes it reached); the run's own
    are the summary's. It returns the whole judgement, and the two kinds of absence are the point:

      worse    in both matrices and it reaches a class the baseline's record of it did not -- a
               program that went `ok -> wrong`, or `wrong -> wrong+run_error`, has got worse even
               when another class came down (453 left the wrong class by turning its wrong cells
               into `RUN_ERROR`, which is not a repair);
      better   in both matrices and a class it had is gone;
      left     in the baseline's matrix and not in this one *because the generator rejects it now*:
               the compiler declining a program it used to accept is a legal reduction -- printed,
               counted, and not a regression. It is the author's rule of 2026-11-17 (165 and 202
               stopped crashing and started being legally rejected; 273/227 became 275/225);
      lost     in the baseline's matrix and absent from this run's records: the run did not reach
               it. Not run is not passed, so this is a failure, and it is a different sentence from
               `left` -- mixing the two is how a gate becomes a wall that everybody ignores.
      entered  the other direction: a program this run has and the baseline's matrix did not. News,
               not a regression: the baseline never measured it."""
    base = {int(seed): list(tokens) for seed, tokens in states.items()}
    today = {int(seed): list(tokens)
             for seed, tokens in (summary.get("programs_by_class") or {}).items()}
    rejected = {int(seed): (reason or "?")
                for seed, reason in (summary.get("rejected_programs") or {}).items()}
    unmeasured = {int(seed) for seed in (summary.get("programs_unmeasured") or [])}
    both = sorted(set(base) & set(today))
    worse, better = [], []
    for seed in both:
        old, new = set(base[seed]), set(today[seed])
        if new - old:
            worse.append((seed, base[seed], today[seed]))
        elif old - new:
            better.append((seed, base[seed], today[seed]))
        elif not new and seed in unmeasured:
            # Nothing reached and cells left unrun: a class that disappeared into `NOT_RUN` has not
            # gone away, it has stopped being measured.
            worse.append((seed, base[seed], today[seed]))
    gone = sorted(set(base) - set(today))
    return dict(both=both, worse=worse, better=better,
                left=[(seed, rejected[seed]) for seed in gone if seed in rejected],
                lost=[seed for seed in gone if seed not in rejected],
                entered=sorted(set(today) - set(base)))


def new_baseline(summary: dict, matrix: str, seeds: str, note: str = "") -> dict:
    """The file `--update-baseline` writes: the three numbers, the population they were measured on,
    and *every program's classes* (`programs_by_class`), so that the next run can be compared with
    this one program by program even if the population moves again. Without that map the intersection
    a moved population must be judged on cannot be computed at all, which is the state this file was
    in on 2026-11-17: numbers and a population, no programs."""
    numbers = {k: summary[k] for k in RATCHET}
    numbers.update(
        what="the E9 three numbers a merge is compared with (only down, never up)",
        matrix=matrix, pin=summary["pin"], seeds=seeds,
        recorded_on=datetime.date.today().isoformat(),
        programs=summary["programs"], generator_rejects=summary["generator_rejects"],
        programs_in_matrix=summary["programs_in_matrix"],
        cell_outcomes=summary["cell_outcomes"],
        programs_by_class={str(seed): tokens
                           for seed, tokens in summary["programs_by_class"].items()})
    if note:
        numbers["note"] = note
    return numbers


def judge(baseline, summary: dict, matrix: str, seeds: str, name: str,
          final: bool = True) -> tuple:
    """Print the verdict of one run against one baseline and return `(exit code, what was found)`.

    The comparison is per program over the programs both runs had in their matrix (`compare_states`),
    and it is the only one that survives a moved population. Everything that is not a comparison is
    said out loud and is a failure rather than a pass, for the reason `test/tools/e9diff/README.md`
    gives: on 2026-11-17 the population moved by two legal rejects, the comparison stopped, the run
    exited 0 and the wrong class went from 7 programs to 12 while nobody was told. `final=False` is a
    recording run (`--update-baseline`): the same reading, and no verdict line, because the file that
    is about to be written is the point the ratchet moves from and not a judgement of this one."""
    found = dict(worse=[], better=[], left=[], lost=[], entered=[], both=[], rose={}, fell={},
                 cannot_compare="")
    if not baseline:
        found["cannot_compare"] = "no baseline recorded"
        if final:
            print(f"[E9] no baseline at {name}: a run with nothing to compare against has not been "
                  f"judged, and a ratchet that is not there is not a pass -- record one with "
                  f"--update-baseline")
        return 1, found
    if baseline.get("matrix") != matrix:
        # By design, not a failure: only the full run is a ratchet (a slow run is another sample of
        # the generator, not a smaller trend), and it says so instead of a silent "held".
        if final:
            print(f"[E9] baseline {name}: not compared -- the baseline is a "
                  f"{baseline.get('matrix')} run; only the full run is a ratchet")
        return 0, found
    if baseline.get("seeds") != seeds:
        if final:
            print(f"[E9] baseline {name}: not compared -- the baseline covers seeds "
                  f"{baseline.get('seeds')}, this run is {seeds}")
        return 0, found
    moved = population_break(baseline, summary)
    if moved:
        print(f"[E9] baseline {name}: {moved}")
        print("[E9] a moved population is not a reason to stop comparing: the programs this run "
              "and the baseline both had in their matrix are compared one by one")
    rows = class_verdicts(baseline, summary)
    for line in format_class_rows(rows, news=bool(moved)):
        print(line)
    found["rose"] = {k: (baseline[k], summary[k]) for k in RATCHET
                     if k in summary and summary[k] > baseline.get(k, summary[k])}
    found["fell"] = {k: (baseline[k], summary[k]) for k in RATCHET
                     if k in summary and summary[k] < baseline.get(k, summary[k])}
    states = baseline.get("programs_by_class")
    if not isinstance(states, dict) or not states:
        if moved:
            found["cannot_compare"] = ("the baseline records no per-program outcomes, so the "
                                       "programs both runs had in their matrix cannot be found")
            print("[E9] the baseline records no per-program outcomes, so which programs both runs "
                  "had in their matrix cannot be found: the intersection cannot be compared, and a "
                  "comparison that cannot be made is a failure, not a pass -- record the baseline "
                  "again (--update-baseline) from a run that writes them")
            if final:
                print("[E9] the comparison cannot be made: this run is a red over a baseline that "
                      "says nothing program by program, which is the state the gate was in on "
                      "2026-11-17 when 12 wrong programs exited 0")
            return 1, found
        return _aggregate_verdict(found, final)
    cmp = compare_states(states, summary)
    found.update(cmp)
    print(f"[E9] compared on the intersection: {len(cmp['both'])} program(s) in both matrices"
          + (f"; {len(cmp['left'])} left as generator rejects" if cmp["left"] else "")
          + (f"; {len(cmp['lost'])} never ran" if cmp["lost"] else "")
          + (f"; {len(cmp['entered'])} entered" if cmp["entered"] else ""))
    if cmp["entered"]:
        print("[E9] entered the matrix (news, not a regression -- the baseline never measured "
              "them): " + " ".join(map(str, cmp["entered"])))
    if cmp["left"]:
        print("[E9] left the matrix because the generator rejects them now (legal, not worse): "
              + ", ".join(f"{seed} ({reason})" for seed, reason in cmp["left"]))
    if cmp["better"]:
        print("[E9] better: " + " ".join(f"{seed} {state_text(old)}->{state_text(new)}"
                                        for seed, old, new in cmp["better"]))
    if cmp["worse"]:
        print("[E9] worse: " + " ".join(str(seed) for seed, _old, _new in cmp["worse"]))
        for seed, old, new in cmp["worse"]:
            print(f"[E9]   seed {seed}: {state_text(old)} -> {state_text(new)}")
    if cmp["lost"]:
        print("[E9] LOST: " + " ".join(map(str, cmp["lost"])) + " -- the baseline had these in its "
              "matrix and this run has no record of them; a program that was not run is not a "
              "program that passed")
    if not cmp["both"]:
        print("[E9] the intersection is empty: no program was in both runs' matrix, so there is "
              "nothing to compare -- and \"nothing to compare\" is a failure, not a pass")
        return 1, found
    if found["worse"] or found["lost"]:
        if final:
            print(f"[E9] ABOVE THE BASELINE: {len(found['worse'])} program(s) worse and "
                  f"{len(found['lost'])} lost, on the intersection")
        return 1, found
    if found["rose"] and not moved:
        if final:
            print("[E9] ABOVE THE BASELINE: "
                  + ", ".join(f"{k} {base}->{now}" for k, (base, now) in found["rose"].items()))
        return 1, found
    if found["fell"] and not moved:
        print("[E9] below the baseline (record it with --update-baseline): "
              + ", ".join(f"{k} {base}->{now}" for k, (base, now) in found["fell"].items()))
    if final:
        print("[E9] baseline held: no program in the intersection got worse")
    return 0, found


def _aggregate_verdict(found: dict, final: bool) -> tuple:
    """The comparison of a baseline that predates `programs_by_class`, over a population that did not
    move: the six numbers, as this tool always compared them. Kept so that an old baseline is still
    read the way it was recorded instead of being called unrecordable."""
    if found["fell"]:
        print("[E9] below the baseline (record it with --update-baseline): "
              + ", ".join(f"{k} {base}->{now}" for k, (base, now) in found["fell"].items()))
    if found["rose"]:
        if final:
            print("[E9] ABOVE THE BASELINE: "
                  + ", ".join(f"{k} {base}->{now}" for k, (base, now) in found["rose"].items()))
        return 1, found
    if final:
        print("[E9] baseline held: no number rose")
    return 0, found


def run_summary(rows: list, matrix: str, seeds: str) -> dict:
    rejected = [r for r in rows if r.get("rejected")]
    in_matrix = [r for r in rows if not r.get("rejected")]
    summary = dict(what="E9 differential: wrong values, refusals and crashes per program",
                   matrix=matrix, pin=build_id(), seeds=seeds, programs=len(rows),
                   generator_rejects=len(rejected),
                   reject_reasons=dict(collections.Counter(r["rejected"] for r in rejected)),
                   programs_in_matrix=len(in_matrix), cells_per_program=len(CELLS),
                   cells_total=sum(len(r["cells"]) for r in in_matrix),
                   # The per-program half of the reading: which classes each program in the matrix
                   # reached, and which seeds are rejects and why. It is what a later run is
                   # compared with (`compare_states`), and what `--update-baseline` writes down.
                   programs_by_class={r["seed"]: program_state(r) for r in in_matrix},
                   rejected_programs={r["seed"]: (r["rejected"] or "?") for r in rejected},
                   programs_unmeasured=sorted(r["seed"] for r in in_matrix
                                              if NOT_RUN in r["cells"].values()))
    summary.update(three_numbers(in_matrix))
    return summary


def cmd_run(a) -> int:
    global OUT, WORK, CELLS
    matrix = a.matrix
    OUT = Path(a.out).resolve()
    if OUT == REPO_ROOT.resolve() or REPO_ROOT.resolve() in OUT.parents:
        print(f"--out {OUT} is inside the tree: a run writes outside it (use a directory under the "
              f"temporary directory)", file=sys.stderr)
        return 2
    CELLS = cells_of(matrix)
    count = a.n if a.n is not None else MATRICES[matrix]["n"]
    # The outputs of an earlier run into the same directory go first: a run that dies must not
    # leave a readable `e9_cells.jsonl` behind that `analyze` would report as this run's.
    for name in ("work", "failures", "e9_cells.jsonl", "e9_run.json", "e9_summary.json",
                 "e9_failing.jsonl", "e9_reduced.json", "e9_reduced"):
        target = OUT / name
        if target.is_dir():
            shutil.rmtree(target, ignore_errors=True)
        elif target.exists():
            target.unlink()
    OUT.mkdir(parents=True, exist_ok=True)
    WORK = OUT / "work"
    WORK.mkdir(parents=True, exist_ok=True)
    seeds = list(range(a.start, a.start + count))
    print(f"[E9] matrix {matrix}: {len(PIPES)} pipelines, {len(CELLS)} cells per program, "
          f"seeds {seeds[0]}..{seeds[-1]}")
    started = time.monotonic()
    rows = pmap(one, seeds, a.jobs)
    seconds = time.monotonic() - started
    with open(OUT / "e9_cells.jsonl", "w", encoding="utf-8") as f:
        for r in rows:
            f.write(json.dumps(r) + "\n")
    shutil.rmtree(WORK, ignore_errors=True)
    seeds_text = f"{seeds[0]}..{seeds[-1]}"
    summary = run_summary(rows, matrix, seeds_text)
    (OUT / "e9_run.json").write_text(json.dumps(summary, indent=1) + "\n",
                                     encoding="utf-8", newline="\n")
    print(f"[E9] pin {summary['pin']}")
    print(f"[E9] seeds {seeds_text}: {summary['programs']} programs, "
          f"{summary['generator_rejects']} generator rejects, "
          f"{summary['programs_in_matrix']} in the matrix, {summary['cells_total']} cells")
    print(f"[E9] wrong {summary['programs_wrong']}  refused {summary['programs_refused']}  "
          f"crashed {summary['programs_crashed']}     "
          f"(cells: wrong {summary['cells_wrong']}, refused {summary['cells_refused']}, "
          f"crashed {summary['cells_crashed']})")
    if summary["other_findings"]:
        print(f"[E9] beside the three numbers: {summary['other_findings']}")
    # Each class's member seeds, on a line of their own: this is the evidence, and it travels on
    # stdout because `--out` is outside the tree and the gate does not bring it back.
    for line in member_lines([r for r in rows if not r.get("rejected")]):
        print(line)
    print(f"[E9] {seconds:.0f} s, {a.jobs} programs at a time, "
          f"{summary['cells_total'] / max(seconds, 1e-9):.1f} cells/s")
    # The verdict is read against the baseline this run found, before anything is written: a
    # re-record that reported only "held" -- because it then compared the file it had just written
    # with itself -- would launder the very movement that made somebody re-record.
    target = Path(a.baseline).resolve() if a.baseline else BASELINE
    old_baseline = read_baseline(target)
    code, found = judge(old_baseline, summary, matrix, seeds_text, target.name,
                        final=not a.update_baseline)
    if not a.update_baseline:
        return code
    if matrix != "full":
        print("[E9] the ratchet is the full run; --update-baseline refused", file=sys.stderr)
        return code
    numbers = new_baseline(summary, matrix, seeds_text, a.baseline_note)
    # `ensure_ascii=False`: the note is written in whatever language whoever records it thinks in,
    # and a note that reads `\u4e2d\u6587` is a note nobody reads before subtracting one baseline
    # from another.
    target.write_text(json.dumps(numbers, indent=1, ensure_ascii=False) + "\n",
                      encoding="utf-8", newline="\n")
    print(f"[E9] baseline written: {target}")
    against = []
    if found["worse"]:
        against.append(f"{len(found['worse'])} program(s) worse ("
                       + " ".join(str(seed) for seed, _old, _new in found["worse"]) + ")")
    if found["lost"]:
        against.append(f"{len(found['lost'])} lost (" + " ".join(map(str, found["lost"])) + ")")
    if found["cannot_compare"]:
        against.append(f"the baseline it replaced was not comparable: {found['cannot_compare']}")
    print("[E9] recorded as the new zero"
          + (f"; against the baseline it replaced, {', '.join(against)}" if against
             else "; against the baseline it replaced, nothing was worse")
          + " -- the new file is the point this ratchet moves from, not a judgement that the old "
            "numbers did not happen")
    return 0


# ---------------------------------------------------------------------------------------- analysis
def column(rec: dict, backend: str, collector: str) -> dict:
    return {pid: rec["cells"].get(cell_id(pid, backend, collector)) for pid, _ in PIPES}


def stage_of(pid: str) -> str:
    fixed = {"p00_none": "(no pass: front end or back end)", "p01_macro": "std::macro",
             "p10_opt": "std::opt (integrated fixpoint)"}
    return fixed[pid] if pid in fixed else "std::cxx::" + pid.split("+")[1]


def localize(rec: dict):
    """The first pipeline whose node VM result is bad; None when the node VM is clean on every
    prefix -- then the fault is in a back end or a collector, not in a pass."""
    col = column(rec, "std::nvm", "generational")
    for pid, _ in PIPES:
        if col.get(pid) in BAD:
            return pid
    return None


def signature(rec: dict) -> dict:
    """The root cause of a failing program: the pipeline stage it localizes to on the node VM, or
    the back ends that fail while every prefix of the node VM is clean, with the first bad cell's
    outcome and diagnostic. Two programs with the same signature have one cause until one is
    reduced. The key is the campaign's: the first failing pipeline prefix, in the order of the
    matrix."""
    bad = rec["bad"]
    loc = localize(rec)
    backends = sorted({cid.split("|")[1] for cid in bad})
    if loc:
        pid = loc
        info = bad[cell_id(pid, "std::nvm", "generational")]
        where = "pipeline:" + stage_of(pid)
    else:
        first = backends[0]
        pid = min(cid.split("|")[0] for cid in bad if cid.split("|")[1] == first)
        info = next(v for c, v in sorted(bad.items()) if c.startswith(pid + "|" + first + "|"))
        where = "backend:" + ",".join(backends) + " first bad at " + stage_of(pid)
    collectors = sorted({cid.split("|")[2] for cid in bad})
    return dict(where=where, outcome=info["outcome"], diag=info.get("diag", ""),
                backends=backends, collectors=collectors, first_pipeline=pid)


def key_of(sig: dict) -> str:
    return f"{sig['where']} | {sig['outcome']}" + (f":{sig['diag']}" if sig["diag"] else "")


def load_cells(out: Path) -> list:
    path = out / "e9_cells.jsonl"
    if not path.exists():
        print(f"{path} does not exist: run `e9diff.py run` first", file=sys.stderr)
        return []
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]


def cmd_analyze(a) -> int:
    global OUT, CELLS
    OUT = Path(a.out).resolve()
    rows = load_cells(OUT)
    if not rows:
        return 2
    CELLS = [tuple(cid.split("|")) for cid in rows[0]["cells"]]
    rejected = [r for r in rows if r.get("rejected")]
    in_matrix = [r for r in rows if not r.get("rejected")]
    failing = [r for r in in_matrix if r.get("bad")]
    per_cell = collections.defaultdict(collections.Counter)
    for r in in_matrix:
        for cid, o in r["cells"].items():
            pid, backend, collector = cid.split("|")
            per_cell[f"{backend}/{collector}"][o] += 1
    groups = collections.defaultdict(list)
    for r in failing:
        sig = signature(r)
        r["signature"] = sig
        groups[key_of(sig)].append(r["seed"])
    per_pipe = {pid: sum(1 for r in failing if any(c.startswith(pid + "|") for c in r["bad"]))
                for pid, _ in PIPES}
    per_backend = {b: sum(1 for r in failing if any(c.split("|")[1] == b for c in r["bad"]))
                   for b in sorted({c.split("|")[1] for r in in_matrix for c in r["cells"]})}
    per_collector = {c: sum(1 for r in failing if any(k.split("|")[2] == c for k in r["bad"]))
                     for c in sorted({c.split("|")[2] for r in in_matrix for c in r["cells"]})}
    legal_refusals = collections.Counter()
    for r in in_matrix:
        for cid, o in r["cells"].items():
            if o == "REFUSED":
                legal_refusals[cid.split("|")[0]] += 1
    numbers = three_numbers(in_matrix)
    summary = dict(
        pin=build_id(),
        seeds=f"{rows[0]['seed']}..{rows[-1]['seed']}" if rows else "",
        programs=len(rows), generator_rejects=len(rejected),
        reject_reasons=dict(collections.Counter(r["rejected"] for r in rejected)),
        programs_in_matrix=len(in_matrix), cells_per_program=len(CELLS),
        cells_total=sum(len(r["cells"]) for r in in_matrix),
        programs_wrong=numbers["programs_wrong"], programs_refused=numbers["programs_refused"],
        programs_crashed=numbers["programs_crashed"],
        cells_wrong=numbers["cells_wrong"], cells_refused=numbers["cells_refused"],
        cells_crashed=numbers["cells_crashed"],
        cell_outcomes=numbers["cell_outcomes"], other_findings=numbers["other_findings"],
        programs_failing=len(failing),
        programs_failing_share=round(len(failing) / len(in_matrix), 4) if in_matrix else None,
        failing_programs_per_pipeline=per_pipe,
        failing_programs_per_backend=per_backend,
        failing_programs_per_collector=per_collector,
        legal_refusals_cells_per_pipeline=dict(legal_refusals),
        cell_outcomes_by_column={k: dict(v) for k, v in per_cell.items()},
        root_causes=[dict(key=k, programs=len(v), seeds=sorted(v)[:40])
                     for k, v in sorted(groups.items(), key=lambda kv: (-len(kv[1]), kv[0]))])
    (OUT / "e9_summary.json").write_text(json.dumps(summary, indent=1) + "\n",
                                         encoding="utf-8", newline="\n")
    with open(OUT / "e9_failing.jsonl", "w", encoding="utf-8") as f:
        for r in failing:
            f.write(json.dumps(dict(seed=r["seed"], signature=r["signature"], bad=r["bad"],
                                    expected=r.get("expected"))) + "\n")
    print(f"[E9] seeds {summary['seeds']}: {len(failing)} of {len(in_matrix)} programs fail, "
          f"{len(summary['root_causes'])} root cause(s)")
    print(f"[E9] wrong {summary['programs_wrong']}  refused {summary['programs_refused']}  "
          f"crashed {summary['programs_crashed']}")
    # The classes' members, one line each: `run` prints them too, and the seeds are what an
    # attribution quoting a count cannot say.
    for line in member_lines(in_matrix):
        print(line)
    for group in summary["root_causes"]:
        print(f"  {group['programs']:3d} programs  {group['key']}")
        print(f"      seeds {' '.join(map(str, group['seeds'][:16]))}"
              + (" ..." if group["programs"] > 16 else ""))
    for name, counts in sorted(summary["cell_outcomes_by_column"].items()):
        bad = {o: n for o, n in counts.items() if o in BAD}
        print(f"  {name:<24} " + (", ".join(f"{o} {n}" for o, n in sorted(bad.items()))
                                  if bad else "all OK"))
    if summary["legal_refusals_cells_per_pipeline"]:
        print("[E9] located refusals (legal) per pipeline: "
              + ", ".join(f"{p} {n}"
                          for p, n in sorted(summary["legal_refusals_cells_per_pipeline"].items())))
    print(f"[E9] wrote {OUT / 'e9_summary.json'} and {OUT / 'e9_failing.jsonl'}")
    return 0


# -------------------------------------------------------------------------------------- re-judging
def cmd_compare(a) -> int:
    """Judge a finished run again, without running it again.

    A full run is 25 minutes on this host and its `--out` is outside the tree, so re-reading one --
    or asking "what would this baseline have said about that run?" -- was not possible: every
    attribution of a reading had to be made by hand on the host. This reads `--out` and answers it,
    and `--baseline FILE` is how the comparison itself is tested: a recording of the same run must
    pass, a copy of an older one must fail, and neither costs a compiler."""
    global OUT, CELLS
    OUT = Path(a.out).resolve()
    rows = load_cells(OUT)
    if not rows:
        return 2
    CELLS = [tuple(cid.split("|")) for cid in rows[0]["cells"]] or CELLS
    recorded = {}
    path = OUT / "e9_run.json"
    if path.exists():
        try:
            recorded = json.loads(path.read_text(encoding="utf-8"))
        except ValueError:
            recorded = {}
    matrix = recorded.get("matrix") or "full"
    seeds = recorded.get("seeds") or f"{rows[0]['seed']}..{rows[-1]['seed']}"
    summary = run_summary(rows, matrix, seeds)
    # The pin is the run's own: this is a reading of that run, not of whatever this checkout is.
    summary["pin"] = recorded.get("pin") or summary["pin"]
    print(f"[E9] re-reading {OUT}: matrix {matrix}, seeds {seeds}")
    print(f"[E9] pin {summary['pin']}")
    print(f"[E9] wrong {summary['programs_wrong']}  refused {summary['programs_refused']}  "
          f"crashed {summary['programs_crashed']}     "
          f"(cells: wrong {summary['cells_wrong']}, refused {summary['cells_refused']}, "
          f"crashed {summary['cells_crashed']})")
    if summary["other_findings"]:
        print(f"[E9] beside the three numbers: {summary['other_findings']}")
    for line in member_lines([r for r in rows if not r.get("rejected")]):
        print(line)
    target = Path(a.baseline).resolve() if a.baseline else BASELINE
    code, _found = judge(read_baseline(target), summary, matrix, seeds, target.name)
    return code


# ---------------------------------------------------------------------------------------- reduction
def cmd_reduce(a) -> int:
    global OUT
    OUT = Path(a.out).resolve()
    from model import evaluate
    from reduce import Reducer

    path = OUT / "e9_failing.jsonl"
    if not path.exists():
        print(f"{path} does not exist: run `e9diff.py analyze` first", file=sys.stderr)
        return 2
    rows = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]
    groups = collections.defaultdict(list)
    for r in rows:
        sig = r["signature"]
        groups[(sig["where"], sig["outcome"], sig["diag"])].append(r)
    outdir = OUT / "e9_reduced"
    # `collect` reads `rc*.cml` from here, so an earlier run's programs cannot be left in it.
    shutil.rmtree(outdir, ignore_errors=True)
    outdir.mkdir(parents=True, exist_ok=True)
    workdir = OUT / "work"
    workdir.mkdir(parents=True, exist_ok=True)
    reduced = []
    for gi, (key, members) in enumerate(sorted(groups.items(), key=lambda kv: (-len(kv[1]), kv[0]))):
        if gi >= a.max_groups:
            break
        rep = min(members, key=lambda r: r["seed"])
        sig = rep["signature"]
        bad = rep["bad"]
        # The failing cell: the localizing column when the pipeline localizes, else the first bad
        # cell of the first bad back end.
        if sig["where"].startswith("pipeline:"):
            cid = cell_id(sig["first_pipeline"], "std::nvm", "generational")
        else:
            cid = next(c for c in sorted(bad)
                       if c.startswith(sig["first_pipeline"] + "|" + sig["backends"][0] + "|"))
        pid, backend, collector = cid.split("|")
        rec = dict(group=" | ".join(key), programs=len(members), seed=rep["seed"], cell=cid)
        if backend == CPP_BACKEND:
            rec["reduced"] = "not attempted: reduce.py runs the camel back ends only"
            reduced.append(rec)
            continue
        prog, expected, _ = generate(rep["seed"])
        passes = ([] if collector == "-" else ["--gc", collector]) + PASSES[pid] + [backend]
        res = R.run_program(emit(prog), prog.a["env"], expected, {"fail": passes, "ref": []},
                            workdir, f"progen_{rep['seed']}", modules=emit_modules(prog))
        target = R.signature(res["fail"])
        rec["target"] = target
        rec["ref_outcome"] = res["ref"]["outcome"]
        if res["fail"]["outcome"] not in BAD:
            rec["reduced"] = f"did not reproduce on re-run ({res['fail']['outcome']})"
            reduced.append(rec)
            continue
        reducer = Reducer(prog, passes, target, reference_ok=res["ref"]["outcome"] == "OK",
                          max_tests=a.max_tests, log=print)
        small, small_passes = reducer.run()
        name = f"rc{gi:02d}_seed{rep['seed']}"
        small.a["module"] = name
        expected_small = evaluate(small)
        source = emit(small)
        (outdir / f"{name}.cml").write_text(source, encoding="utf-8", newline="\n")
        for stem, text in emit_modules(small).items():
            (outdir / f"{stem}.cml").write_text(text, encoding="utf-8", newline="\n")
        seen = collections.Counter()
        shown = None
        for _ in range(5):
            info = R.run_program(source, small.a["env"], expected_small, {"fail": small_passes},
                                 outdir, name, modules=emit_modules(small))["fail"]
            seen[info["outcome"]] += 1
            if info["outcome"] in BAD and shown is None:
                shown = info
        rec.update(reduced=f"{name}.cml", reduced_lines=len(source.splitlines()),
                   passes=small_passes, expected=expected_small[:20],
                   observed=(shown or {}).get("got"), observed_outcome=(shown or {}).get("outcome"),
                   detail=(shown or {}).get("detail"), diag=(shown or {}).get("diag"),
                   reproduced_of_5=dict(seen), tests=reducer.tests)
        reduced.append(rec)
        print(f"[E9] {rec['group']}: {name}.cml, {rec['reduced_lines']} lines, {dict(seen)} of 5")
    (OUT / "e9_reduced.json").write_text(json.dumps(reduced, indent=1) + "\n",
                                         encoding="utf-8", newline="\n")
    print(f"[E9] wrote {OUT / 'e9_reduced.json'}")
    return 0


# ------------------------------------------------------------------------------------- collection
def plan_programs() -> set:
    """The programs `test/plans/feat/opt/e9.plan.toml` already holds, by file stem."""
    try:
        text = PLAN.read_text(encoding="utf-8")
    except OSError:
        return set()
    return set(re.findall(r'case\s*=\s*"[^"]*/([\w.-]+)\.cml"', text))


def fast_cells(path: Path) -> tuple:
    """The claim a reduced program makes in the fast layer, with the arguments the plan gives it:
    the same output with no pass and through `std::opt`, on the four back ends. `(baseline lines,
    [(passes, backend, info)])`; the baseline is what the node VM prints with no pass, which is the
    only oracle a program of this campaign has -- it was reduced from a run of one graph, not
    drawn by the generator."""
    probe = R.run_cell(path, ["std::nvm"], {}, [], CELL_TIMEOUT)
    if probe["outcome"] == "OK":
        baseline = []
    elif probe["outcome"] == "MISMATCH":
        baseline = probe.get("got", [])
    else:
        return None, [([], "std::nvm", probe)]
    cells = []
    for passes in ([], ["std::opt"]):
        for backend in FAST_BACKENDS:
            info = R.run_cell(path, [*passes, "std::" + backend], {}, baseline, CELL_TIMEOUT)
            cells.append((passes, "std::" + backend, info))
    return baseline, cells


def plan_entry(name: str, expected: list, desc_pass: dict) -> str:
    """The two tests of one program, in the form the fast layer already uses: one without a pass,
    one through `std::opt`, each on the four back ends (`backends` of the plan's defaults)."""
    exact = "".join(line + "\\n" for line in expected)
    blocks = []
    for label, passes in (("without_the_pass", []), ("opt", ["std::opt"])):
        args = '"${case}", ' + ('"std::opt", ' if passes else "") + '"${backend}"'
        blocks.append(f'[[tests]]\n'
                      f'name = "{name}.{label}"\n'
                      f'desc = "{desc_pass[label]}"\n'
                      f'case = "../../../cases/opt/e9/{name}.cml"\n'
                      f'args = [{args}]\n'
                      f'expect_exit = 0\nexpect_stdout_exact = "{exact}"\n')
    return "\n".join(blocks)


PLAN_HEADER = """id = "opt.e9"
order = 336
tier = "fast"
tags = ["feat", "opt", "defunctionalize", "inline", "fold", "dce"]
desc = "The optimizer answers what it cannot do with a diagnostic, not with a crash: the E9 reduced programs of the differential campaign"

# The reduced programs of the E9 differential campaign (test/tools/e9diff), each one a failure the
# differential run found and `reduce` shrank. What each case holds is what the program promises: the
# same output with no pass and through the whole optimizer, on the four back ends. A pass that
# crashes on one of them, or prints something else, fails it.
#
# `e9diff.py collect --from DIR` writes these entries: it proves a program green before it installs
# it, so a case that is red the day it lands cannot be added by it.

[defaults]
kind = "behavior"
backends = ["nvm", "fvm", "jit", "prl"]
"""


def cmd_collect(a) -> int:
    """Takes the programs a `reduce` run produced (`DIR/rc*.cml`) into the fast layer, and says of
    every program -- installed or not -- whether it is green now. A program the campaign reduced but
    cannot install is listed with the cell that still fails: that list is the inventory of what the
    fast layer is missing and why."""
    source = Path(a.from_dir).resolve()
    names = sorted(p.stem for p in source.glob("rc*.cml"))
    if not names:
        print(f"{source} holds no rc*.cml", file=sys.stderr)
        return 2
    collected = plan_programs()
    workdir = source / ".work"
    workdir.mkdir(parents=True, exist_ok=True)
    green, red, already = [], [], []
    for name in names:
        copy = workdir / f"{name}.cml"
        shutil.copyfile(source / f"{name}.cml", copy)
        for extra in source.glob(f"{name}_*.cml"):
            shutil.copyfile(extra, workdir / extra.name)
        baseline, cells = fast_cells(copy)
        failing = [c for c in cells if c[2]["outcome"] != "OK"]
        if baseline is None:
            red.append((name, cells[0][2]))
        elif failing:
            red.append((name, failing[0][2]))
        elif name in collected:
            already.append(name)
        else:
            green.append((name, baseline, copy))
    if green and not a.dry_run:
        CASES.mkdir(parents=True, exist_ok=True)
        text = PLAN.read_text(encoding="utf-8") if PLAN.exists() else PLAN_HEADER
        for name, baseline, copy in green:
            shutil.copyfile(copy, CASES / f"{name}.cml")
            for extra in workdir.glob(f"{name}_*.cml"):
                shutil.copyfile(extra, CASES / extra.name)
            text += "\n" + plan_entry(name, baseline, {
                "without_the_pass": "The program with no pass at all: what it prints, which the "
                                    "reduced finding says the optimizer must keep.",
                "opt": "The whole optimizer, on a program the differential run reduced."})
        PLAN.parent.mkdir(parents=True, exist_ok=True)
        PLAN.write_text(text, encoding="utf-8", newline="\n")
    shutil.rmtree(workdir, ignore_errors=True)
    print(f"[E9] fast layer: {len(names)} programs in {source}")
    print(f"[E9] collected: {len(collected)} programs in {PLAN.relative_to(REPO_ROOT).as_posix()}"
          if PLAN.exists() else "[E9] collected: no plan yet")
    for name in already:
        print(f"  already collected   {name}")
    for name, baseline, copy in green:
        state = "would collect  " if a.dry_run else "collected      "
        print(f"  {state} {name}  ({len(baseline)} expected line(s), "
              f"8 cells green)")
    for name, info in red:
        print(f"  cannot collect yet  {name}  {info['outcome']}"
              f"{(':' + info['diag']) if info.get('diag') else ''}: "
              f"{(info.get('detail') or '')[:120]}")
    if a.dry_run:
        print("[E9] --dry-run: nothing written")
    return 1 if red else 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--camel", help="the install to measure: the `camel` binary to run, else this "
                                    "tree's out/latest/bin/camel (test/tools/camel_arg.py)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("run")
    p.add_argument("--matrix", choices=sorted(MATRICES), default="slow")
    p.add_argument("--n", type=int, default=None,
                   help="programs (default: 50 for slow, 500 for full)")
    p.add_argument("--start", type=int, default=1)
    p.add_argument("--out", required=True, help="where the run writes, outside the tree")
    p.add_argument("--jobs", type=int, default=4, help="programs at a time")
    p.add_argument("--update-baseline", action="store_true",
                   help=f"record this run's three numbers and its per-program classes in "
                        f"{BASELINE.name} (full matrix only)")
    p.add_argument("--baseline-note", default="",
                   help=f"a line written into {BASELINE.name} beside the numbers: why this zero was "
                        f"taken, in the words of whoever took it")
    # Which file the run is judged against. Empty is `baseline.json` beside this script, which is
    # the one the gate reads; another one is how the comparison itself is exercised (a recording of
    # the same run has to pass, a copy of an older one has to fail) without editing that file.
    p.add_argument("--baseline", default="",
                   help=f"the baseline to judge against, and to write with --update-baseline "
                        f"(default: {BASELINE.name} beside this script)")
    p.set_defaults(fn=cmd_run)
    p = sub.add_parser("compare")
    p.add_argument("--out", required=True, help="the --out of a finished `run`")
    p.add_argument("--baseline", default="",
                   help=f"the baseline to judge against (default: {BASELINE.name} beside this "
                        f"script)")
    p.set_defaults(fn=cmd_compare)
    p = sub.add_parser("analyze")
    p.add_argument("--out", required=True)
    p.set_defaults(fn=cmd_analyze)
    p = sub.add_parser("reduce")
    p.add_argument("--out", required=True)
    p.add_argument("--max-groups", type=int, default=12)
    p.add_argument("--max-tests", type=int, default=3000)
    p.set_defaults(fn=cmd_reduce)
    p = sub.add_parser("collect")
    p.add_argument("--from", dest="from_dir", required=True,
                   help="a directory of reduced programs, rc*.cml (the --out of `reduce`)")
    p.add_argument("--dry-run", action="store_true", help="say what would be collected")
    p.set_defaults(fn=cmd_collect)
    a = ap.parse_args(argv)
    # Every `camel` this run starts is the one `--camel` names: the rule of test/tools/camel_arg.py,
    # and the switch is an argument, never an environment variable.
    R.CAMEL = a.camel
    return a.fn(a)


if __name__ == "__main__":
    sys.exit(main())