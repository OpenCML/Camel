"""Asks a ruleset's index a question, and checks the answer and the reason when there is none.

The index is built while a ruleset module is read (`src/compile/gir/program.cpp`), but built is not
the same as askable: a table that can only be printed answers nothing, and reads as if it did. So
the case asks `lookupRule(family, head, arity)` -- the same function a transform asks -- through the
compiler's probe, and pins all three answers the function can give:

  found          `rulesets.jvp` has a rule for `-` with one operand: `jvp_neg`, and for `-` with two
                 operands: `jvp_sub`. The two are different primitives (`__neg__` and `__sub__`),
                 which is why the operand count is part of the key.
  no rule        `-` with three operands: the family is there, nothing there takes three.
  no family      a name no ruleset module has.

It also checks that the probe is the only thing that prints: a program that imports a ruleset and
asks nothing prints nothing about rules.

Usage: python rulesets_lookup.py [--camel PATH]
"""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[3]

sys.path.insert(0, str(REPO_ROOT / "test" / "tools"))
from camel_arg import camel_from_argv  # noqa: E402

PROGRAM = HERE / "rulesets_lookup.cml"
CAMEL, ARGV = camel_from_argv(REPO_ROOT)

QUERIES = [
    ("rulesets.jvp|-|1", "LOOKUP rulesets.jvp -/1 -> jvp_neg"),
    ("rulesets.jvp|-|2", "LOOKUP rulesets.jvp -/2 -> jvp_sub"),
    ("rulesets.jvp|-|3", "LOOKUP rulesets.jvp -/3 -> no rule"),
    ("rulesets.nope|-|1", "LOOKUP rulesets.nope -/1 -> no such family"),
]

failures: list[str] = []


def check(ok: bool, what: str) -> None:
    if not ok:
        failures.append(what)


def run(env_extra: dict[str, str] | None) -> tuple[int, str]:
    env = dict(os.environ)
    env.setdefault("CAMEL_HOME", str(Path(CAMEL).resolve().parents[1]))
    env.pop("CAMEL_RULESET_LOOKUP", None)
    env.pop("CAMEL_RULESET_INDEX", None)
    if env_extra:
        env.update(env_extra)
    proc = subprocess.run(
        [CAMEL, str(PROGRAM), *ARGV],
        env=env,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    return proc.returncode, proc.stdout


code, quiet = run(None)
check(code == 0, f"the program exits 0 without the probe (got {code}):\n{quiet}")
check("LOOKUP" not in quiet, f"nothing is asked, so nothing is answered:\n{quiet}")
check("RULESET " not in quiet, f"and nothing is listed either:\n{quiet}")

code, asked = run({"CAMEL_RULESET_LOOKUP": ";".join(query for query, _ in QUERIES)})
check(code == 0, f"the program exits 0 with the probe (got {code}):\n{asked}")

lines = [line for line in asked.splitlines() if line.startswith("LOOKUP ")]
expected = [want for _, want in QUERIES]
check(lines == expected, f"the answers are {expected}, got {lines}")

# The reason is part of the answer: "no rule" and "no such family" are different repairs (write the
# rule, or import the ruleset), so they are never collapsed into one "none".
check(
    sum(1 for line in lines if line.endswith("-> no rule")) == 1,
    f"exactly one answer says the family has no such rule: {lines}",
)
check(
    sum(1 for line in lines if line.endswith("-> no such family")) == 1,
    f"exactly one answer says the family is unknown: {lines}",
)

if failures:
    for failure in failures:
        print(f"FAIL: {failure}")
    raise SystemExit(1)
print("ruleset lookup: PASS")