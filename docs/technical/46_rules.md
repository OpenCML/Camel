# Rules, and the engines that apply them

Step 8 of the graph store (design: [rewrite](../design/rewrite/README.cn.md),
[graph store](../design/graph-store/README.cn.md) 5.2). This note records what the code does;
[44](44_values_with_several_writings.md) describes the engines underneath.

## 1. A rule is a function

```camel
export rule mul_one(x: int) { x * 1 => x }
rule combine(x: int, const c1: int, const c2: int) { x * c1 + x * c2 => x * (c1 + c2) }
rule swap(a: int, b: int) { a + b <=> b + a }
```

`rule` (and `when`, after the closing brace) are names that mean something only where a rule
declaration starts, so a program may still have a variable called `rule`. `<=>` is a token of its
own. A parameter marked `const` only matches a constant.

The AST builder (`Builder::visitRuleDecl`) turns a declaration into three functions, which is what
the design calls the shorthand of a pair of functions with the same parameters:

```camel
func mul_one__lhs(x: int) => x * 1          // the pattern
func mul_one__rhs(x: int) => x              // what it becomes
func mul_one() => __rule('mul_one', false, 0, 0, mul_one__lhs, mul_one__rhs)
```

A condition adds a fourth function and a different maker:

```camel
rule add_zero(x: int, y: int) { x + y => x } when isZero(y)

func add_zero__lhs(x: int, y: int) => x + y
func add_zero__rhs(x: int, y: int) => x
func add_zero__when(x: Node, y: Node) => isZero(y)   // the parameters as nodes of the draft
func add_zero() => __rule_when('add_zero', false, 0, 0, add_zero__lhs, add_zero__rhs, add_zero__when)
```

`__rule` and `__rule_when` are operators of the module `rewrite` (`modules/rewrite`), which the
declaration imports itself (with `graph.Node` for the condition). They make a `RuleObject` (type
`Rule`): the name, the direction, the permissions the rule needs, a bit per `const` parameter and
the function values. A rule is the function that makes it, not a `let`: a library function (what a
Camel pass is) has no module-level values, and a rule function is exported, imported and listed
like any function. A list of rules is an array of such functions (`[mul_one, combine]`, type
`(() => Rule)[]`).

The context of a rule (`rule fuse<f, g>(...)`) is read by the grammar and refused by the compiler
(`ConstructNotSupported`) until generics land (H38 item 5).

A rule may state the numeric permissions it needs with `#requires<...>` before the declaration
(design principles 7.2): a rule that rewrites `x * c1 + x * c2` into `x * (c1 + c2)` is only
correct where `reassociate` is allowed. `#requires<reassociate, contract>` names permissions; the
core group `finite_math` names the four assumptions it groups, so `#requires<finite_math>` needs
all four, and a module may declare its own group of permissions (`tag fast = [reassociate,
contract]`) and require `#requires<fast>`: the AST pre-pass expands same-module groups
recursively into a `sem::PermissionMask`. A program may not declare a tag with the name of a
core permission (`SemanticDiag::TagShadowsPermission`), because that name always means the
core's permission and the declaration would never be read. The mask is the third argument of
`__rule` (`__rule('name', twoWay, needs, consts, lhs, rhs)`); the fourth is the mask of `const`
parameters, as before. The mask is 16 bits (`PermissionMask`): the nine permissions of 7.2,
`nondeterministic` included, and room for more without a second column.

The check and the engines ask the numeric policy of the nodes for the mask: a rule whose `needs`
the policy does not allow is skipped at that function (and a rule that needs nothing always
applies). With the defaults of 7.3, a rule that needs `reassociate` or `contract` applies without
any tag; one that needs `approx_math`, `change_precision`, `nondeterministic` or an
assumption of `finite_math` applies only where the program grants it, written as the tag
`#approx_math` (or `#[finite_math]`). The engines read the permissions of a node through
`tag::effectivePermissions`, which returns the empty set for `#strict` (7.4): no rule that needs a
permission applies under it, and a rule that needs nothing still does. A requirement that names no
permission is a located error
(`SemanticDiag::TagNotAPermission`), and so is a tag other than `#requires` before a rule
(`SemanticDiag::RuleTagNotRequires`).

## 2. From a rule to the engine

`compileRule(RuleSource)` (`runtime/rules.h`) reads the two functions: the left one's result is the
pattern and its parameters are the variables; the nodes of a pattern are operators, constants and
parameters, anything else is refused. On the right, a term made only of constants and `const`
parameters is computed when the rule is applied, by the operator itself; a constant that faults
makes the rule not apply at that match. Refused: a right side that uses a parameter the left side
does not, a left side that is a single parameter (it would match every value), a left side that
calls a user function. A `<=>` rule gives two `Rule`s.

## 3. The engines (module `rewrite`)

| operator | |
|---|---|
| `rewrite(p, rules)` | the rules one way, in place, to a fixpoint (`DraftWritings::rewrite`); a `<=>` rule is the error value `RuleTwoWay` |
| `saturate(p, rules, rounds)` | both ways on the writings of every value; the result has values with several writings |
| `extract(p, cost)` | one writing per value by the named cost model |
| `check(rules, samples, seed)` | the check of section 4 |

`rewrite` and `saturate` leave the functions that are the sides of rules (`__lhs`, `__rhs`, and
`__when`) alone. The result is `Program!`; the errors are `RuleInvalid`, `RuleTwoWay`,
`NoCostModel`, `RewriteInvalid`. A pass is
a function `(p: Program) => Program!` (`camel prog.cml mod::pass std::nvm`):

```camel
export func apply(p: Program): Program! { return rewrite(p, [add_zero, mul_one, combine]) }
```

The engines call the rule functions through `NodeVMSchedPass::callGraph`. A call can move the
objects, so a kernel reads the array of rules from its argument each time and copies what it needs
from a rule object at once (the poison sweep finds a raw pointer kept across a call).

## 4. The check

`check(rules, samples, seed)` is the check every rule passes before it enters a library (graph
store decision 5). For each rule: `samples` random instances of the two sides (random constants
for the `const` parameters, random values for the others, with the special values of floats that
the rule's permissions do not rule out: NaN, infinities, signed zero) must agree: bit for bit
when the rule needs no permission, within `64 (steps + 1) eps (|a| + |b| + scale)` when it does
(a bound on the rounding of the steps; it catches a wrong equation, not a subtle rounding). Then
the engine applies the rule to its own left side with random constants (up to eight times): it
must fire, and the rewritten function must agree too. The report has a line per rule: `ok name:
n samples agree, fired k of m`, or `FAIL name: ...` with the arguments where it fails
(`x + 0.0 => x` fails at `-0.0`). A rule with a condition cannot use the sampled values (the tool
cannot ask a condition about them), so its line is `ok name: the condition selects the instances,
fired k of m` and only the engine path runs; a condition that rejects every generated instance
reports `never fired`.

## 5. The standard rules

`stdlib/rules.cml` (`import ... from rules`) holds the laws that hold bit for bit: for the
integers (wrapping arithmetic) the neutral elements, `x * 0`, `x - x`, double negation, folding
of constants, commutation, association and distribution; for the floats the ones rounding does
not touch (`x * 1.0`, `x + -0.0`, `x - 0.0`, `x / 1.0`, double negation, commutation). Two passes:
`rules::simplify` (the rules that make an expression smaller, one way, to a fixpoint) and
`rules::saturate_all` (every rule, both ways where it holds both ways, then the smallest writing
of each value). No rule enters the file before `check` passes it: `check_library.cml` runs the
check over every rule of it (the plan `modules.rules`). The float laws that depend on a
permission (`x*c1 + x*c2`, `(x*c1)*c2`) can now be written with `#requires`; the ones that need a
condition (`x*c + x*c` for a whole `c`) still wait for `when`; `std::opt::algebra` keeps doing
them.

## 6. The rest of the engine (H38)

The rule declaration, the engines and the check are the first step; `when` is implemented (H38
item 1), the rest is planned in the H38 report (`docs/handover/reports/H38.md`, section 1). In
short:

**`when` (implemented).** `rule … { lhs => rhs } when expr` compiles to a fourth function
`name__when(params as Node) => expr`, and to a maker `__rule_when(name, twoWay, needs, consts,
lhs, rhs, when)`; the 6-argument `__rule` stays for rules without a condition. A parameter of the
condition is a `graph::Node` naming the node the engine bound to that parameter in the draft
being rewritten (scheme (a)), so a condition sees the live graph: its operator, type, tags,
inputs, users, constants, anchor flags. The graph module's `Handle` carries a draft, and the node
readers answer from it; `callee` is the one read that still needs a frozen program. The engine
already calls `Rule.when` after a match and before a replacement; it now builds that closure from
the Camel function, roots the rule object and the Node arguments across the call, and registers a
tracer that relocates the draft's static values and the constant slots copied into the compiled
patterns (a condition call is a safepoint). `check` compares the two sides only through the
engine for a conditional rule. Tests: `test/cases/modules/rules/when_rules.cml` with
`when_target.cml` (fact, shape and type conditions, each firing and refusing, all four backends,
saturation) and `check_when.cml`. A rule, its target and the pass that applies it may also live
in one module (`same_module_pass.cml`): the pass module is a program of its own, and the program
it is also part of is built afterwards (H38 report §2.5).

**`analysis`.** `analysis range: Interval { initial => …; join(a,b) => …; widen(a,b) => …; on (x +
y) => range(x) + range(y) }` declares a lattice and its transfer clauses; it compiles to
`range__initial`, `range__join`, `range__widen`, one pattern/clause pair per `on`, and the query
`range(n: Node) => __analysis(…)`. The clause expression sees its pattern variables as `Node`s
(like `when`); the fact type is restricted to trivial data, and the analysis becomes one
`user.<module>.<analysis>` fact kind (Derived, scope Node, Anchors dependency). The facts are
propagated in post-order, joined at JOIN nodes and across the writings of a value, cached per
graph revision and per draft revision. Which of a clause's variables are values and which are
facts was the open question the design's example left ambiguous; they are `Node`s, as the review
of H38 decided.

**`analysis` with parameters (H61).** `analysis pair(a: Node, b: Node): bool { … }` is the same
declaration with a parameter list: the parameters are pattern variables, so a rule's condition
writes `pair(x, y)` and hands it the parts of the compound it is rewriting. The query compiles to
`func pair(a: Node, b: Node): bool => __analysis('pair', …)` with one node argument per parameter;
the engine answers the fact of the value those parts belong to — the value a user of the first
part reads with exactly these parts as its operands and one of the `on` clauses matches, clauses
in written order and values in node order. A declaration without parameters keeps the one-value
form `range(n)`. There is no syntax that names the whole match.

**`#requires` (implemented, H2b).** `#requires<reassociate, finite_math>` before a rule resolves the
names to `sem::PermissionMask` -- `finite_math` expands to its four bits -- and fills the `needs`
slot `Rule` already carries, so the engine and the check gate on it with no engine change. A name
that is not a permission is a located `TagNotAPermission`, another tag before a rule is
`RuleTagNotRequires`, an empty list a `TagArgumentCount`, and `#requires` anywhere but before a rule
an undeclared tag. Program tags are declared after the AST is built, so only core permission names
resolve here.

**Generic rules.** `rule comm<T>(…) where T: numeric` is one rule per instantiation, with the
operator family selected by the instantiated type (the table `AlgebraPass::Families` reads in the
native pass); it waits for `where` and the first stage of generics.

**Deleting `algebra.cpp`.** After `when` and `#requires`, the rule library expresses the laws the
native walk applies today (float coefficient folding needs the permissions). The bar: identical
output and `std::jgir` structure on the algebra corpus and on generated 10k/40k/80k programs, on
all four backends, and every test of `opt.algebra` passing with the rules pass substituted; then
the pass is deleted and its name becomes the Camel pass. The order the author set (H61): first
the laws that differ by element type (int32/float32 overflow and rounding), then the tensor laws;
`safe_fp` is **not** a separate mode to reproduce — it is the strict numeric policy, which holds
no permission at all (`#strict`, design principles 7.4), so the laws that require a permission
are exactly the ones that do not apply under it, and no separate semantics is added for it.

**The export list.** It is generated from the modules' undefined symbols (H36/H40), so a new
libcamel function the module calls is exported without a hand edit; `rewrite` links the sibling
`graph.cmo` for the handle constructors. A link failure is a defect of the generator.

## 7. Open

- The condition's side effects are not enforced statically (there is no purity annotation for
  arbitrary functions); `when` must be pure by contract, and `check` is the guard.
- `const` parameters match a *constant writing*, while `graph::is_constant` asks whether the
  matched *node* is a constant; a condition that needs the value of a `const` parameter should use
  the literal or `graph::constant` on the node that is the literal.
- `check` cannot exercise a condition that depends on a non-constant parameter's value (it
  materialises only the `const` ones); such a rule reports `never fired`.
- The tolerance of the check for rules that need a permission is a bound on the rounding, not the
  declared numeric contract of each operator.
- The condition is a function of the rule's parameters, not of the whole matched pattern: in
  `x + (a * b)` the parameter `a` is the multiplication's operand, not the multiplication node.
  A fact about a compound is read by declaring an analysis over its parts (H61): `pair(x, y)`
  answers the fact of the value those parts belong to.
- `analysis`: are a clause's variables values or nodes? They are `Node`s (the review of H38;
  implemented in H38b).
