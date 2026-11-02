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

The transpose and batching families are asked the same way, because they are asked apart: a
differentiating transform reads `rulesets.jvp`, a pullback `rulesets.transpose`, `reinterp<vmap>`
`rulesets.vmap`. Each is asked by its own keys, and the program imports all three.

**No operator is bound to a key by hand any more.** The key a rule is filed under is read off the
operator itself (`camel/core/forward.h#ruleKeyOf`): `:op/add_d` is written `+` with two operands
because that is what `add` means, and `tensor:sum_axis` is written `sum_axis(...)` because that is
the operator's own name, at whatever count the family filed it under. Two operators cannot be written
that way, and those two are the whole of `ruleKeyExceptions()`: `:op/idx_arr` (`xs[i]` is a bracket
form with no token) and `tensor:multiply` (`*/2` is already `:op/mul_d`'s key). With
`forward=true` the pass prints those two lines and the sentence saying how the rest is derived, so an
exception is never silent -- and this case pins both the lines and the rules their keys resolve to.

Nothing in this file counts rules. The two halves of the same fact are checked against each other: the
index a module builds, and the keys the operators derive.

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
    ("rulesets.jvp|+|2", "LOOKUP rulesets.jvp +/2 -> jvp_add"),
    ("rulesets.jvp|*|2", "LOOKUP rulesets.jvp */2 -> jvp_mul"),
    ("rulesets.jvp|/|2", "LOOKUP rulesets.jvp //2 -> jvp_div"),
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
# families are in the program: the two scalar ones and the batching family, which is now the largest
# of the three. What the listing is checked against is the *other* half of the same fact -- the three
# binding tables -- and not a number written here: a rule added to a family, or an operator bound to a
# new key, changes both halves, and a count in this file would go red for the one thing that cannot be
# wrong. So every count below is computed from the tables, and every binding is looked up by name.
listed = [line for line in asked.splitlines() if line.startswith("RULESET ")]
check(listed, f"the pass lists the index it built: {asked}")
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

# No per-operator table is left, so what the pass prints is the other half of the same fact: the keys
# that cannot be derived, with the reason each cannot. They are printed on every run of the pass,
# whatever `forward` says, because an exception only visible on request is an exception nobody reads;
# `forward=true` adds the sentence saying how every other key is derived.
code, bound = run(["std::rulesets", "forward=true"])
check(code == 0, f"the program exits 0 asking for the keys (got {code}):\n{bound}")

# The count moved from two to three on 2026-11-03: `:op/ltod` (the conversion `float(x: int)`) is
# bound through the exception table to `float_of_int/1`, because `float` at one operand is a spelling
# every conversion to a double shares (`float(x: i32)`, `float(x: float)`, `float(x: string)` ...), so
# one key could not name it. The assertion did not get looser -- it still counts and still names every
# entry -- and the third entry is a fact about the spelling, not a concession to a test.
exceptions = [line for line in bound.splitlines() if line.startswith("RULEKEY-EXCEPTION ")]
check(len(exceptions) == 3, f"exactly three operators have a key that cannot be derived: {exceptions}")
keys = [(line.split()[1], line.split()[3]) for line in exceptions]
check(
    keys
    == [
        (":op/idx_arr", "index_into/2"),
        (":op/ltod", "float_of_int/1"),
        ("tensor:multiply", "mul_tensor/2"),
    ],
    f"the three are the array index, the array product and the conversion from an integer, at the keys their rules are filed under: {keys}",
)
check(
    all(" because " in line and len(line.split(" because ", 1)[1]) > 20 for line in exceptions),
    f"each line says why in words, not only what: {exceptions}",
)
check(
    any("bracket form" in line and "no token" in line for line in exceptions),
    f"the index's reason is that `xs[i]` is a bracket form with no token: {exceptions}",
)
check(
    any(":op/mul_d" in line and "already" in line for line in exceptions),
    f"the product's reason is that `*/2` is already the scalar product's key: {exceptions}",
)
check(
    any(line.startswith("RULEKEY derivable:") for line in bound.splitlines()),
    f"with \"forward=true\" the pass says how every other key is derived: {bound}",
)

# Each exception's key resolves in the family it names, and asking the index for it by name answers the
# same rule: the exception is written down in C++ and the rule is written down in the ruleset, so the
# two halves are checked against each other here rather than one of them being trusted.
questions = [f"rulesets.vmap|{head}|{arity}" for _, key in keys for head, _, arity in [key.rpartition("/")]]
code, answered = run(["std::rulesets", f"query={';'.join(questions)}"])
check(code == 0, f"the program exits 0 asking the exceptions' keys (got {code}):\n{answered}")
looked = {
    line.split(" -> ")[0]: line.partition(" -> ")[2]
    for line in answered.splitlines()
    if line.startswith("LOOKUP ")
}
for want, rule in (
    ("LOOKUP rulesets.vmap index_into/2", "vmap_rule_idx"),
    ("LOOKUP rulesets.vmap mul_tensor/2", "vmap_rule_mul_tensor"),
):
    check(
        looked.get(want) == rule,
        f"`{want}` resolves to {rule}, and the index answers {looked.get(want)!r}",
    )

if failures:
    for failure in failures:
        print(f"FAIL: {failure}")
    raise SystemExit(1)
print("ruleset lookup: PASS")