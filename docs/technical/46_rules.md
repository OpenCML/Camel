# Rules, and the engines that apply them

Step 8 of the graph store (design: [rewrite](../design/rewrite/README.cn.md),
[graph store](../design/graph-store/README.cn.md) 5.2). This note records what the code does;
[44](44_values_with_several_writings.md) describes the engines underneath.

## 1. A rule is a value

```camel
export rule mul_one(x: int) { x * 1 => x }
rule combine(x: int, const c1: int, const c2: int) { x * c1 + x * c2 => x * (c1 + c2) }
rule swap(a: int, b: int) { a + b <=> b + a }
```

`rule` (and `when`, after the closing brace) are names that mean something only where a rule
declaration starts, so a program may still have a variable called `rule`. `<=>` is a token of its
own. A parameter marked `const` only matches a constant, and it must be a constant of the type the
parameter is declared with (rewrite design 2 (second version) 1): the matcher checks the declared
type against the constant's, so a rule no longer guards `isI32(c)`-style itself.

The AST builder (`Builder::visitRuleDecl`) turns a declaration into two functions and a
module-level value, which is what the design calls the shorthand of a pair of functions with the
same parameters:

```camel
func mul_one__lhs(x: int) => x * 1          // the pattern
func mul_one__rhs(x: int) => x              // what it becomes
let mul_one = __rule('mul_one', false, 0, 0, mul_one__lhs, mul_one__rhs)
```

A condition adds a third function and a different maker:

```camel
rule add_zero(x: int, y: int) when isZero(y) { x + y => x }

func add_zero__lhs(x: int, y: int) => x + y
func add_zero__rhs(x: int, y: int) => x
func add_zero__when(x: Node, y: Node) => isZero(y)   // the parameters as nodes of the draft
let add_zero = __rule_when('add_zero', false, 0, 0, add_zero__lhs, add_zero__rhs, add_zero__when)
```

A `const` parameter is the exception: it is the constant's *value* inside the condition, of the
type it was declared with (`func keep_positive__when(x: Node, c: float) => positive(c)` for
`rule keep_positive(x: float, const c: float) when positive(c) { x * c => x }`), so the guard is
arithmetic on the value and needs neither `constant(node, sample)` nor the `match … case null`
layers around a read. The engine passes that value where it passed a node, and the parameter bit
the rule carries is what tells it which parameters are which.

Reaching the *node* of a `const` parameter is `node_of(c)` (the rule on
`rule_when_const_node`: "条件函数按 `const` 形参追加隐藏的节点形参，构建期把 `node_of(c)` 换成它；
不引入新类型"). The condition takes one hidden node parameter per `const` parameter, appended after
the rule's own, in the same order:

```camel
rule mul_one(x: int, const c: int) when sign(node_of(c)) == 1 { x * c => x }

// func mul_one__when(x: Node, c: int, c__node: Node) => sign(c__node) == 1
```

`node_of` is read where the condition is built, so it is not a call of a function and there is no
new type: the name `node_of(c)` becomes that hidden parameter, and the map the builder holds while
it builds one rule's condition is what refuses a name that is not that rule's own `const`
parameter (`SemanticDiag::NodeOfNotAParameter`). `node_of` written anywhere else meets an empty map
and is refused by the same diagnostic, by name. The hidden parameters are what
`RuleSource::whenNodesStart` counts from, and the engine hands each of them the node its `const`
parameter was bound to.

The left side is what binds a `const` parameter, so a declaration that gives one of them no
position to match a constant in is refused where it is written
(`SemanticDiag::RuleConstParamNotMatched`): with no position on the left, the parameter is given no
constant in the condition and its hidden node parameter is given no node, which the engine reports
as an internal error at every match. The hidden node parameter exists whether the condition writes
`node_of(c)` or not -- what the declaration must do is use the parameter on the left, as in
`x * c`.

`__rule` and `__rule_when` are operators of the module `rewrite` (`modules/rewrite`), which the
declaration imports itself (with `graph.Node` for the condition). They make a `RuleObject` (type
`Rule`): the name, the direction, the permissions the rule needs, a bit per `const` parameter and
the function values. The rule is a value, so it can be held in a variable, passed to and returned
from a function, and stored in a `Rule[]`; a rule set is an ordinary array (`[mul_one, combine]`,
type `Rule[]`), made of values rather than of the functions that make them. A module-level value
is computed before the pass of its module runs, so a rule is ready when the engine is called.

`Rule` is not a primitive: the compiler names the type the C++ module registers, and everything
else about a rule set is the language's own arrays, functions and control flow. The declaration
always produces the `Rule` value; a program that only wants the two sides can still call
`name__lhs` and `name__rhs` in the same module, but they are an implementation detail of the
declaration, not the rule's public surface.

The context of a rule (`rule fuse<f, g>(...)`) is read by the grammar and refused by the compiler
(`ConstructNotSupported`) until generics land (H38 item 5).

A rule may state the numeric permissions it needs with `#requires(...)` before the declaration
(design principles 7.2): a rule that rewrites `x * c1 + x * c2` into `x * (c1 + c2)` is only
correct where `reassociate` is allowed. `#requires(reassociate, contract)` names permissions; the
core group `finite_math` names the four assumptions it groups, so `#requires(finite_math)` needs
all four, and a module may declare its own group of permissions (`tag fast = [reassociate,
contract]`) and require `#requires(fast)`: the AST pre-pass expands same-module groups
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
| `p->run<rules>` | `rewrite` with the round budget explicit, as a `Run` with its cost (section 3.1); `rounds` (16), `strict` (false) and `explain` (false) have defaults |
| `p->run<rules, explain: true>` | the same, with the attribution record open; `explain(r, a, b)` reads the chain between two values and `explain_all(r)` the whole log (section 3.2) |
| `explain(r, a, b)` | the shortest chain of rewrites connecting the value `a` to the value `b`, as a `Proof` |
| `explain_all(r)` | every rewrite the run made, as a `Proof`; the debugging view |
| `why_not(r, rule, v)` | why `rule` did not fire at the value `v` |

`rewrite` and `saturate` leave the functions that are the sides of rules (`__lhs`, `__rhs`, and
`__when`) alone. The result is `Program!`; the errors are `RuleInvalid`, `RuleTwoWay` and
`NoCostModel`. A rule's condition may not have an effect: a condition is evaluated while the engine
holds a session on the program it is rewriting, so a rewrite or a commit there replaces the program
the rewrite started from while it runs, and the rule is refused where it is written. A pass is
a function `(p: Program) => Program!` (`camel prog.cml mod::pass std::nvm`):

```camel
export func apply(p: Program): Program! { return rewrite(p, [add_zero, mul_one, combine]) }
```

The engines read `Rule` values from the array; no function is called to make a rule. Compiling a
rule makes constants and can collect, so a kernel reads the array again each time and copies what
it needs from a rule object before the next allocation; a condition still runs through
`NodeVMSchedPass::callGraph`, and the rule object is rooted for it.

### 3.1 Scheduling, and what an application costs

The scheduling combinators are ordinary Camel functions in `stdlib/rules.cml`, over stages (a
stage is a `Rule[]` applied by `run` to a fixpoint). The rule sets and the choices are `with`
parameters, so they are written in the angle brackets of the call:

| combinator | semantics |
|---|---|
| `seq<stages, strict>(p)` | the stages once each, in the written order; a failing stage fails the sequence |
| `first<stages, strict>(p)` | the stages in the written order, the first one that rewrites something is the result; a stage that matches nothing does not stop the choice |
| `repeat<rules, rounds, strict>(p)` | one round at a time until a round applies nothing or `rounds` rounds; the round that proves the fixpoint is made and counted |
| `until<rules, done, rounds, strict>(p)` | one round at a time while `done(p)` is false, until a fixpoint or `rounds` rounds |
| `alternate<left, right, rounds, strict>(p)` | one round of `left`, then one of `right`, until a full pair applies nothing or `rounds` pairs |

Each answers a `Run<Program>`: `result` is the program, and `rewrites`, `rounds`, `visited`,
`erased`, `fixpoint` and `seconds` are what the schedule cost, summed over the stages (`fixpoint`
is the last application's; `proof` is described in section 3.2). The functions are written with
`loop ... for ... while` and `?->`; no
helper carries an index or an accumulator. A combinator never reorders a stage's rules and never
drops a match: a stage that applies nothing leaves the count at zero.

`p->run<rules, rounds: 8, strict: true>` is the same engine with the round budget explicit and the
cost kept in a `Run` value: the rule array is the ordinary argument and the three settings are
`with` parameters, each with a default, so `p->run<rules>` is the whole call for the common case. `Run` is a plain Camel struct declared in `stdlib/rewrite.cml`; the native
kernel fills its eight fields in that order, so a field is read as `r.rewrites` and the program is
`r.result`. `seconds` is the wall time of one application: compiling the rules, matching,
rewriting the graphs and the commit; it is measured in the kernel with
`std::chrono::steady_clock`. `visited` is the number of alive nodes that take part in a rewrite,
summed over rounds and graphs (`DraftWritings::rewrite` increments it for every node it
considers); `rewrites` is the number of replacements actually made. The numbers are ordinary
values, so a benchmark prints them from Camel and the plan can pin them.

### 3.2 Attribution, `explain` and `why_not`

A rewrite is only useful to a reader if it can be attributed to the rule that made it (rewrite
design 8.7). The record is kept where the application happens: `DraftWritings::rewrite` sees the
rule, the round, the match and the nodes it erases, and it is the only place that sees an erased
node before the draft forgets it. `rewrite` takes a `RewriteLog *` (null means no record); for
each rewrite it appends one `RewriteStep` with:

- the rule's source name and compiled id, and the round of the graph's rewrite;
- the graph, filled in by `applyRewrite` from the graph the step was made in;
- the node the left side matched and the nodes its variables bound, with the text each node had
  at that moment (`%id kind [operator] : type`, plus the value of a scalar constant);
- the node the right side was built as, with its text;
- the nodes the rewrite left unread and erased, with their text.

The text is taken at recording time because a node that is erased or renumbered has no later text
to read. With a null log the engine allocates nothing and takes no branch in the per-node match:
`rewrite` and `saturate` keep their H226 cost, which `bench::compare` measures side by side with
the recording on.

`run`'s `explain` parameter decides whether the application passes a log. With it, the kernel
builds one `Proof` from the log and stores it in the `proof` field of the `Run` (a finalizer
releases the steps); without it the field is null, nothing was recorded, and `explain` and
`explain_all` refuse the run with the error value `NoAttribution`, rather than answering with an
empty chain.

`explain_all(r)` gives the whole `Proof` back: every rewrite in the order of the applications,
graph by graph, round by round, nodes in id order, rules in the order of the array, which is what
makes a `seq` schedule's log follow the schedule and what the plan pins:

```
2 rewrites in 2 rounds, 9 nodes visited
  g round 1: %3 :op/add_l : int
  = %0 port : int  by add_zero
    matched %3 :op/add_l : int; %0 port : int;
    erased %3 :op/add_l : int; %2 data 0 : int;
  g round 1: %5 :op/add_l : int
  = %1 port : int  by add_zero
    matched %5 :op/add_l : int; %1 port : int;
    erased %5 :op/add_l : int; %4 data 0 : int;
```

A run that applied nothing prints one sentence (`no rewrite matched in 1 round: the program is
unchanged`), not an empty table. `explain_all` is the debugging view; the user-facing question is
the next one.

`explain(r, a, b)` answers "why are these two values equal". It reads the record as a graph rather
than a log: a step is an edge from the value its left side matched to the value its right side
produced, so the chain is a path over those pairs, read in either direction, within one graph (a
value of one function is never rewritten into a value of another). Only the chain is printed:

```
chain %2 :op/add_l : int = %6 :op/add_l : int: 2 rewrites
  %2 :op/add_l : int
    = %4 :op/add_l : int  by zero_to_one  (round 1, graph f)
    = %6 :op/add_l : int  by bump_one  (round 2, graph f)
```

- **Shortest** is the fewest rewrites: breadth-first from `a` over the step edges, stopping at the
  first depth that reaches `b`. A longer chain that happens to print shorter is never offered; the
  chain is a proof, so the step count is the claim.
- **A non-confluent intermediate** is printed, not skipped: when `a` became `v` and `v` became `b`,
  the chain names `v` even though the final program no longer has it. `v`'s text is the text the
  record took at the step; a chain that jumped from `a` to `b` because `b` is the final value would
  be a claim without a justification.
- **Backwards**: when the connection reads steps against the direction they were made, each such
  step is marked `(backwards)`; the rule named is still the one that fired. The same pair asked the
  other way round prints the same steps in the opposite order with the marks swapped.
- **Two equally short chains**: the search keeps every predecessor of a node at its depth, so it
  never silently picks one. All shortest chains are printed, in the order of the schedule (their
  step indexes, lexicographically) and numbered, under a header that says how many there are
  (`2 chains of the same length from %2 ... to %6 ..., in the order the rewrites were made`). In
  this engine a value is rewritten at most once, so the step graph of an in-place run is a forest
  of chains and a tie cannot arise today; the print does not rely on that, and a schedule that
  rewrites a value twice would show both chains rather than choosing.
- **No chain**: `explain` refuses with the error value `NoChain`, naming the two values and what it
  looked for (`no chain of rewrites connects %0 and %4: the run made no step that turns one into
  the other, directly or through a value it rewrote in between`). An empty chain would read as
  "equal", so it is not printed. One value given twice prints one sentence (`the same value: %2 ...
  is one node, so no rewrite is needed to connect it to itself`); a handle of another program is
  `InvalidArgument`.

`why_not(r, rule, v)` answers the other question 8.7 names: why the rule did not fire at a value
of the program the run produced. It answers with the first place the left side fails to match (a
different operator, a different constant, a value of the wrong type, a different permission class,
a missing permission, a node a rewrite may not read) and the values it compared; when the left
side matches, with the values the condition read and its result (a condition that cannot be
evaluated is reported as such, not as `false`). A rule that did fire at `v` is reported with the
round, the graph and the value it made, rather than as "no chain". The result is a `string!`: the
`NoAttribution` and `InvalidArgument` cases are error values, so a caller cannot mistake one for an
answer.
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
item 1), the rest is planned in the H38 report (``, section 1). In
short:

**`when` (implemented).** `rule … when expr { lhs => rhs }` compiles to a third function
`name__when(params) => expr`, and to the value `let name = __rule_when(name, twoWay, needs,
consts, lhs, rhs, when)`; the 6-argument `__rule` stays for rules without a condition. A parameter of the
condition that is not `const` is a `graph::Node` naming the node the engine bound to that parameter
in the draft being rewritten (scheme (a)), so a condition sees the live graph: its operator, type,
tags, inputs, users, constants, anchor flags; a `const` parameter is the constant's value, of the
type it was declared with (rewrite design 2 (second version) 1). The graph module's `Handle`
carries a draft, and the node
readers answer from it; `callee` is the one read that still needs a frozen program. The engine
already calls `Rule.when` after a match and before a replacement; it now builds that closure from
the Camel function, roots the rule object and its arguments across the call (a node handle, or a
constant the collector can move), and registers a
tracer that relocates the draft's static values and the constant slots copied into the compiled
patterns (a condition call is a safepoint). `check` compares the two sides only through the
engine for a conditional rule. Tests: `test/cases/modules/rules/when_rules.cml` with
`when_target.cml` (fact, shape and type conditions, each firing and refusing, all four backends,
saturation), `when_values.cml` (a `const` parameter read as a value), `const_type.cml` (the
matcher checking a `const` parameter's declared type) and `check_when.cml`. A rule, its target and the pass that applies it may also live
in one module (`same_module_pass.cml`): the pass module is a program of its own, and the program
it is also part of is built afterwards (H38 report §2.5).

**`fact`.** `fact range: Interval match v { case const c => …; case x + y => range(x) + range(y);
case _ => … } join Interval::union` declares a lattice and how a value's fact is read off the way it
was built; it compiles to `range__initial`, `range__join`, one pattern/fact pair per `case`, and the
query `range(n: Node) => __fact(…)`. `case _` is required and is what the fact is of a value no
other case matched; the clauses after the body (`join` is required, `meet`, `widen` and `narrow`
are optional) each take a binary function -- a function's name or a lambda -- and every clause
without one is the join. The case expression sees its pattern variables as `Node`s (like `when`);
the fact type is restricted to trivial data, and the fact becomes one `user.fact.<name>` fact kind
(Derived, scope Node, Anchors dependency). The facts are propagated in post-order, merged at JOIN
nodes and across the writings of a value, cached per graph revision and per draft revision. Which
of a case's variables are values and which are facts was the open question the design's example
left ambiguous; they are `Node`s, as the review of H38 decided.

**`fact` with parameters (H61).** `fact pair(a: Node, b: Node): bool match (a, b) { … }` is the same
declaration with a parameter list: the parameters are pattern variables, so a rule's condition
writes `pair(x, y)` and hands it the parts of the compound it is rewriting. The query compiles to
`func pair(a: Node, b: Node): bool => __fact('pair', …)` with one node argument per parameter; the
engine answers the fact of the value those parts belong to — the value a user of the first part
reads with exactly these parts as its operands and one of the cases matches, cases in written order
and values in node order. A declaration without parameters keeps the one-value form `range(n)`.
There is no syntax that names the whole match.

**`fact` with `with` parameters (rewrite design 7.5).** `fact range::<T>(v: T): Interval` may also
declare `with` parameters -- `fact range<limit: int = 5>(v: int): int` -- which the query writes out
(`range<3>(x)`), each defaulting to the declaration's default. The arguments are part of what the
fact is: they must be compile-time constants (a non-constant argument is a compile error), and the
description, the fact kind (`user.fact.range<3>`) and the stored value are all keyed by the name
and the arguments, so `range<3>(x)` and `range<5>(x)` are two facts, each computed and cached on
its own. Every function the declaration generates -- `range__initial`, the `range__join` and its
optional siblings, and each case's pattern and fact function -- carries the declaration's `with`
parameters, so a clause or a case names one and reads the value the query wrote out
(`join Interval::widen_after<widen_after>`), and the engine passes the query's arguments to them
as the `with` arguments of the call.

**`#requires` (implemented, H2b).** `#requires(reassociate, finite_math)` before a rule resolves the
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
  A fact about a compound is read by declaring a `fact` over its parts (H61): `pair(x, y)`
  answers the fact of the value those parts belong to.
- `fact`: are a case's variables values or nodes? They are `Node`s (the review of H38;
  implemented in H38b).



