# 9. Rules and facts

English | [中文](../09_rules_and_facts.md)

The first eight chapters are about how the language computes on its own. This chapter is about the knowledge a reader **teaches the compiler**: a `rule` says "these two writings are one value", and a `fact` says "this value has this property". Both are **values** — a rule is a `Rule`, a fact is a function that can be queried — and neither takes effect by being written down: whoever wants them hands them over ([9.4](#94-a-rule-set-is-handed-over-explicitly)).

---

## 9.1 A rule

```
rule name(parameters) when condition? { left => right }
rule name(parameters) { left <=> right }
```

`left` is a pattern and `right` is a writing it may be replaced by; `<=>` says both directions may be used. There are two kinds of parameter:

| Parameter | What it matches |
|---|---|
| `x: T` | any value in the graph |
| `const c: T` | a constant of type `T`; the condition and the right side get the constant's **value** |

The declared type of a parameter is checked by the matcher, so a rule needs no guard of its own such as `isI32(c)`: `const c: int` matches only `int` constants, `const c: f32` only `f32` constants.

A `rule` declaration declares a `Rule` value (like a constant declared with `let`, not like a function): it can be passed to a function, put in an array, filtered by a condition. **A rule set is an ordinary `Rule[]`**, and scheduling is ordinary Camel code ([rules language v2, section 8.5](../../design/rewrite/v2.cn.md)).

```cml
module law

import { Program } from graph
import { rewrite } from rewrite

// x - x is zero for every int: no condition, so nothing has to hold first.
rule sub_self(x: int) { x - x => 0 }

// c1 + c2 on the right is computed when the rule fires, so the two coefficients become one.
rule fold_scales(x: int, const c1: int, const c2: int) { x * c1 + x * c2 => x * (c1 + c2) }

/// A rule set is an array; a pass is an ordinary function that is handed one.
export func simplify(p: Program): Program! {
    return rewrite(p, [sub_self, fold_scales])
}
```

A rule also carries its name and its requirements: `rule_name`, `rule_two_way`, `rule_needs` and `rule_consts` read a `Rule` value ([standard library reference](../../stdlib-reference.md)).

## 9.2 Conditions and permissions

`when` is **a query about the place that was matched**: the engine has found a match and asks the rule whether it holds there.

- The parameters of the condition are the rule's own: an ordinary parameter is a **node** there, a `const` parameter is a **value** (so a guard is arithmetic on values and need not take the node apart first). To read the node a `const` parameter was bound to, write `node_of(c)`.
- A condition holds only expressions with **no external effect**. A call that does IO, such as `println`, is refused when written in a condition (`SemanticDiag::EffectInRuleCondition`): the engine evaluates the condition with the rewrite session in hand, and a commit or a rewrite there would move the program the engine is reading. An effect belongs on the right side of a rule, or in the program outside the rewrite.
- A condition may call other functions and other `fact`s like any expression; whether the function it calls has an external effect is checked all the same.

Counter-example: the condition calls a function that prints, and the compiler refuses it at the declaration.

<!-- camel-snippet: counter-example: the condition calls a function that prints, which the compiler refuses -->

```cml
func reports(c: float): bool sync {
    println('guard ', c)
    return c > 0.0
}

rule keep_positive(x: float, const c: float) when reports(c) { x * c => x }
```

```text
error[SemanticDiag::EffectInRuleCondition]: The call of 'println' in the function 'reports' has an effect, and it is inside the condition of the rule 'keep_positive'.
```

**Permissions.** A rule may be preceded by `#requires(permission, ...)`: the engine applies the rule only where the numeric policy of the nodes it matched allows those permissions ([7.6.1](07_statements.md#761-numeric-permission-tags)). The arguments are permission names (`reassociate`, `contract`, `approx_math`, `change_precision`, `nondeterministic`, and the four parts of `finite_math`), the core's group `finite_math`, or a group the module declares with `tag name = [permission, ...]`. `reassociate` and `contract` are default permissions, so writing them only spells out what already holds ([design principles 7.3](../../design-principles.en.md)).

```cml
// A sum of two scaled copies of one value is one scaled copy only where regrouping is allowed.
#requires(reassociate)
rule combine_scales(x: float, const c1: float, const c2: float) { x * c1 + x * c2 => x * (c1 + c2) }
```

## 9.3 Type parameters and `with` parameters

A rule may declare **type parameters** after its name, and **`with` parameters** before the declaration:

```
with <name: type = default, ...>
rule name::<T>(parameters...) when condition? { left => right }
```

The kinds of parameter come from different places ([design principles 5.5](../../design-principles.en.md)):

| Parameter | Who decides its value |
|---|---|
| a parameter of the pattern | the value that was matched |
| `const c: T` | the constant that was matched |
| `::<T>` | the type that was matched: a generic rule is instantiated per type |
| `with <tile: int>` | whoever chooses this rule, bound once and then used at that value for the whole rewrite |

A `with` parameter is written like a function's ([7.4.2](07_statements.md#742-with-parameters)): it may have a default, and it may be given by name. **The binding is written where the rule is used**, the same spelling as a partial application of a function:

```cml
module tiled

import { Program } from graph
import { rewrite } from rewrite

with <tile: int>
rule add_zero_tiled(x: int) when tile > 0 { x + 0 => x }

with <scale: int = 1>
rule mul_one_scaled(x: int) when scale > 0 { x * 1 => x }

/// add_zero_tiled<1> binds tile; mul_one_scaled's parameter has a default, so its name alone is a Rule.
export func apply(p: Program): Program! {
    return rewrite(p, [add_zero_tiled<1>, mul_one_scaled])
}
```

- When every parameter has a default, the rule's name alone is a `Rule`; whether a binding is written is visible in the declaration.
- A parameter with no default that is not bound where the rule set is written is a compile error (`SemanticDiag::RuleWithParameterUnbound`): no value in that rule value is there for its condition to read.
- **Type arguments and `with` arguments go through the same instantiation channel, and the key of an instance is that pair**: the same pair hits the same instance, and different bindings are different instances. This is the same thing as the instantiation of a generic function, and with both written the spelling is the function's too: `name::<T><value>`.

**Not implemented.** These two forms are ruled, and do not compile today:

- **The right side of a rule reading a `with` parameter** (`x + 0 => x + shift`): refused today with `SemanticDiag::RuleWithParameterOnASide`. The ruled target is that **both sides** — `when` and the right side — can read it.
- **Type parameters and `with` parameters on one rule** (`with <tile: int> rule tiled::<T>(...)`): refused today with `SemanticDiag::RuleWithParameterGenericHead`.

The acceptance form of this rule is `tile_matmul` in [rules language v2, section 1](../../design/rewrite/v2.cn.md): `with <tile: int> rule tile_matmul::<T>(a: T[m, k], b: T[k, n]) when m % tile == 0 { a @ b => tiled_matmul<tile>(a, b) }`. What can be written today is each half of the two layers — the scheduling parameter as a `with` parameter bound by whoever chooses the rule (`add_zero_tiled<1>` above), and the type as a generic head instantiated by the matched type (`rule add_zero::<T>(x: T) when is_integer::<T>() { x + 0 => x }`) — and both keys on one rule are the ruled target form.

## 9.4 A rule set is handed over explicitly

- **A rewrite pass always receives its rule set explicitly**: importing a module never adds a pass to the pipeline, and rules are never collected on their own. Writing a `rule` in a module does not quietly change what any other pass does ([rules language v2, section 8.5](../../design/rewrite/v2.cn.md); the pass model of [design principles](../../design-principles.en.md), section 9).
- The Camel spelling is the `rewrite` module of the [standard library reference](../../stdlib-reference.md): `rewrite(p, rules)` applies a set in one direction to a fixed point; `saturate(p, rules, rounds)` also uses the rules that reorder; `run<rules>` brings the round budget and the cost out explicitly (`explain` and `why_not` read it). A pass is a function ([4.7](04_execution_model.md#47-passes-and-pipelines)), so composition is function composition and pipes.
- The standard optimization pass `std::opt` simplifies with the standard library's laws (`stdlib/rules.cml`). To hand it your own rules as well: in Camel write `f->std::opt<rules: [negate]>`, on the command line write `std::opt rules=[law::negate]` (a `Rule[]` is a constant expression, read by the pass-parameter rules).

**Not implemented.** `std::opt` has no `rules` parameter today: writing it on the command line gives `SemanticDiag::ParamArgumentUnknown` ("'rules' is not a parameter of 'std::opt'"), and the name `std::opt` does not resolve in source either. To apply your own rules today, write a pass of your own — a pass is a function from a program to a program — and hand it to the command line. The pass takes the module's name (here the file's name), so it is invoked like this:

```cml
// counts.cml
module counts

import { abs } from math
import { Program } from graph
import { rewrite } from rewrite

/// What this program may rely on: its values are counts, so a count is never negative and a
/// difference of two counts may be.
fact nonnegative(v: int): bool match v {
    case const c => c >= 0
    case x - y   => false
    case x + y   => nonnegative(x) && nonnegative(y)
    case _       => true
} join (lhs: bool, rhs: bool) => lhs | rhs

/// A count is its own absolute value.
rule abs_of_count(x: int) when nonnegative(x) { abs(x) => x }

/// The rule set is handed to the pass here, and nowhere else.
export func simplify(p: Program): Program! {
    return rewrite(p, [abs_of_count])
}

func distance(a: int, b: int): int {
    return abs(a + b)
}

func main(): int sync {
    println(distance(3, 4))
    return 0
}
```

```bash
camel ./counts.cml counts::simplify std::nvm
```

```text
7
```

## 9.5 Facts

A `fact` declares one **fact** about a value in the graph: it is computed from the value's **construction**, and a rule's `when` and other facts query it like a function call.

```
fact name(parameters...): fact type match the value or tuple matched {
    case pattern => value
    ...
} join function meet function widen function narrow function
```

- **Head**: `fact name(parameters...)`, with the input written out; the fact type is limited to plain data (a number, a boolean, a struct).
- **Body**: written like an ordinary `match` ([6.8.2](06_expressions.md#682-match)), except that what is matched is the value's **construction in the graph**: `case x + y` takes "the value an addition produced" apart into its two operands, as a variant of an algebraic data type is taken apart. Patterns are written like the left side of a `rule`. With more than one parameter the tuple is matched ([9.6](#96-fact-parameters-and-with-arguments)).
- **`case _` is mandatory**: what the fact is for a value no other case matched has to be said; it is also the value a value that depends on itself settles on. Without it the declaration is refused (`SemanticDiag::FactCaseDefaultMissing`).
- **Trailing clauses**: each takes a binary function — the name of one that exists, or a lambda `(a, b) => ...`; any order, and they may wrap lines. When `meet`, `widen` or `narrow` is left out, that clause uses `join`.

| Clause | Meaning | Required |
|---|---|---|
| `join` | where branches meet: "either of the two", the least upper bound | yes |
| `meet` | between equivalent writings of one value: "both hold", the greatest lower bound; without it, equivalent writings use `join` too (correct but imprecise) | optional |
| `widen` | accelerates convergence while iterating; without it, iteration runs to the limit and falls back to the `case _` fact | optional |
| `narrow` | tightens again after `widen`, recovering the precision it lost | optional |

In theory these are ⊔/⊓/∇/Δ of abstract interpretation.

`join`, `meet`, `widen` and `narrow` take a **function**, so what is written here is a name that exists in a library (such as `max`) or a lambda. **`or`/`and` are not standard library functions**, and no new ones are added: the boolean "or" is `|` (evaluated on both sides, an ordinary overloadable logical operator, [design principles 4.2](../../design-principles.en.md)), so the merge of a boolean fact is written `join (lhs: bool, rhs: bool) => lhs | rhs`. `||` is the reserved operator that turns into a branch; it is not a function value that can be passed, and cannot be written here.

```cml
module bounds

import { max, min } from math

type Interval = { lo: int, hi: int }

/// The wider of two intervals: what `join` and `widen` use here.
func hull(a: Interval, b: Interval): Interval {
    return { lo: min(a.lo, b.lo), hi: max(a.hi, b.hi) }
}

/// The overlap of two intervals: what `meet` and `narrow` use here.
func intersect(a: Interval, b: Interval): Interval {
    return { lo: max(a.lo, b.lo), hi: min(a.hi, b.hi) }
}

/// Where a value can be: a constant is itself, a sum adds the two ends, anything else is unknown.
/// The four clauses are written out so their spelling is visible; this example reuses the two
/// helpers, so `widen` grows and `narrow` narrows.
fact range(v: int): Interval match v {
    case const c => { lo: c, hi: c }
    case x + y   => { lo: range(x).lo + range(y).lo, hi: range(x).hi + range(y).hi }
    case _       => { lo: 0, hi: 0 }
} join hull meet intersect widen hull narrow intersect
```

Counter-example: `case _` is mandatory, and without it nothing says what the fact is for a value no other case matched.

<!-- camel-snippet: counter-example: the fact has no case _, which the compiler requires -->

```cml
fact sign(v: int): int match v {
    case const c => c
} join max
```

```text
error[SemanticDiag::FactCaseDefaultMissing]: The fact 'sign' has no 'case _', so nothing says what it is for a value no other case matched.
```

## 9.6 Fact parameters and `with` arguments

**Parameters.** A fact may have parameters, passed as arguments when it is queried. They are **the parts** of the value (nodes): `fact mul_by_one(a: Node, b: Node): bool match (a, b) { ... }` is queried by a rule's condition as `mul_by_one(x, node_of(c))`. With no parameter, the single-value form is `range(v)`. Type parameters are declared after the name (`fact range::<T>(v: T): Interval`), as they are on a rule — see [9.3](#93-type-parameters-and-with-parameters).

```cml
module parts

import { max } from math
import { Node, Program } from graph
import { rewrite } from rewrite

fact sign(v: int): int match v {
    case const c => c
    case _       => 0
} join max

/// The fact of a product, read from the two parts a rule's condition matched.
fact mul_by_one(a: Node, b: Node): bool match (a, b) {
    case x * y => sign(y) == 1
    case _     => false
} join (lhs: bool, rhs: bool) => lhs | rhs

rule mul_one_by_parts(x: int, const c: int) when mul_by_one(x, node_of(c)) { x * c => x }

export func apply(p: Program): Program! {
    return rewrite(p, [mul_one_by_parts])
}
```

**`with` arguments.** A fact may have `with` parameters: adjustments such as a widening threshold, a precision, a truncation depth or a tolerance. They are declared like a function's or a rule's; a query writes the arguments as a call does, and a default may be left out.

```cml
module capped_fact

import { max } from math
import { Program } from graph
import { rewrite } from rewrite

with <cap: int = 5>
fact capped(v: int): int match v {
    case const c => c
    case _       => 0
} join max

/// The argument is left out: this is the fact `capped`, at the default.
rule at_default(x: int) when capped(x) == 5 { x + 0 => x }

/// The argument is written out here: this is a different fact, `capped<3>`.
rule at_three(x: int) when capped<3>(x) == 3 { x * 1 => x }

export func apply(p: Program): Program! {
    return rewrite(p, [at_default, at_three])
}
```

- **Different arguments are different facts**: the engine computes and caches by "fact + arguments", so `capped<3>` and `capped` each have a value of their own, and writing one argument does not change the fact read elsewhere.
- **An argument must be a compile-time constant**: the value of a fact is decided by the program and by the arguments as written, so that every rule sees one answer. An argument that is not a constant is refused (`SemanticDiag::FactArgumentNotConstant`).
- **A query is written where a value is matched**: a fact is queried like a function call, but in a rule's condition (`when`) or in the body of another fact ([9.5](#95-facts)) — that is where "how the value was built in the graph" is available. An ordinary function body has no such construction, and `capped(y)` written there is not a query (`SemanticDiag::NoMatchingFunction`).

Counter-example: the argument of the fact is the rule's parameter `y`, which is not a constant.

<!-- camel-snippet: counter-example: the argument of the fact is a rule parameter, not a constant -->

```cml
with <cap: int = 5>
fact capped(v: int): int match v {
    case const c => c
    case _       => 0
} join max

rule reads_constant(x: int, y: int) when capped<y>(x) > 0 { x * y => x }
```

```text
error[SemanticDiag::FactArgumentNotConstant]: The argument 'cap' of a fact is not a compile-time constant of type 'int'.
```

**Where a `with` parameter is visible, and the instantiation channel.** A `with` parameter is visible to **every** part of the fact: the clauses (`case`, `join`/`meet`/`widen`/`narrow`) read the same arguments as everything else — the functions generated for them declare the same `with` parameters, which is the mechanism a function's `with` parameters use. Facts and rules **share one instantiation channel**: the key of an instance is the type arguments plus the `with` arguments, so a fact head with type parameters (`fact range::<T>(v: T): Interval`) and a fact with `with` parameters on one declaration are the same thing, exactly as the target form of a rule in [9.3](#93-type-parameters-and-with-parameters).

## 9.7 Checking, explaining, and "which rule fired"

- `check(rules, samples, seed)` samples a rule: it makes random instances, computes both sides and compares them, and prints a table of findings. It only samples: a counter-example says the rule is wrong, while no error only says that no counter-example was drawn this time. Every standard library rule passes `check` before it goes in.

```cml
module checked

import { check } from rewrite

rule sub_self(x: int) { x - x => 0 }

rule fold_scales(x: int, const c1: int, const c2: int) { x * c1 + x * c2 => x * (c1 + c2) }

func main(): int sync {
    println(check([sub_self], 100, 1))
    println(check([fold_scales], 100, 1))
    return 0
}
```

```text
sampled sub_self: proposition equality, evidence samples 100, fired 8 of 8, coverage integer scalar int: (x - x) = 0

sampled fold_scales: proposition equality, evidence samples 100, fired 8 of 8, coverage integer scalar int: ((x * c1) + (x * c2)) = (x * (c1 + c2))
```

- Readers: `rule_name`, `rule_two_way`, `rule_needs` and `rule_consts` read a `Rule`; the `Run` that `run<rules>` returns is read by `explain`, `explain_all` and `why_not` ([standard library reference](../../stdlib-reference.md)).
- **Asserting "which rule fired" from a graph dump**: such an assertion compares `text` blocks of the dump, and before comparing it must **normalize the attributes that carry a position** — `origin`, `span`, `srcfile` and the like — and it compares an excerpt as a run of consecutive lines. Otherwise changing an unrelated line, or a temporary directory, turns the assertion red: even `origin` of one node differs before and after a rewrite.

A small program that does nothing else shows how to read it: the `math:abs_l` node inside `distance` is gone after `fired::simplify`, and that is what "this rule fired" reads like. (Lines that do not carry a position are left out here, and the values of `origin` are exactly the kind of thing that changes.)

<!-- camel-snippet: graph-dump: the text block is an excerpt of two std::gir dumps, not the program's stdout -->

```cml
// fired.cml
module fired

import { abs } from math
import { Program } from graph
import { rewrite } from rewrite

// abs(x) => x, with no condition, so the dump is about the rule firing and nothing else.
rule drop_abs(x: int) { abs(x) => x }

export func simplify(p: Program): Program! {
    return rewrite(p, [drop_abs])
}

func distance(a: int, b: int): int {
    return abs(a + b)
}
```

```bash
camel ./fired.cml std::gir
camel ./fired.cml fired::simplify std::gir
```

```text
distance (camel ./fired.cml std::gir):
    N0A [label=":op/add_l", type="OPER", origin=4150, span="15:17-15:18", srcfile="./fired.cml", value_type="int"];
    N0B [label="math:abs_l", type="OPER", origin=4151, span="15:11-15:14", srcfile="./fired.cml", value_type="int"];
    N0A -> N0B [label="0|0", type="NORM"];
    N0B -> O04 [label="0", type="EXIT"];

distance (camel ./fired.cml fired::simplify std::gir):
    N0A [label=":op/add_l", type="OPER", origin=4269, span="15:17-15:18", srcfile="./fired.cml", value_type="int"];
    N0A -> O04 [label="0", type="EXIT"];
```

## 9.8 Forms that are not implemented

These forms are all ruled ([rules language v2](../../design/rewrite/v2.cn.md), section 1, and [design principles 5.5](../../design-principles.en.md)), and this page marks each of them "not implemented" where it uses it. What they read as today:

| Form | Today |
|---|---|
| the right side of a rule reading a `with` parameter | `SemanticDiag::RuleWithParameterOnASide`, whose message says "not implemented yet" |
| type parameters and `with` parameters on one rule | `SemanticDiag::RuleWithParameterGenericHead` |
| `std::opt rules=[law::negate]` on the command line, `f->std::opt<rules: [negate]>` in source | `SemanticDiag::ParamArgumentUnknown`; the name `std::opt` does not resolve in source today either |

## Related documents

- [Rules language v2 (design)](../../design/rewrite/v2.cn.md): the target spelling of rules and facts, `check`, the scheduling combinators
- [46 Rules](../../technical/46_rules.md): what rules and facts look like in the implementation (a technical record)
- [Standard library reference](../../stdlib-reference.md): every function of the `rewrite` module (`rewrite`, `saturate`, `extract`, `check`, `run`, `explain`, `why_not`, the four readers)
- [Chapter 17 · Teaching the compiler a law](../../tutorial/17_teaching_a_law.en.md): writing a rule from scratch and checking it with `check`