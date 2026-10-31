"""Asks a ruleset's index a question, and checks the answer and the reason when there is none.

The index is built while a ruleset module is read; built is not the same as askable, and a table
that can only be printed answers nothing while looking as if it did. The `std::rulesets` pass is the
reading: it prints the index, and with `query=` it asks `lookupRule(family, head, arity)` -- the
function a transform asks -- and prints the answer. The case pins all three answers:

  found          `rulesets.jvp` has a rule for `-` with one operand: `jvp_neg`, and for `-` with two
                 operands: `jvp_sub`. The two are different primitives (`__neg__` and `__sub__`),
                 which is why the operand count is part of the key.
  no rule        `-` with three operands: the family is there, nothing there takes three.
  no family      a name no ruleset module has.

The two ways of not finding a rule are named apart because the repairs differ (write the rule, or
import the ruleset). The case also checks that a program without the pass prints nothing about
rules, and that the pass hands the program on unchanged.

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


def run(extra: list[str]) -> tuple[int, str]:
    env = dict(os.environ)
    env.setdefault("CAMEL_HOME", str(Path(CAMEL).resolve().parents[1]))
    # No environment switch does this any more: what a run prints about rules is what its passes ask
    # for (design principles: a pass is the interface, not an environment variable).
    env.pop("CAMEL_RULESET_LOOKUP", None)
    env.pop("CAMEL_RULESET_INDEX", None)
    proc = subprocess.run(
        [CAMEL, str(PROGRAM), *extra, *ARGV],
        env=env,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    return proc.returncode, proc.stdout


code, quiet = run([])
check(code == 0, f"the program exits 0 without the pass (got {code}):\n{quiet}")
check("LOOKUP" not in quiet, f"no pass asks, so nothing is answered:\n{quiet}")
check("RULESET " not in quiet, f"and nothing is listed either:\n{quiet}")
check("1.0" in quiet, f"the program still runs and prints its value:\n{quiet}")

query = ";".join(question for question, _ in QUERIES)
code, asked = run(["std::rulesets", f"query={query}"])
check(code == 0, f"the program exits 0 with the pass (got {code}):\n{asked}")

lines = [line for line in asked.splitlines() if line.startswith("LOOKUP ")]
expected = [want for _, want in QUERIES]
check(lines == expected, f"the answers are {expected}, got {lines}")

# The index itself is listed by the same pass, one line per rule, and the program still runs.
listed = [line for line in asked.splitlines() if line.startswith("RULESET ")]
check(len(listed) == 10, f"the index lists ten rules, got {len(listed)}: {listed}")
check(
    "RULESET rulesets.jvp -/1 -> jvp_neg (rhs neg_tangent)" in listed
    and "RULESET rulesets.transpose -/2 -> sub_t (rhs sub_cotangent)" in listed,
    f"the listing names the rule each key is filed under, and the function it calls: {listed}",
)
check("1.0" in asked, f"the pass hands the program on unchanged:\n{asked}")

# The reason is part of the answer: "no rule" and "no such family" are different repairs, so they
# are never collapsed into one "none".
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