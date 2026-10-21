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

`__rule` is an operator of the module `rewrite` (`modules/rewrite`), which the declaration imports
itself. It makes a `RuleObject` (type `Rule`): the name, the direction, the permissions the rule
needs, a bit per `const` parameter and the two function values. A rule is the function that makes
it, not a `let`: a library function (what a Camel pass is) has no module-level values, and a rule
function is exported, imported and listed like any function. A list of rules is an array of such
functions (`[mul_one, combine]`, type `(() => Rule)[]`).

The condition (`when`) and the context of a rule (`rule fuse<f, g>(...)`) are read by the grammar
and refused by the compiler (`ConstructNotSupported`) until they are implemented.

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

`rewrite` and `saturate` leave the functions that are the sides of rules alone. The result is
`Program!`; the errors are `RuleInvalid`, `RuleTwoWay`, `NoCostModel`, `RewriteInvalid`. A pass is
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
(`x + 0.0 => x` fails at `-0.0`).

## 5. The standard rules

`stdlib/rules.cml` (`import ... from rules`) holds the laws that hold bit for bit: for the
integers (wrapping arithmetic) the neutral elements, `x * 0`, `x - x`, double negation, folding
of constants, commutation, association and distribution; for the floats the ones rounding does
not touch (`x * 1.0`, `x + -0.0`, `x - 0.0`, `x / 1.0`, double negation, commutation). Two passes:
`rules::simplify` (the rules that make an expression smaller, one way, to a fixpoint) and
`rules::saturate_all` (every rule, both ways where it holds both ways, then the smallest writing
of each value). No rule enters the file before `check` passes it: `check_library.cml` runs the
check over every rule of it (the plan `modules.rules`). The float laws that depend on a
permission (`x*c1 + x*c2`, `(x*c1)*c2`) and the ones that need a condition (`x*c + x*c` for a
whole `c`) wait for `#requires` and `when`; `std::opt::algebra` keeps doing them.

## 6. Open

- `when` and the context of a rule; the permissions of a rule are not written yet (`#requires`
  needs the tag mechanism); analyses.
- The tolerance of the check for rules that need a permission is a bound on the rounding, not the
  declared numeric contract of each operator.
- The module `rewrite` uses libcamel functions (`NodeVMSchedPass::callGraph`, the rule compiler,
  `DraftWritings`, the cost models); the Windows export list has to get them.
