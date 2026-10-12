"""Shrinks a failing (program, pipeline) pair while the failure stays the same.

A candidate is kept only when all of these hold:

  * it is still inside the determined subset (model.effect_violations is empty), has no
    fault that is visible without running (model.static_faults), and the reference evaluates it (so its expected output is recomputed, never carried over);
  * the run without passes prints exactly that expected output (unless the failure being
    reduced is itself in the run without passes), which also filters candidates the compiler
    does not accept;
  * the failing pipeline still gives the same outcome (and the same diagnostic name).

Steps, repeated until none applies: remove whole functions; remove statements (chunks, then
single ones); splice the statements of an `if` statement's arm in its place; replace an
expression by one of its operands of the same type or by the smallest literal of its type;
drop `macro`; drop passes from the pipeline and try the node VM as the scheduler.
"""

from __future__ import annotations

import tempfile
from pathlib import Path
from typing import Callable, List, Optional

from model import BOOL, FLOAT, INT, N, STR, Reject, effect_violations, emit, evaluate, static_faults, walk
from runner import BACKENDS, run_program, signature

STATEMENTS = {"let", "var", "expr", "ifs", "func", "case", "prog", "blk"}


def smallest(t, types) -> Optional[N]:
    if t == INT:
        return N("lit", t=t, v=0)
    if t == FLOAT:
        return N("lit", t=t, v=1.0)
    if t == BOOL:
        return N("lit", t=t, v=False)
    if t == STR:
        return N("lit", t=t, v="")
    if t is None or t[0] in ("fn", "fneff", "fnvar", "void"):
        return None
    if t[0] == "arr":
        e = smallest(t[1], types)
        return None if e is None else N("arr", [e], t=t)
    if t[0] == "tup":
        es = [smallest(x, types) for x in t[1]]
        return None if any(e is None for e in es) else N("tup", es, t=t)
    if t[0] == "rec":
        fs = types[t[1]]
        es = [smallest(ft, types) for _, ft in fs]
        return None if any(e is None for e in es) else N("rec", es, t=t, fields=[f for f, _ in fs])
    return None


def same(a: N, b: N) -> bool:
    return emit_expr(a) == emit_expr(b)


def emit_expr(n: N) -> str:
    from model import ex
    return ex(n, 0, True)


class Reducer:
    def __init__(self, prog: N, passes: List[str], target: str, reference_ok: bool = True,
                 log: Optional[Callable[[str], None]] = None, max_tests: int = 4000,
                 workdir: Optional[Path] = None, repeats: int = 1):
        """`workdir`: where the candidates are written and run. Some failures depend on the
        path of the source file (they are memory-layout dependent), so the directory of the
        original failing run should be given. `repeats`: a candidate counts as failing when
        one of that many runs fails (for failures that do not happen on every run)."""
        self.prog, self.passes, self.target = prog.clone(), list(passes), target
        self.reference_ok = reference_ok and bool(passes)
        self.log = log or (lambda s: None)
        self.tests, self.max_tests = 0, max_tests
        self.repeats = repeats
        self.tmp = None if workdir else tempfile.TemporaryDirectory(prefix="progen-reduce-")
        self.work = Path(workdir or self.tmp.name)
        self.last = None            # the run record of the last failing candidate

    def fails(self, prog: N, passes: Optional[List[str]] = None) -> bool:
        if self.tests >= self.max_tests:
            return False
        if effect_violations(prog) or static_faults(prog):
            return False
        try:
            expected = evaluate(prog)
        except Reject:
            return False
        try:
            source = emit(prog)
        except (AssertionError, ValueError, KeyError, TypeError):
            return False
        self.tests += 1
        cells = {"fail": self.passes if passes is None else passes}
        if self.reference_ok:
            cells["ref"] = []
        for _ in range(self.repeats):
            res = run_program(source, prog.a.get("env", {}), expected, cells, self.work, prog.a["module"])
            if "ref" in res and res["ref"]["outcome"] != "OK":
                return False
            if signature(res["fail"]) == self.target:
                self.last = dict(res["fail"], expected=expected)
                return True
            cells.pop("ref", None)
        return False

    # each step returns True when it changed the program
    def drop_functions(self) -> bool:
        changed = False
        funcs = self.prog.c[0]
        for f in list(funcs):
            if f.a["name"] == "main":
                continue
            i = funcs.index(f)
            del funcs[i]
            if self.fails(self.prog):
                changed = True
                self.log(f"  removed function {f.a['name']}")
            else:
                funcs.insert(i, f)
        return changed

    def statement_lists(self):
        return [n.c[0] for n in walk(self.prog) if n.k == "blk"]

    def drop_statements(self) -> bool:
        changed = False
        for lst in self.statement_lists():
            size = max(1, len(lst) // 2)
            while size >= 1 and lst:
                i = 0
                while i < len(lst):
                    chunk = lst[i:i + size]
                    del lst[i:i + size]
                    if self.fails(self.prog):
                        changed = True
                    else:
                        lst[i:i] = chunk
                        i += size
                size //= 2
        return changed

    def splice_branches(self) -> bool:
        changed = False
        for lst in self.statement_lists():
            i = 0
            while i < len(lst):
                s = lst[i]
                if s.k == "ifs":
                    for arm in s.c[1:]:
                        if arm is None:
                            continue
                        lst[i:i + 1] = arm.c[0]
                        if self.fails(self.prog):
                            changed = True
                            break
                        lst[i:i + len(arm.c[0])] = [s]
                i += 1
        return changed

    def simplify_expressions(self) -> bool:
        changed = False
        types = self.prog.a.get("types", {})
        progress = True
        while progress:
            progress = False
            for parent in list(walk(self.prog)):
                for i, x in enumerate(parent.c):
                    if not isinstance(x, N) or x.t is None or x.k in STATEMENTS or x.k == "lit":
                        continue
                    if parent.k == "call" and i == 0:
                        continue
                    cands = [c for c in x.c if isinstance(c, N) and c.t == x.t and c.k not in ("case",)]
                    if x.k == "match":
                        cands += [case.c[0] for case in x.c if case.c[0].t == x.t]
                    small = smallest(x.t, types)
                    if small is not None and not same(small, x):
                        cands.append(small)
                    before = emit(self.prog)
                    for c in cands:
                        parent.c[i] = c
                        # `parent` may have been cut out by an earlier replacement of this pass
                        if emit(self.prog) != before and self.fails(self.prog):
                            changed = progress = True
                            break
                        parent.c[i] = x
        return changed

    def drop_flags(self) -> bool:
        changed = False
        for f in self.prog.c[0]:
            if f.a.get("macro"):
                f.a["macro"] = False
                if self.fails(self.prog):
                    changed = True
                else:
                    f.a["macro"] = True
        return changed

    def shrink_pipeline(self) -> bool:
        changed = False
        i = 0
        while i < len(self.passes):
            if self.passes[i] in BACKENDS:
                i += 1
                continue
            cand = self.passes[:i] + self.passes[i + 1:]
            if self.fails(self.prog, cand):
                self.passes, changed = cand, True
            else:
                i += 1
        if self.passes and self.passes[-1] in BACKENDS and self.passes[-1] != "std::nvm":
            for cand in (self.passes[:-1], self.passes[:-1] + ["std::nvm"]):
                if cand and self.fails(self.prog, cand):
                    self.passes, changed = cand, True
                    break
        return changed

    def drop_unused_env(self):
        used = {n.a["name"] for n in walk(self.prog) if n.k == "env"}
        self.prog.a["env"] = {k: v for k, v in self.prog.a.get("env", {}).items() if k in used}
        text = emit(self.prog)
        self.prog.a["types"] = {k: v for k, v in self.prog.a.get("types", {}).items()
                                if any(k in line for line in text.splitlines() if not line.startswith("type "))}

    def run(self):
        if not self.fails(self.prog):
            raise RuntimeError("the failure does not reproduce")
        self.shrink_pipeline()
        for round_ in range(8):
            before = self.tests
            changed = self.drop_statements()
            changed |= self.drop_functions()
            changed |= self.splice_branches()
            changed |= self.simplify_expressions()
            changed |= self.drop_flags()
            changed |= self.shrink_pipeline()
            self.log(f"round {round_ + 1}: {self.tests - before} tests, "
                     f"{len(emit(self.prog).splitlines())} lines, pipeline {' '.join(self.passes) or '(none)'}")
            if not changed or self.tests >= self.max_tests:
                break
        self.drop_unused_env()
        if self.tmp:
            self.tmp.cleanup()
        return self.prog, self.passes
