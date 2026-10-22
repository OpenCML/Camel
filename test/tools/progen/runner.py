# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""Runs one generated program under a matrix of (rewrite passes x scheduler) and compares every
run with the reference output computed by model.py.

Outcomes of a cell:

  OK                 exit 0 and exactly the reference lines
  REFUSED            a diagnostic of a rewrite pass (stage pass:<name>) with a source position
                     and nothing printed before it: the legal way for a pipeline to decline
  REFUSED_UNLOCATED  the same without a source position, or after output
  MISMATCH           exit 0 and other output: a silent wrong result
  RUN_ERROR          a diagnostic of stage run or internal (the reference says the program
                     runs to the end, so the run should not fail; `located` is recorded)
  ERROR_NO_DIAG      non-zero exit without a structured diagnostic
  CRASH              killed by a signal
  HANG               still running at the time limit
  FRONTEND           a diagnostic of stage parse or typecheck (the generator wrote something
                     the compiler does not accept: a generator matter unless it only happens
                     with passes, which cannot be)
  NOT_RUN            not run because MAX_CRASHES cells of the program had already crashed

Reused from benchmarks/pipeline: how camel is invoked (`--output-format json`) and how its
diagnostics array is split from the program's own output (run.split_diagnostics), what counts
as a located diagnostic (classify.located, classify.errors_of), and the fixed and frozen random
pipeline sets (pipelines.FIXED, pipelines.random_pipelines) for `--matrix frozen`. That
facility's own contract compares with the zero-pass run; here the comparison is with the
reference, so a wrong zero-pass run is seen too.
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Dict, List, Optional

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[2]
sys.path.insert(0, str(REPO_ROOT / "benchmarks" / "pipeline"))
import classify as bp_classify  # noqa: E402
import pipelines as bp_pipelines  # noqa: E402
import run as bp_run  # noqa: E402

ANSI = re.compile(r"\x1b\[[0-9;]*m")

BACKENDS = ["std::nvm", "std::fvm", "std::jit", "std::prl"]

REWRITES: Dict[str, List[str]] = {
    "none": [],
    "inline": ["std::opt::inline"],
    "fold": ["std::opt::fold"],
    "cse": ["std::opt::cse"],
    "dce": ["std::opt::dce"],
    "simplify": ["std::opt"],
    "opt": ["std::opt"],
    "macro": ["std::macro"],
    "macro+opt": ["std::macro", "std::opt"],
    "devirt": ["std::opt::devirtualize"],
    "spec": ["std::opt::specialize"],
    "inline+fold": ["std::opt::inline", "std::opt::fold"],
    "fold+inline": ["std::opt::fold", "std::opt::inline"],
    "fold+cse": ["std::opt::fold", "std::opt::cse"],
    "cse+dce": ["std::opt::cse", "std::opt::dce"],
    "inline+simplify": ["std::opt::inline", "std::opt"],
    "macro+simplify": ["std::macro", "std::opt"],
    "macro+inline": ["std::macro", "std::opt::inline"],
    "simplify+simplify": ["std::opt", "std::opt"],
    "opt+opt": ["std::opt", "std::opt"],
    "inline+opt": ["std::opt::inline", "std::opt"],
    "spec+inline": ["std::opt::specialize", "std::opt::inline"],
    "devirt+inline+dce": ["std::opt::devirtualize", "std::opt::inline", "std::opt::dce"],
    "inline*3": ["std::opt::inline", "std::opt::inline", "std::opt::inline"],
    "macro+fold+cse+dce": ["std::macro", "std::opt::fold", "std::opt::cse", "std::opt::dce"],
}
QUICK = ["none", "inline", "fold", "cse", "dce", "simplify", "opt", "macro+opt"]
# `cross`: every rewrite sequence on the node VM, and these on the other schedulers too.
CROSS = ["none", "inline", "simplify", "opt", "macro+opt"]

# A crashing process can take many seconds to go away (a system crash handler collects it), so
# after this many crashed or hung cells the other cells of the program are not run (NOT_RUN).
MAX_CRASHES = 4

BAD = {"MISMATCH", "CRASH", "HANG", "RUN_ERROR", "ERROR_NO_DIAG", "REFUSED_UNLOCATED", "FRONTEND"}


def matrix(name: str = "full", backends: Optional[List[str]] = None) -> Dict[str, List[str]]:
    """Cell id -> pass list. `default` is the run without any pass (the node VM)."""
    backends = backends or BACKENDS
    cells: Dict[str, List[str]] = {"default": []}
    if name == "reference":
        return cells
    if name == "frozen":                 # the sets of benchmarks/pipeline, minus module passes
        for pid, passes in {**bp_pipelines.FIXED, **bp_pipelines.random_pipelines()}.items():
            if passes and not any(p in bp_pipelines.MODULE_PASSES for p in passes):
                cells[pid] = list(passes)
        return cells
    names = QUICK if name == "quick" else list(REWRITES)
    for r in names:
        for b in backends:
            if name == "cross" and b != "std::nvm" and r not in CROSS:
                continue
            cells[f"{r}@{b.split('::')[-1]}"] = REWRITES[r] + [b]
    return cells


def camel_exe() -> str:
    if os.environ.get("CAMEL_EXE"):
        return os.environ["CAMEL_EXE"]
    home = os.environ.get("CAMEL_HOME") or str(REPO_ROOT / "out" / "latest")
    return str(Path(home) / "bin" / ("camel.exe" if os.name == "nt" else "camel"))


def run_cell(source_path: Path, passes: List[str], env_values: dict, expected: List[str],
             timeout: float = 30.0) -> dict:
    exe = camel_exe()
    env = dict(os.environ)
    env.setdefault("CAMEL_HOME", str(Path(exe).resolve().parents[1]))
    env.update(env_values)
    cmd = [exe, "--output-format", "json", source_path.name, *passes]
    try:
        proc = subprocess.run(cmd, cwd=source_path.parent, env=env, stdin=subprocess.DEVNULL,
                              capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return {"outcome": "HANG", "detail": f"no result after {timeout:.0f} s"}
    stdout = ANSI.sub("", proc.stdout.decode("utf-8", "replace"))
    stderr = ANSI.sub("", proc.stderr.decode("utf-8", "replace"))
    output, diags = bp_run.split_diagnostics(stdout)
    errs = bp_classify.errors_of({"diagnostics": diags})
    got = output.split("\n")
    if got and got[-1] == "":
        got.pop()
    code = proc.returncode
    info = {"exit": code}
    if code < 0 or (code >= 128 and not errs):
        info.update(outcome="CRASH", detail=f"exit code {code}; {stderr.strip()[-200:]}")
    elif errs:
        d = errs[0]
        stage = d.get("data", {}).get("stage", "")
        located = bp_classify.located(d)
        info.update(diag=d.get("data", {}).get("name", ""), stage=stage, located=located,
                    detail=d.get("message", "")[:300],
                    where=d.get("range", {}).get("start", {}).get("line", 0) + 1,
                    column=d.get("range", {}).get("start", {}).get("character", 0) + 1)
        if stage in ("parse", "typecheck"):
            info["outcome"] = "FRONTEND"
        elif stage.startswith("pass:"):
            info["outcome"] = "REFUSED" if located and not got else "REFUSED_UNLOCATED"
        else:
            info["outcome"] = "RUN_ERROR"
    elif code != 0:
        info.update(outcome="ERROR_NO_DIAG", detail=f"exit code {code}; {(stderr.strip() or output.strip())[-200:]}")
    elif got == expected:
        info["outcome"] = "OK"
    else:
        info.update(outcome="MISMATCH", detail=first_difference(expected, got))
    if info["outcome"] != "OK":
        info["got"] = got[:60]
    return info


def first_difference(expected: List[str], got: List[str]) -> str:
    for i, (e, g) in enumerate(zip(expected, got)):
        if e != g:
            return f"line {i + 1}: expected {e!r}, got {g!r}"
    return f"{len(expected)} lines expected, {len(got)} printed"


def run_program(source: str, env_values: dict, expected: List[str], cells: Dict[str, List[str]],
                workdir: Optional[Path] = None, name: str = "prog", timeout: float = 30.0,
                modules: Optional[Dict[str, str]] = None) -> Dict[str, dict]:
    """Runs the cells one after the other. Returns cell id -> info. `modules`: the other
    modules of the program (file stem -> source), written beside it (feature `modules`)."""
    def go(d: Path):
        path = d / f"{name}.cml"
        path.write_text(source, encoding="utf-8", newline="\n")
        for stem, text in (modules or {}).items():
            (d / f"{stem}.cml").write_text(text, encoding="utf-8", newline="\n")
        out, crashes = {}, 0
        for cid, passes in cells.items():
            if crashes >= MAX_CRASHES:
                out[cid] = {"outcome": "NOT_RUN", "detail": f"{MAX_CRASHES} cells of this program crashed"}
                continue
            out[cid] = run_cell(path, passes, env_values, expected, timeout)
            crashes += out[cid]["outcome"] in ("CRASH", "HANG")
        return out
    if workdir is not None:
        workdir.mkdir(parents=True, exist_ok=True)
        return go(workdir)
    with tempfile.TemporaryDirectory(prefix="progen-") as t:
        return go(Path(t))


def signature(info: dict) -> str:
    """What the reducer must preserve: the outcome, and for a diagnostic its name."""
    return info["outcome"] + (":" + info["diag"] if info.get("diag") else "")
