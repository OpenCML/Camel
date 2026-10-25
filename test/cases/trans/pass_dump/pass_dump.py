"""Checks `camel --dump-after-each-pass <dir>` and tools/graph-diff on a macro program.

Runs `basic.cml` (a macro call `add_one(41)`) through `std::macro std::opt::inline std::nvm` and
checks that:
  - snapshot 00 is the input graph and one snapshot follows per pass that returned a graph
    (the scheduling pass consumes the graph and leaves none);
  - every snapshot has a readable DOT and a stats JSON;
  - graph-diff between the input and the `std::macro` output reports the macro call as removed,
    at its call site, and the materialized constant as added;
  - the program's own output is unchanged by the option.

Usage: python pass_dump.py [--camel PATH]   (needs only the standard library)
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[3]

sys.path.insert(0, str(REPO_ROOT / "test" / "tools"))
from camel_arg import camel_from_argv  # noqa: E402
PROGRAM = REPO_ROOT / "test" / "cases" / "trans" / "macro" / "basic.cml"
GRAPH_DIFF = REPO_ROOT / "tools" / "graph-diff" / "graph_diff.py"


# The install this case measures: `--camel <path>` names it (the author's ruling of 2026-10-25
# replaced the `CAMEL_EXE` environment switch with that argument); without it,
# `$CAMEL_HOME/bin/camel`, else this tree's `out/latest/bin/camel`. One rule, in one place:
# test/tools/camel_arg.py. `ARGV` is this process's arguments without the `--camel` pair.
CAMEL, ARGV = camel_from_argv(REPO_ROOT)


def camel_exe() -> str:
    return CAMEL


def main() -> int:
    env = dict(os.environ)
    env.setdefault("CAMEL_HOME", str(Path(CAMEL).resolve().parents[1]))
    failures = []

    def check(cond: bool, what: str) -> None:
        print(f"{'ok' if cond else 'FAIL'}: {what}")
        if not cond:
            failures.append(what)

    with tempfile.TemporaryDirectory() as t:
        out = Path(t) / "snapshots"
        proc = subprocess.run(
            [camel_exe(), "--dump-after-each-pass", str(out), str(PROGRAM),
             "std::macro", "std::opt::inline", "std::nvm"],
            env=env, capture_output=True, text=True, cwd=REPO_ROOT)
        check(proc.returncode == 0, f"camel exit code 0 (got {proc.returncode})")
        check(proc.stdout.strip() == "42", "program output unchanged (42)")

        names = sorted(p.name for p in out.iterdir()) if out.is_dir() else []
        expected = [f"{stem}.{ext}"
                    for stem in ("00_input", "01_std.macro", "02_std.opt.inline")
                    for ext in ("gir.dot", "stats.json")]
        check(names == sorted(expected), f"snapshot files {sorted(expected)} (got {names})")
        if failures:
            return 1

        for stem in ("00_input", "01_std.macro", "02_std.opt.inline"):
            stats = json.loads((out / f"{stem}.stats.json").read_text())
            check(stats.get("nodes", 0) > 0, f"{stem}: stats JSON has a node count")

        diff = subprocess.run(
            [sys.executable, str(GRAPH_DIFF), "--json",
             str(out / "00_input.gir.dot"), str(out / "01_std.macro.gir.dot")],
            capture_output=True, text=True)
        check(diff.returncode == 1, "graph-diff exit code 1 when the graphs differ")
        report = json.loads(diff.stdout)
        removed = [(e["type"], e["label"]) for e in report["removed"]]
        added = [(e["type"], e["label"]) for e in report["added"]]
        check(removed == [("FUNC", "add_one")], f"removed is the macro call (got {removed})")
        check(added == [("DATA", "42")], f"added is the materialized constant (got {added})")
        call = report["removed"][0]
        check(call["srcfile"].replace("\\", "/").endswith("trans/macro/basic.cml")
              and call["span"].startswith("10:"),
              f"the removed call is located at its call site, line 11 (got {call['location']})")

        same = subprocess.run(
            [sys.executable, str(GRAPH_DIFF),
             str(out / "01_std.macro.gir.dot"), str(out / "01_std.macro.gir.dot")],
            capture_output=True, text=True)
        check(same.returncode == 0, "graph-diff exit code 0 on identical snapshots")

    print("pass dump: " + ("PASS" if not failures else "FAIL"))
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
