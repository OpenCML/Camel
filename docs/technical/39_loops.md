# Loops: the `do` Form, the LOOP Node and How Each Back End Runs It

This note describes how a loop goes from source to execution. The normative text is
`docs/design-principles.cn.md`, sections 3.10 (a loop is an expression with a state; no `break`
or `continue`; a `LOOP` node whose condition and body are regions of the graph it is in, run in
that graph's frame by every back end; `for … while`
is a third form of the same node; several values of state are a tuple; the `do`
syntax) and 7.5 (`@numeric` covers loop bodies). The design work behind it is `docs/design/loops`, the
node card is in `docs/design/graph-model` 3.5, and the consumer API is
`docs/design/op-semantics` section 11.

## 1. Source

```camel
let total    = loop acc = 0 for x in xs do acc + x * x
let (i, acc) = loop i = 1, acc = 0 while i <= n do (i + 1, acc + i)
let p        = loop p = { x: 0 } for d in ds do { x: p.x + d }
let found    = loop hit = -1 for x in xs while hit < 0 do if x > 9 then x else hit
```

Grammar (`src/parse/syntax/parser_expr.cpp`, `parseLoop`; the generated block of
`docs/language/08_top_level_and_grammar.md`):

```
loopExpr  : 'loop' loopState (',' loopState)* loopFor? loopWhile? 'do' blockExpr
loopState : identDef '=' logicalOrExpr
loopFor   : 'for' identDef 'in' logicalOrExpr
loopWhile : 'while' logicalOrExpr
```

A comma after a first value continues the head only when a name and `=` follow, so a loop can
be an argument (`f(loop a = 0 for x in xs do a + x, 5)`). `do` is followed by a `blockExpr`,
the same production as after `then`. A head without `for` and without `while` is a syntax error
(the narrow reading of an open question: such a loop could only end through a fault).

The AST node is `LoopExprLoad` (names of the state and of the element; children: the first
values, the sequence, the condition, the body as a block). The GCT node is `LOOP`
(`include/camel/compile/gct/node.h`): tag `camel::core::LoopKind` (`While`, `For`, `ForWhile`,
`include/camel/core/loop_kind.h`), the names in its payload, children: the k first values, the
sequence (For, ForWhile), an `EXEC` holding the condition (While, ForWhile), the `EXEC` of the
body. Both `EXEC`s carry the numeric permissions in force where the loop is written.

## 2. The graph

`Builder::visitLoopNode` (`src/compile/gir/builder.cpp`):

1. The first values and the sequence are built where the loop is; they do not see the names of
   the head. Their types are the state types; `null` and `[]` give none
   (`LoopStateWithoutType`). A sequence that is not an array (a string, a map, a tensor) is
   turned into its array of elements by the `__iter__` operator the sequence's type resolves,
   and any other type is `LoopSequenceNotIterable`; see section 2a.
2. The condition and the body are regions of the current graph (`LoopCond`, `LoopBody`), built
   where they are written like the arms of a branch (`Builder::buildArm`). Their ports (the
   state; the body the element first) are Port nodes of the graph that belong to the region and
   are declared as names in its scope; everything else the code names is read where it is, so a
   loop has no capture table.
3. Each part is a block (`sync` if written so, `return` ends the block with the next state).
   The condition must be a `bool` (`LoopConditionNotBool`); the body must give the state
   (`LoopBodyTypeMismatch`), a value that enters it (an `int` for an `int?`) gets the CAST at the
   body's exit.
4. The LOOP node is added with Norm inputs (the array, the first values) and With inputs the
   results of the parts (the body's, then the condition's) and a Port node of type `int64`, the
   slot the back ends count the rounds in.
5. Like a branch, the loop uses every variable its parts used and writes those they wrote
   (`mergeArmVariables`: ordering, `wait`, `IgnoredSideEffect`); in a sync block it joins the
   chain. A loop whose value nothing uses outside a sync block does not run; when its parts have
   effects that is reported as `EffectNotExecuted` ("The loop").

Storage (`include/camel/runtime/graph.h`): kind 15; the frozen payload `GCLoopRef` holds the form
(`condRef` and `bodyRef` are none: the node names no graph) and `collectionCount`, the number of
collection slots (section 3.1, zero for a loop written in source today). The regions are
`GCRegion`s of kind `LoopCond` and `LoopBody` owned by the node: their ports are the Port nodes
the code reads the state and the element through, named by `GCRegion::params`; the With inputs of
the node are the results of the regions (which is how what they compute is reached from the node)
and the round counter. A graph that has loops holds their nodes: the passes, the facts and the
validator see them as nodes of one graph with regions. Every commit checks the payload (I12/I13)
and `validateRuntimeLoopInterface` (op-semantics I18, I19). `LoopView` reads the node; a consumer
that needs the condition and the body as graphs of their own (autodiff) takes them out in a
private program, `runtime::outlineLoopRegions` (`src/runtime/loop_outline.cpp`), and puts them
back, `passes::opt::regionizeLoops` (`src/passes/opt/inline/loops.cpp`). `test::outline_loops`
shows the graph form in a dump.

`std::rgir` shows the node (`LOOP while`); in graph form it names the graphs:

```
N2A [label="LOOP while cond=main.loop2.cond body=main.loop2.body", type="LOOP", ...];
```
(`graph form` is what `test::outline_loops` and autodiff's working copy have.)

## 2a. The elements of a string, a map and a tensor

`for x in xs` takes the elements of an array, a string, a map or a tensor (design principles
3.10, 11.1, 11.3). The builder keeps one form: a sequence that is not an array is converted to
its array of elements once, where the loop is, by the compiler-internal operator group
`__iter__`. The overload is chosen by the type of the sequence, as for any operator, so the
builder names no type and each module owns its own elements:

| Sequence | Operator | Elements | Order |
| --- | --- | --- | --- |
| `T[]` | none (already an array) | `T` | as written |
| `string` | `:struct/iter_str` | `string` (one code point each, as `s[i]`) | code points |
| `Map<K, V>` | `:coll/map_items` | `(K, V)` | keys ascending (11.3) |
| `Tensor` | `tensor:iter` | sub-tensor with the first dimension dropped | first dimension |

The tensor overload is default-imported by the tensor module (`__iter__` starts with `__`), so
a program that imports `tensor` gets it without naming anything; a rank-0 tensor is rejected
when its shape is known (and is a runtime fault otherwise), and a `Tensor` whose rank is
unknown gives elements of type `Tensor`. A `Set` has no overload (it is not iterated by `for`
today; the question is in the H4b report).

The result is that the LOOP node's iterable port is always an array, and every consumer sees
one form: `LoopView::iterable()` is the array of elements (an OPER node when a conversion was
made), the element port has its element type (I18), `driveLoopRounds`
(`include/camel/execute/loop_driver.h`) takes one element per round from it. No back end
has iteration code of its own, and `std::cpp` handles the string conversion (`rt::chars`)
while maps and tensors stay untranslated types there.

## 3. Consumers: `camel/runtime/loop.h`

`LoopView` gives what op-semantics 11.1 lists: the form, k, the state types, the iterable, the
first values, the captures, the two graphs, the ports of the state, the element and the
captures in each graph, the next state and (for a tuple FILL) its components, and the effect
summaries (`effectSummaryOf`, computed from the nodes). Graphs are `GCGraph *`, nodes NodeIds,
types `Type *` (the design's GraphId and TypeId as this runtime spells them).

Passes treat the node as opaque: it carries ordering, the inliner's call graph and effect
analysis look into its two graphs, and nothing lowers it in the shared program.

### 3.1 Collection slots

A loop can give one value per round for the executor to collect into an array (the tape of a
future autodiff rewrite, and ONNX `Loop`'s scan output). Neither the grammar nor the builder
writes such a loop yet; `collectionCount` (`s`) is part of the payload, and a pass or a later
front end sets it. With `s > 0`:

```
value        (state_0 … state_{k-1}, T_0[] … T_{s-1}[])
body result  (next_state_0 … next_state_{k-1}, v_0 … v_{s-1}),  v_c : T_c
```

The count is in the node because with `k = 2` the value `(state, array)` is otherwise
indistinguishable from two states of which the second is an array. `s = 0` is exactly the old
shape (the value when `k = 1`, the k-tuple when `k > 1`). `LoopView` exposes
`collectionCount`, `valueIsTuple`, `valueComponentCount`, `stateValueIndex`,
`collectionValueIndex`, `collectionArrayType`, `collectionElemType`, `nextStateIsTuple`,
`bodyResultType`, `bodyStateResultIndex`, `bodyCollectionResultIndex`,
`nextCollectionComponent`, and the free helpers `loopValueComponent`, `loopStateValue`,
`loopCollectedValue`; the validation requires the value tuple to be the state types followed by
the array types and the body's exit to be the state types followed by their element types.

### 3.2 The tape of a differentiated loop

autodiff (`modules/autodiff/pullback.cpp`) uses the collection slots as its tape
(`docs/handover/reports/H4.md`, "H4d 第二步"). The rewrite decodes the loop's body graph and
differentiates it as a function of the captures and the state, as it differentiates any other
graph, keeping the values its pullback reads as *residual ports*: the operands of every rule, and
for a call inside the body the callee's pullback closure. The body of the loop node becomes that
graph's forward copy, returning `(next_state…, residual…)`, and `collectionCount` becomes the
number of residual values. The tape is the collected arrays themselves: a round appends each
residual in place (section 3.1), so the tape costs one array per value the pullback reads and no
value is copied.

The reverse pass is a second LOOP node of its own (`While`, `collectionCount = 0`), not a second
loop driver. Its state is the round index `i` plus one gradient per state component and per capture
that has one; its condition is `i >= 0`; its body reads the round's residuals with `:op/idx_arr`,
calls the body's pullback graph `(dy, residual…) -> tangents`, chains the state gradient through
the round, adds each capture's gradient, and decrements `i`. The first round therefore runs with
`i = len(tape) - 1`, and a loop that ran no round leaves the state's gradient as the seed and every
capture's gradient zero. A body whose pullback reads no residual still collects one constant `1` per
round, so that the reverse pass knows how many rounds ran; that slot is not one of its inputs.
`LoopDraftView` (`camel/runtime/loop.h`) is how the rewrite reads a decoded loop and writes the two
it builds, the same accessors `LoopView` gives a consumer.

An array has a tangent of its own: the array of its elements' tangents (design principles 9.9). A
captured array that depends on a differentiated parameter therefore carries a gradient, and the
reverse of reading `xs[i]` scatters the element's gradient into that array's gradient. An array
whose elements have no tangent (e.g. `string[]`) has none, and reading it stops the gradient, as
before. Iterating an array that depends on a differentiated parameter is still refused where it is
written: the body receives one element per round, and the reverse pass has no per-round scatter of
that element's gradient back into the iterated array. A loop whose value has no tangent but reads a
differentiated value is refused at the loop, with its position, instead of being skipped: the
contribution would be dropped without a sign of it.

## 4. Back ends

| Consumer | How it runs a LOOP |
|---|---|
| `std::nvm` | natively, in the frame of the function: the plan lays the nodes of the condition, those of the body and the LOOP node out as one range (`ExecLoop`), and the interpreter starts, tests and ends rounds at the positions the plan names (`ExecLoopEvent`); the state is in the node's slot |
| `std::fvm`, `std::jit` | natively, as bytecode: the plan's events are three instructions, `LBEG` (the loop starts), `LTST` (the condition has run: stop, or bind the body's ports) and `LEND` (the body has run: the next state, and a backward jump to the start of a round), run by the same functions as NodeVM's (`loopBegin`, `loopTest`, `loopEnd`). A graph with a loop is compiled as framed code (`07_jit_architecture.md`, 17.9): the three instructions call those functions on the frame and jump on what they return, and `LEND` polls the round safepoint as the interpreter does, so the rounds stay interruptible at the same position |
| `std::macro` evaluator | natively, in the frame of the function (`stepLoops`), at most 1,000,000 rounds |
| `std::prl` | a graph with a loop is evaluated node by node, with the same events as NodeVM's (`stepLoops`), or, when it is serial, run as bytecode |
| `std::cpp` | a `for (;;)` of its own: the state in variables, the condition and the body in the loop, the next components computed before any variable is assigned (`FunctionEmitter::lowerLoop`) |
| ONNX export | refused where the loop is written (the LOOP to ONNX Loop translation is the next step) |
| autodiff | differentiated through a synthesized reverse loop (section 3.2); the forward loop collects the tape |
| debugger | the condition and the body are regions of the graph of their function: part of its frame, with the names of their ports |

**The range of a loop.** The plan (`ExecPlan`, `src/execute/exec_plan.cpp`) lays a loop out as one
piece: what its regions read from outside, the first values of the state, the nodes of the
condition, those of the body, then the LOOP node (`computeExecutionOrder`). `ExecLoop` records the
positions, the data slots a round reads and writes and the slot of the round counter; the events
at a position (`ExecLoopEvent`: `Begin`, `Test`, `End`) are run by the functions of
`camel/execute/loop_frame.h` (`loopBegin`, `loopTest`, `loopEnd`, and `stepLoops` for an
interpreter that walks the nodes). A nested loop starts again in every round of the loop around
it; going back to the start of a round does not start or test the loops around it.

**A loop in an arm of a branch.** The nodes of an arm are one range between its BRCH and its JOIN
(`17_graph_rewrite_api_contracts.md`, 4.1), and a loop inside the arm is laid out inside that
range: its condition, its body and the LOOP node. The arm's entry in the plan is the **first**
node of the range, not the first node of it that does something: a loop's first position is the
first node of its regions, which is usually a port of the body, and the `Begin`/`Test` events of
the first round sit at that position. An entry chosen past it would skip the events, so the first
round would run with unbound ports and the loop would lose it (the bug H45 fixed). Every back end
that jumps to an arm entry — NodeVM, FastVM/JIT, Taskflow, the macro evaluator — starts at the
range's first node, so a loop in an arm gives the value of the same loop in the function body.

**Operators that read graphs see the program's own.** An operator that reads the graph of a
function value it receives instead of calling it (Tiles' `run`, `run_device`, `source`,
`inspect`, `benchmark`; `onnx:export_model`; `autodiff:grad`, `with_grad`) declares
`OperatorTraits::inspectsGraphs`. It sees a function's LOOP and its regions (`modules.tiles`
`loop_structure_*`), and the ONNX export refuses it where it is written (`linear.loops`
`loops.onnx_refused_lowered`). autodiff differentiates a function's own LOOP node instead of
refusing it: the forward loop collects each round's residuals in its collection slots and the
reverse pass is a loop over those arrays (section 3.2, `linear.loops`
`loops.grad_through_loop_lowered`).

**Recursion limit.** A round calls the condition and the body one level deep and returns before
the next round; no number of rounds takes depth (`--max-recursion-depth 3` runs a million rounds
on every back end: `linear.loops` `loops.not_recursion`).

**One driver for the rounds.** NodeVM, the native loop of FastVM and the macro evaluator no
longer each write the round: `camel/execute/loop_driver.h` holds the order of a round (the
condition first, then the next element, then the body), the ports a round binds
(`bindLoopRound`: the element, the k values of the state, the captures) and what ends the loop;
each interpreter supplies a frame and a call (`driveLoopRounds` is a template, so the services are
inlined). NodeVM runs the rounds in the frame of the function and uses the same pieces of the
driver (`startLoopState`, `takeLoopRound`), and so does the macro evaluator (`stepLoops`,
`camel/execute/loop_frame.h`). The driver takes one element of an array per round; the other sequences are turned
into their array of elements before the loop starts (section 2a), so no second form reaches it.
The macro evaluator's limit on
the rounds of a loop is `--max-macro-rounds <n>` (else `CAMEL_MAX_MACRO_ROUNDS`; default
1000000, 1 to 2^40), the way the nesting limit is `--max-macro-depth`.

A collecting loop keeps its state and its tapes in one tuple, `(state…, tape…)`: the k state
components are updated in place, and each round's `v_c` is appended to tape `c` with
`Array::append` (O(1) amortised, O(n) for the loop; the tape is uniquely owned by the loop, so the
append is unobservable). The work tuple lives in the node's slot (NodeVM, FastVM) or in a rooted
tuple, so the arrays are traced; the tapes grow on the run-time heap, so a
limit hit is `RuntimeDiag::MemoryExhausted` reported while "collecting the values a loop produces
each round" at the loop's source location. There is no round limit.

## 5. Timings

Ten million rounds of a `while` loop with two values of state (`acc + i % 7`, `i + 1`), whole
process, one container, the median of three runs:

| `std::nvm` | `std::fvm` | `std::jit` |
|---|---|---|
| 2.06 s | 1.08 s | 1.07 s |

Before the loops were bytecode, FastVM and the JIT ran a lowering to tail recursion (1.67 s, 1.88 s).
A loop with several values of state builds the tuple of the next state every round (the body's
FILL), which a hand-written recursion over separate parameters does not.

Next steps: compiling a loop with the JIT; passing the k values of the state separately when the
body ends in a tuple FILL; the translation of a loop to an ONNX Loop; autodiff on the regions.

## 6. Tests

`test/plans/feat/linear/loops.plan.toml` (shapes, effects, captures, nesting, edges, a million
rounds under a recursion limit of 3, faults, passes, std::macro, dumps, the five diagnostics, the
refusals, and the iterables: `loops.iterate_string`, `loops.iterate_map`, `loops.iterate_tensor`,
`loops.iterate_empty`, the `.macro` forms, `loops.err_not_iterable`,
`loops.err_tensor_rank0` and `loops.fault_tensor_rank0`), `parse.rulings` (syntax, AST, GCT, graph), `tools.debugger`
`debugger.loop_frames`, four loop programs in `test/tools/cppexec/expect.txt` (one of them over a
string), and progen's `loop` feature (all forms, sync bodies with effects).

refusals), `parse.rulings` (syntax, AST, GCT, graph), `tools.debugger` `debugger.loop_frames`,
three loop programs in `test/tools/cppexec/expect.txt`, and progen's `loop` feature (all forms,
sync bodies with effects).

`test/plans/feat/linear/loops_in_arms.plan.toml` is the matrix of a loop written in a branch arm
(section 4): the whole `then` and `else` arm, a `while`, two values of state, statements after the
loop in the arm, an `if` in the body, a `match` arm, two loops in two arms, a collecting loop, a
loop in the root graph's arm, and the edges (no round, `for … while`, a loop inside a loop's body,
a macro function's arm, a value computed in the arm) — every value the one the loop gives in the
function body, on `std::nvm`, `std::fvm`, `std::jit` (compiled, also without inlining), `std::prl`
(also `bytecode=false`) and `std::macro`.

`test/plans/feat/linear/loop_collect.plan.toml` covers collection slots through two test-only
passes (`test::loop_collect`, `test::loop_collect_summary`, enabled by
`CAMEL_ENABLE_TEST_PASSES=1`): the four shapes and the three ways a loop runs no rounds; the same
on `std::nvm`, `std::fvm`, `std::jit`, `std::prl`,
with the back ends' output compared; `std::macro` on the two-value loop of `macro.cml`; a hundred
thousand rounds of a `while` and a `for` loop with the tapes summarised by their lengths; and a
tape that outgrows `CAMEL_GC_HEAP_LIMIT_MB`, which every executor reports as
`RuntimeDiag::MemoryExhausted` at the loop's source line.

`test/plans/feat/modules/autodiff.plan.toml` covers the differentiation through a loop (section
3.2): a summation loop (one per round, every executor in agreement), a product loop (the product
rule), an affine accumulator (the geometric series, so the state chain is checked), a two-value
state, a nested loop (the inner pullback closure collected as one value of the outer tape), an
empty loop (zero gradient), a captured array that does not depend on the parameter (the gradient
stops at the read), and the two refusals — an iterated and a captured array that does depend on it
— reported at the loop. `linear.loops` `loops.grad_through_loop` and
`loops.grad_through_loop_lowered` are the source-level check under `std::macro` and `std::fvm` that
a loop of the program's own graph is differentiated (they replace the two refusals that H4d 第一步
left).
