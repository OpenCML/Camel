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

The transpose family is asked the same way, because it is the one family whose rules have no
consumer yet: the index files its five rules under the operator of their left side, and nothing
binds an operator to it. So the same program answers every transpose question the forward family
answers -- and with "forward=true" the bindings that would make a transform use them are zero.
That gap is the reading a transpose operator has to close, so it is pinned here rather than
remembered.

The third family is `rulesets.vmap`, and the program imports it for one reason: the batching table
(`bindVmapRule`) names it as the family of the array and tensor primitives' batching rules
(`tensor:multiply`, `:op/idx_arr`, `matmul`, the four reductions along an axis). Each table is read as
a whole, so a binding whose family is not in the program prints `-> no rule`, and that line is the
thing a table must never show for a binding that has a rule. The family is therefore part of the
fixture, and the counts below are what it adds: twelve rules (the five elementwise keys, `mul_tensor`,
`index_into`, `matmul` and the four axis reductions) and twelve entries in the batching table -- the
ten scalar operators plus the two array ones -- which is why that table is printed as a listing of its
own (`VMAP`) instead of being folded into the forward one. The forward table is the ten scalar
operators: a batching rule in the forward table would be a rule a differentiating transform reads as
if it were a tangent rule, and the five of them are filed in their own family.

Query the array primitives' keys like any other: `rulesets.vmap` at `mul_tensor` with two operands is
`vmap_rule_mul_tensor`, and at `index_into` with two is `vmap_rule_idx`. A call is a key (`f(a, b)` is
`f` at two operands), which is how an operator with no token of its own -- the product of two arrays,
the element of a row -- is written down.

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
    # The transposes of the same five primitives, filed under the same five keys.
    ("rulesets.transpose|-|1", "LOOKUP rulesets.transpose -/1 -> neg_t"),
    ("rulesets.transpose|-|2", "LOOKUP rulesets.transpose -/2 -> sub_t"),
    ("rulesets.transpose|+|2", "LOOKUP rulesets.transpose +/2 -> add_t"),
    ("rulesets.transpose|*|2", "LOOKUP rulesets.transpose */2 -> mul_t"),
    ("rulesets.transpose|/|2", "LOOKUP rulesets.transpose //2 -> div_t"),
    # The array product, filed under a call because the operator has no token of its own.
    ("rulesets.vmap|mul_tensor|2", "LOOKUP rulesets.vmap mul_tensor/2 -> vmap_rule_mul_tensor"),
    ("rulesets.vmap|mul_tensor|3", "LOOKUP rulesets.vmap mul_tensor/3 -> no rule"),
    # And the array index, likewise: `xs[i]` is a bracket form, which is none of the three shapes.
    ("rulesets.vmap|index_into|2", "LOOKUP rulesets.vmap index_into/2 -> vmap_rule_idx"),
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

# The index itself is listed by the same pass, one line per rule, and the program still runs. Three
# families are in the program: the two scalar ones (five keys each) and the batching family, which now
# holds the most of the three -- five elementwise keys, `mul_tensor`, `index_into`, `matmul` and the
# four reductions along an axis. The count is the real one, and every rule of the batching family is
# also pinned by name, because a count says the index grew and a name says which rule grew it.
listed = [line for line in asked.splitlines() if line.startswith("RULESET ")]
check(len(listed) == 22, f"the index lists twenty-two rules, got {len(listed)}: {listed}")
check(
    [line for line in listed if line.startswith("RULESET rulesets.vmap ")]
    == [
        "RULESET rulesets.vmap */2 -> vmap_rule_mul (rhs vmap_mul)",
        "RULESET rulesets.vmap +/2 -> vmap_rule_add (rhs vmap_add)",
        "RULESET rulesets.vmap -/1 -> vmap_rule_neg (rhs vmap_neg)",
        "RULESET rulesets.vmap -/2 -> vmap_rule_sub (rhs vmap_sub)",
        "RULESET rulesets.vmap //2 -> vmap_rule_div (rhs vmap_div)",
        "RULESET rulesets.vmap index_into/2 -> vmap_rule_idx (rhs idx_batch)",
        "RULESET rulesets.vmap matmul/2 -> vmap_rule_matmul (rhs vmap_matmul)",
        "RULESET rulesets.vmap max_axis/3 -> vmap_rule_max_axis (rhs vmap_max_axis)",
        "RULESET rulesets.vmap mean_axis/3 -> vmap_rule_mean_axis (rhs vmap_mean_axis)",
        "RULESET rulesets.vmap min_axis/3 -> vmap_rule_min_axis (rhs vmap_min_axis)",
        "RULESET rulesets.vmap mul_tensor/2 -> vmap_rule_mul_tensor (rhs vmap_mul_tensor)",
        "RULESET rulesets.vmap sum_axis/3 -> vmap_rule_sum_axis (rhs vmap_sum_axis)",
    ],
    f"the batching family files these twelve rules: {listed}",
)
check(
    "RULESET rulesets.jvp -/1 -> jvp_neg (rhs neg_tangent)" in listed
    and "RULESET rulesets.transpose -/2 -> sub_t (rhs sub_cotangent)" in listed,
    f"the listing names the rule each key is filed under, and the function it calls: {listed}",
)
check("1.0" in asked, f"the pass hands the program on unchanged:\n{asked}")

# The reason is part of the answer: "no rule" and "no such family" are different repairs, so they
# are never collapsed into one "none". Two answers say the family has no such rule, and each is asked
# on purpose: `-` at three operands (a count nothing is filed under) and `mul_tensor` at three (a
# head that is filed, at another count).
no_rule = [line for line in lines if line.endswith("-> no rule")]
check(
    no_rule == ["LOOKUP rulesets.jvp -/3 -> no rule", "LOOKUP rulesets.vmap mul_tensor/3 -> no rule"],
    f"two answers say the family has no such rule: {lines}",
)
check(
    sum(1 for line in lines if line.endswith("-> no such family")) == 1,
    f"exactly one answer says the family is unknown: {lines}",
)

# The bindings are the other half: a rule in the index is used by a transform only when an operator
# is bound to that family's key. There are three tables now, and they are printed apart because they
# are asked apart: a differentiating transform reads the forward one, a pullback reads the transpose
# one, and `reinterp<vmap>` reads the batching one. The forward table holds the ten scalar operators,
# whose five keys its family holds; the array and tensor primitives are in the batching table, because
# a batching rule is not a tangent rule and an operator may have one of each.
code, bound = run(["std::rulesets", "forward=true"])
check(code == 0, f"the program exits 0 asking for the bindings (got {code}):\n{bound}")
forwards = [line for line in bound.splitlines() if line.startswith("FORWARD ")]
check(len(forwards) == 10, f"ten operators have a forward rule, got {len(forwards)}: {forwards}")
check(
    all(" rulesets.jvp " in line for line in forwards),
    f"the ten scalar operators are bound to the forward family: {forwards}",
)
check(
    not any(" rulesets.transpose " in line or " rulesets.vmap " in line for line in forwards),
    f"the forward table names neither of the other two families: {forwards}",
)

vmaps = [line for line in bound.splitlines() if line.startswith("VMAP ")]
check(len(vmaps) == 17, f"seventeen operators have a batching rule, got {len(vmaps)}: {vmaps}")
arrays = [line for line in vmaps if line.startswith("VMAP tensor:") or line.startswith("VMAP :op/idx")]
check(
    arrays
    == [
        "VMAP :op/idx_arr rulesets.vmap index_into/2 -> vmap_rule_idx",
        "VMAP tensor:matmul rulesets.vmap matmul/2 -> vmap_rule_matmul",
        "VMAP tensor:max_axis rulesets.vmap max_axis/3 -> vmap_rule_max_axis",
        "VMAP tensor:mean_axis rulesets.vmap mean_axis/3 -> vmap_rule_mean_axis",
        "VMAP tensor:min_axis rulesets.vmap min_axis/3 -> vmap_rule_min_axis",
        "VMAP tensor:multiply rulesets.vmap mul_tensor/2 -> vmap_rule_mul_tensor",
        "VMAP tensor:sum_axis rulesets.vmap sum_axis/3 -> vmap_rule_sum_axis",
    ],
    f"the array and tensor primitives are bound to the family that holds their batching rules: {vmaps}",
)
check(
    all(" rulesets.vmap " in line and not line.endswith("-> no rule") for line in vmaps),
    f"the index answers every batching binding with a rule the family holds: {vmaps}",
)

# The transpose table is the second slot: the same five keys, the family a linear function's
# pullback is written in, and the rules the index already holds for all five.
transposes = [line for line in bound.splitlines() if line.startswith("TRANSPOSE ")]
check(
    len(transposes) == 10,
    f"ten operators have a transpose rule, got {len(transposes)}: {transposes}",
)
check(
    all(" rulesets.transpose " in line for line in transposes),
    f"every one of them is bound to the transpose family: {transposes}",
)
check(
    all(not line.endswith("-> no rule") and not line.endswith("-> no such family") for line in transposes),
    f"and the index answers every binding with a rule the family holds: {transposes}",
)

if failures:
    for failure in failures:
        print(f"FAIL: {failure}")
    raise SystemExit(1)
print("ruleset lookup: PASS")