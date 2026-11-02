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
(`tensor:multiply`, `:op/idx_arr`, `matmul`, the reductions). Each table is read as a whole, so a
binding whose family is not in the program prints `-> no rule`, and that line is the thing a table
must never show for a binding that has a rule. The family is therefore part of the fixture, and the
three tables are printed apart (`FORWARD`, `TRANSPOSE`, `VMAP`) because they are asked apart: a
differentiating transform reads the first, a pullback the second, `reinterp<vmap>` the third.

Nothing in this file counts rules. The two halves of the same fact -- the index a module builds and
the tables the operators bind into it -- are checked against each other: every binding resolves to a
rule, asking the index for that key answers the same rule the binding printed, and each family holds
exactly the keys its table binds. A rule added to a family, or an operator bound to a new key, moves
both halves, so this test grows with them instead of going red at the next addition.

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

# The bindings are the other half: a rule in the index is used by a transform only when an operator
# is bound to that family's key. There are three tables and they are printed apart because they are
# asked apart: a differentiating transform reads the forward one, a pullback reads the transpose one,
# and `reinterp<vmap>` reads the batching one. Each table is checked against the index the same way,
# so nothing here names an operator or counts a rule: a table that grows is checked as it grows.
#
#   * every binding resolves (`-> <rule>`, never `-> no rule` / `-> no such family`);
#   * asking the index for that key directly answers the same rule the binding printed;
#   * the family holds exactly the keys the table binds -- one rule per key, none unreached.
code, bound = run(["std::rulesets", "forward=true"])
check(code == 0, f"the program exits 0 asking for the bindings (got {code}):\n{bound}")


def parseBinding(line: str) -> tuple[str, str, str, int, str]:
    """`uri family head/arity -> rule`, the four parts this file reads."""
    fields = line.split()
    head, _, arity = fields[2].rpartition("/")
    return fields[0], fields[1], head, int(arity), line.partition(" -> ")[2]


tables: dict[str, list[tuple[str, str, str, int, str]]] = {}
for entry in bound.splitlines():
    prefix, _, rest = entry.partition(" ")
    if prefix in ("FORWARD", "TRANSPOSE", "VMAP") and rest:
        tables.setdefault(prefix, []).append(parseBinding(rest))
check(
    sorted(tables) == ["FORWARD", "TRANSPOSE", "VMAP"],
    f"the pass prints all three tables: {sorted(tables)}",
)

familyOf = {"FORWARD": "rulesets.jvp", "TRANSPOSE": "rulesets.transpose", "VMAP": "rulesets.vmap"}
listedKeys = {family: set() for family in familyOf.values()}
for line in listed:
    _, family, key, _ = line.split(maxsplit=3)
    head, _, arity = key.rpartition("/")
    listedKeys.setdefault(family, set()).add((head, int(arity)))

for prefix, rows in tables.items():
    check(rows, f"the {prefix} table is empty: {bound}")
    unresolved = [row for row in rows if row[4].startswith("no ")]
    check(not unresolved, f"every {prefix} binding answers with a rule the family holds: {unresolved}")
    foreign = [row for row in rows if row[1] != familyOf[prefix]]
    check(not foreign, f"every {prefix} binding names its own family: {foreign}")
    keys = {(row[2], row[3]) for row in rows}
    check(
        keys == listedKeys[familyOf[prefix]],
        f"the {familyOf[prefix]} index and the {prefix} table hold the same keys: "
        f"{sorted(keys)} vs {sorted(listedKeys[familyOf[prefix]])}",
    )

# Every binding, asked for by name: the answer is the rule the table printed, which is the same fact
# read twice. This is the check a written count cannot make -- it says the two halves agree about
# *which* rule, key by key.
questions = sorted({f"{row[1]}|{row[2]}|{row[3]}" for rows in tables.values() for row in rows})
code, answered = run(["std::rulesets", f"query={';'.join(questions)}"])
check(code == 0, f"the program exits 0 asking every binding (got {code}):\n{answered}")
looked = {
    line.split(" -> ")[0]: line.partition(" -> ")[2]
    for line in answered.splitlines()
    if line.startswith("LOOKUP ")
}
for rows in tables.values():
    for uri, family, head, arity, rule in rows:
        want = f"LOOKUP {family} {head}/{arity}"
        check(
            looked.get(want) == rule,
            f"`{uri}` is bound to {family} {head}/{arity} -> {rule}, and the index answers {looked.get(want)!r}",
        )

# The two operators this fixture exists for are in the batching table, by name: the array product and
# the element of a row are what showed that an operator without a token of its own needs a key, and
# they are the two whose bindings moved tables once already.
arrays = [line for line in bound.splitlines() if line.startswith("VMAP :op/idx") or line.startswith("VMAP tensor:multiply")]
check(
    [line.split(" -> ")[0] for line in arrays]
    == [
        "VMAP :op/idx_arr rulesets.vmap index_into/2",
        "VMAP tensor:multiply rulesets.vmap mul_tensor/2",
    ],
    f"the array operators are batching rules, filed under the batching family: {arrays}",
)

if failures:
    for failure in failures:
        print(f"FAIL: {failure}")
    raise SystemExit(1)
print("ruleset lookup: PASS")