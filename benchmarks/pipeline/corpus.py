#!/usr/bin/env python3
"""Builds the corpus of EXISTING programs for the pipeline-invariance experiment (X1a).

Sources (all tagged `dev`: they were written by the developers of the passes under test):
  1. test plans: every distinct `.cml` case referenced by a camel test that is expected to
     succeed (`expect_exit = 0`), excluding plans under parse/, format/ and perf/, tests run
     through another tool (python/node drivers), and `inspect` invocations. The environment
     of the first such test is kept (`env = ["NAME=value", ...]`).
  2. exemplars: `exemplars/*/program.cml`.

Excluded programs are listed with the reason, so the numbers can be audited.

Usage: corpus.py --out corpus.json
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
EXCLUDED_PLAN_DIRS = ("parse", "format", "perf")


def program_id(path: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]", "_", path.removesuffix(".cml"))


_STR = re.compile(r'"((?:[^"\\]|\\.)*)"')


def parse_plan(text: str) -> dict:
    """Reads the fields this script needs from a test plan. The plans are TOML-like but their
    multi-line `verify_script` strings are not valid TOML (the test runner has its own
    reader), so they are read line by line: `key = value` with string, integer and
    single-line string-array values; triple-quoted blocks are skipped."""
    plan: dict = {"tests": []}
    current = plan
    in_block = False
    for line in text.splitlines():
        stripped = line.strip()
        if in_block:
            if '"""' in stripped:
                in_block = False
            continue
        if stripped == "[[tests]]":
            current = {}
            plan["tests"].append(current)
            continue
        m = re.match(r"([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*)$", stripped)
        if not m:
            continue
        key, value = m.group(1), m.group(2)
        if value.startswith('"""'):
            in_block = value.count('"""') < 2
            continue
        if value.startswith("["):
            current[key] = [x.replace('\\"', '"').replace("\\\\", "\\") for x in _STR.findall(value)]
        elif value.startswith('"'):
            found = _STR.match(value)
            current[key] = found.group(1) if found else value
        elif re.fullmatch(r"-?\d+", value):
            current[key] = int(value)
        else:
            current[key] = value
    return plan


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    programs: dict[str, dict] = {}
    excluded: list[dict] = []
    seen_excluded: set[tuple[str, str]] = set()

    def exclude(path: str, reason: str) -> None:
        if (path, reason) not in seen_excluded:
            seen_excluded.add((path, reason))
            excluded.append({"path": path, "reason": reason})

    plans_root = REPO_ROOT / "test" / "plans"
    for plan in sorted(plans_root.rglob("*.plan.toml")):
        rel_plan = plan.relative_to(plans_root).as_posix()
        data = parse_plan(plan.read_text(encoding="utf-8"))
        for test in data.get("tests", []):
            case = test.get("case", "")
            if not case.endswith(".cml"):
                continue
            path = (plan.parent / case).resolve().relative_to(REPO_ROOT).as_posix()
            parts = rel_plan.split("/")
            if any(d in parts for d in EXCLUDED_PLAN_DIRS):
                exclude(path, f"plan directory excluded ({'/'.join(parts[:-1])})")
                continue
            if test.get("tool", "camel") != "camel":
                exclude(path, f"run through tool '{test.get('tool')}'")
                continue
            argv = [str(a) for a in test.get("args", [])]
            if argv and argv[0] in ("inspect", "check") or "${inspect" in " ".join(argv):
                exclude(path, "inspect/check invocation, not an execution")
                continue
            if test.get("expect_exit", 0) != 0:
                exclude(path, f"expected to fail (expect_exit = {test.get('expect_exit')})")
                continue
            env = {}
            for item in test.get("env", []):
                name, _, value = item.partition("=")
                env[name] = value
            if any("${" in v for v in env.values()):
                exclude(path, "environment uses harness variables")
                continue
            entry = programs.setdefault(path, {
                "id": program_id(path), "path": path, "env": env, "corpus": "dev",
                "origin": "test/cases", "plans": [], "plan_passes": []})
            if data.get("id") not in entry["plans"]:
                entry["plans"].append(data.get("id"))
            entry["plan_passes"].append([a for a in argv if "::" in a])

    for prog in sorted((REPO_ROOT / "exemplars").glob("*/program.cml")):
        path = prog.relative_to(REPO_ROOT).as_posix()
        programs.setdefault(path, {"id": program_id(path), "path": path, "env": {}, "corpus": "dev",
                                   "origin": "exemplars", "plans": [], "plan_passes": []})

    # A program that is also listed as excluded for one of its tests stays in (it has at least
    # one succeeding camel execution); the exclusion list then only documents the other test.
    kept = sorted(programs.values(), key=lambda p: p["path"])
    only_excluded = [e for e in excluded if e["path"] not in programs]
    out = {"programs": kept, "excluded": only_excluded,
           "counts": {"programs": len(kept),
                      "from_tests": sum(1 for p in kept if p["origin"] == "test/cases"),
                      "from_exemplars": sum(1 for p in kept if p["origin"] == "exemplars"),
                      "excluded_programs": len({e["path"] for e in only_excluded})}}
    Path(args.out).write_text(json.dumps(out, indent=1), encoding="utf-8")
    print(json.dumps(out["counts"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
