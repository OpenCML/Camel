# Loops: the `do` Form, the LOOP Node and How Each Back End Runs It

This note describes how a loop goes from source to execution. The normative text is
`docs/design-principles.cn.md`, sections 3.10 (a loop is an expression with a state; no `break`
or `continue`; a `LOOP` node whose condition and body are regions of the graph it is in, run in
that graph's frame by every back end; `for … while`
is a third form of the same node; several values of state are a tuple; the `do`
syntax) and 7.5 (`@numeric` covers loop bodies). The node card is in `docs/design/graph-model` 3.5, and the consumer API is
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
   (`LoopStateWithoutType`). A sequence that is not an array (a string, a map) is
   turned into its array of elements by the `__iter__` operator the sequence's type resolves,
   and any other type is `LoopSequenceNotIterable`; see section 2a. A tensor is a numeric array,
   so it takes the core's own path and no module takes part in it.
2. The condition and the body are regions of the current graph (`LoopCond`, `LoopBody`), built
   where they are written like the arms of a branch (`Builder::buildArm`). Their ports (the
   state; the body the element first) are Port nodes of the graph that belong to the region and
   are declared as names in its scope; everything else the code names is read where it is, so a
   loop has no capture table. A constant is not a capture: a `DATA` node (a literal, a folded
   literal, a static value) and any node whose value is known at build time (the `constant` flag)
   is evaluated where it is read, in place, so `LoopView::externalInputs` leaves it out by default
   (`constants = false`). A value that is merely loop-invariant -- a parameter, a module value
   read through a capture port, a variable's cell -- is not constant and is an external input.
3. Each part is a block (`sync` if written so, `return` ends the block with the next state).
   The condition must be a `bool` (`LoopConditionNotBool`); the body must give the state
   (`LoopBodyTypeMismatch`), a value that enters it (an `int` for an `int?`) gets the CAST at the
   body's exit.
4. The LOOP node is added with Norm inputs (the array, the first values) and With inputs the
   results of the parts (the body's, then the condition's) and a Port node of type `int64`, the
   slot the back ends count the rounds in.
5. Like a branch, the loop uses every variable its parts used and writes those they wrote
   (`mergeArmVariables`: ordering, `IgnoredSideEffect`); in a sync block it joins the
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
that needs the condition and the body reads their regions: the two are control regions of the
graph, each with its own result and ports, and autodiff rewrites them in place.

`std::rgir` shows the node (`LOOP while`) and the two control regions it owns:

```
N2A [label="LOOP while cond=main.loop2.cond body=main.loop2.body", type="LOOP", ...];
```

## 2a. The elements of an array, a string and a map

`for x in xs` takes the elements of an array, a string or a map (design principles 3.10, 11.1,
11.3). An array of any rank is the core's own: `t[i]` takes the i-th along the first dimension
(5.4), so the round of a rank-1 array is its element and the round of a higher-rank one is the
row one dimension shorter. The type states it once
(`ArrayType::firstDimensionElement`) and every reader asks it: the builder gives the element port
that type, I18 checks the port against it, and the executors take one such value a round
(`Array::firstDimensionElement`). No module takes part in a `for` over an array, so the iterable
position holds the array itself.

A sequence that is not an array is converted to its array of elements once, where the loop is, by
the compiler-internal operator group `__iter__`. The overload is chosen by the type of the
sequence, as for any operator, so the builder names no type and each module owns its own elements:

| Sequence | Operator | Elements | Order |
| --- | --- | --- | --- |
| `T[]`, `T[m, n]` and any other array | none: the core's rule, the first dimension | `T` at rank 1, the row `T[n]` at rank 2 | first dimension |
| `string` | `:struct/iter_str` | `string` (one code point each, as `s[i]`) | code points |
| `Map<K, V>` | `:coll/map_items` | `(K, V)` | keys ascending (11.3) |

A `Set` has no overload (it is not iterated by `for` today; the question is still open).
A sequence whose declared type is an array but whose value has no dimension is refused where the
loop reads it (`loopTest`: a value with no dimension has no first dimension to iterate over); a
`Tensor` whose rank is unknown gives elements of the same open-rank array, one dimension fewer.

The result is that the LOOP node's iterable port is always an array, and every consumer sees one
form: `LoopView::iterable()` is the array of elements (an OPER node when a conversion was made),
the element port has its element type (I18), `driveLoopRounds`
(`include/camel/execute/loop_driver.h`) takes one element per round from it. No back end has
iteration code of its own, and `std::cpp` handles the string conversion (`rt::chars`) while maps
stay untranslated types there.

A numeric array used to ask the tensor module for its elements (`tensor:iter`, one row a round);
that iteration moved into the core, for every
array, because a `for` over an array is core semantics and the operator in between hid a constant
array's row count from `core.loopDomain` (`docs/technical/40_graph_facts.md`, the fact is
`shape[0]`). The module's `iter` goes away with the P4 alias (one thing, one spelling).

## 3. Consumers: `camel/runtime/loop.h`

`LoopView` gives what op-semantics 11.1 lists: the form, k, the state types, the iterable, the
first values, the values the two regions read from outside, the region ids and their nodes,
the ports of the state, the element and the captures in each region, the next state and (for
a tuple FILL) its components, and the effect summaries (`effectSummaryOf`, computed from the
nodes). Graphs are `GCGraph *`, nodes NodeIds, types `Type *` (the design's GraphId and
TypeId as this runtime spells them).

Passes treat the node as opaque: it carries ordering, the inliner's call graph and effect
analysis look into its two regions, and nothing turns it into graphs in the shared program.

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
(the second step of the rewrite). The rewrite decodes the loop's body graph and
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

**A loop of several values keeps its components, not a tuple per round.** A body that ends in the
tuple of the next state makes that tuple with a `FILL`, and the next round takes it apart again:
one allocation and k writes per round, for a tuple nothing ever reads. When the plan can prove
that the tuple is only a carrier, it is not made at all (`ExecLoop::stateComponents`,
`ExecPlan::elidedAt`). The proof, in `planStateComponents`: the loop collects nothing, the state
has at least two components, the body's result is a `FILL` whose template is a constant, its
`slots()` name every one of the k components exactly once, and every with-input of that `FILL` is
a node of the body (not a port, not a capture) that writes a slot of its own, and no node but the
LOOP reads the tuple. Then each round works on the components in the body's own slots, which
`loopTest` binds to the condition's ports as before; the tuple still exists in the state slot from
`loopBegin` (a loop whose state is a tuple has one, and the collector traces it) and is brought up
to date from the components once, when the loop ends. NodeVM and FastVM do not run the elided
`FILL` (the FastVM does not even emit it, so the JIT never sees it); the other consumers run it,
which is correct and only costs. A body whose state comes from a port, or whose tuple is read by
something else, keeps the round-by-round tuple: the plan says nothing, and every back end behaves
as before.

**A loop in an arm of a branch.** The nodes of an arm are one range between its BRCH and its JOIN
(`17_graph_rewrite_api_contracts.md`, 4.1), and a loop inside the arm is laid out inside that
range: its condition, its body and the LOOP node. The arm's entry in the plan is the **first**
node of the range, not the first node of it that does something: a loop's first position is the
first node of its regions, which is usually a port of the body, and the `Begin`/`Test` events of
the first round sit at that position. An entry chosen past it would skip the events, so the first
round would run with unbound ports and the loop would lose it (a bug since fixed). Every back end
that jumps to an arm entry — NodeVM, FastVM/JIT, Taskflow, the macro evaluator — starts at the
range's first node, so a loop in an arm gives the value of the same loop in the function body.
A loop inside another loop's body that is itself in an arm, and an arm that holds a loop and
another `if`, are the same: each loop's condition and body are regions of where the loop is, and
the commit refuses a graph where they are not (`17_graph_rewrite_api_contracts.md`, 4.1).

**Operators that read graphs see the program's own.** An operator that reads the graph of a
function value it receives instead of calling it (Tiles' `run_preferred_resident`, `source`,
`inspect`, `benchmark`; `onnx:export_model`; `autodiff:grad`, `with_grad`) declares
`OperatorTraits::readsGraphs`. It sees a function's LOOP and its regions (`modules.tiles`
`loop_structure_*`), and the ONNX export reads the LOOP as an ONNX `Loop` (`linear.loops`
`loops.onnx_exported_prl`: the export sees the same LOOP whichever back end schedules the
program). autodiff differentiates a function's own LOOP node instead of
refusing it: the forward loop collects each round's residuals in its collection slots and the
reverse pass is a loop over those arrays (section 3.2, `linear.loops`
`loops.grad_through_loop_fvm`).

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
the rounds of a loop is `--max-macro-rounds <n>` (default
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

That was where the time went: on a 32-core EPYC 7343 (shared, load average 20 to 130), whole-process
CPU time of ten million rounds, the loop against the same loop written as a tail recursion, in ns
per round:

| round | `std::nvm` | `std::fvm` |
|---|---|---|
| two values of state, `i % 7` (before) | 233.7 | 125.5 |
| of which the tuple `FILL` | 114 | 61 |
| of which `:op/mod_l` through a native operator call | 30 | 30 |
| of which the loop machinery (events, ports, counters) | 90 | 34 |
| one value of state, no `%` | 91.8 | 33.4 |
| two values of state, no `%` | 121.8 | 37.9 |
| two values of state, `i % 7` (after) | 152.3 | 40.9 |
| the same work as a tail recursion, interpreted | 155.7 | 30.4 |

Two changes, each measured on its own (median of five runs, interleaved, same host):

| | `std::nvm` | `std::fvm` | `std::jit` |
|---|---|---|---|
| before | 2.337 s | 1.255 s | 1.216 s |
| the state as its components (no tuple per round) | 1.630 s | 0.595 s | 0.598 s |
| `IMOD` / `LMOD`, the remainder as one instruction | 1.523 s | 0.409 s | 0.413 s |
| the hand-written tail recursion of the same work | 1.557 s | 0.304 s | 0.078 s |

The remainder became an instruction of its own because `%` is in every loop of this shape: `:op/mod_l`
stays a native call of the operator table, which costs about 30 ns a round in every back end (in the
JIT it is a call of the runtime, since the operator is not compiled), where `LMOD` is a divide and a
move in the interpreter and one helper call in compiled code. A compiled graph that meets an `IMOD`
or `LMOD` runs that bytecode in the helper that the other runtime constructs use
(`jitHelperBytecode`, and `collectRuntimeConstructs` keeps such a graph framed for it).

A merged change (`48345984`) makes the JIT compile a loop of regions: `LBEG`,
`LTST` and `LEND` run the interpreters' own `loop_frame.h` functions on the frame through helpers
(`07_jit_architecture.md`, 17.9), and a graph with a loop is compiled at its first call under the
default threshold (14.1). The same measurement on the merged tree (median of five interleaved runs,
whole-process CPU time, load average 60 to 75):

| | `std::nvm` | `std::fvm` | `std::jit` | tail recursion, `std::fvm` | tail recursion, `std::jit` |
|---|---|---|---|---|---|
| perf build (loop interpreted) | 1.571 s | 0.436 s | 0.435 s | 0.310 s | — |
| merged tree (loop compiled) | 1.611 s | 0.457 s | **0.363 s** | 0.309 s | 0.084 s |

`std::jit` is 20% faster than the interpreter in the same batch (0.363 against 0.457 s) and 16%
faster than its own earlier number; `std::jit hot_threshold=0` gives the same 0.363 s, and
`std::jit` without a parameter now compiles the loop too — the entry function `main` is called once,
which the call-count threshold used to leave interpreted. The compiled round is 36.3 ns against
8.4 ns for the same work as a compiled tail recursion; the difference is the two helper calls and
the frame traffic of every round. The target 0.155 s is therefore still 2.3 times away.

A later change took the interpreter's own round down, without changing the
bytecode or the plan: the state bindings (`bindLoopState`) are copied in place and inline for the
shapes a loop actually has — the components of a multi-value state (`ExecLoop::stateComponents`),
and the one value that is the state itself — instead of running every component through
`LoopShape`; a round that collects nothing writes its next state where it stands instead of calling
`takeLoopRound`; `camel::profile`'s flag is read inline inside libcamel (the exported `enabled()`
stays for modules); and the loop events take their `ExecLoop` from a constant offset
(`BytecodeHeader::loopPayload`) instead of `extra()`'s opcode switch. Paired and alternating — the
loop and the tail recursion in the same minutes, whole-process CPU time, core 3 — on the same host:

| | `std::fvm` loop | tail recursion, `std::fvm` | the loop per round |
|---|---|---|---|
| earlier tree | 44.9 ns | 30.3 ns | 14.7 ns slower |
| later tree | 28.4–30.2 ns | 30.0–32.1 ns | 0.1–3.8 ns faster (never slower, five batches) |

An event now costs 8.1 ns against 3.3 for an ordinary bytecode (it was 15.8), so a round of this
shape is four bytecodes (13.6 ns) and the two events (16.3 ns). What an event still does: two
dispatches, the two port bindings, the round counter, the safepoint poll and the profile flag. The
next step is the round as one superinstruction (`LTST`, the straight-line body, `LEND`), which is
worth an estimated 3–5 ns; `std::jit` gains from the same shared helpers (35.0 → 21.6 ns a round),
and a `for` loop is 32.6 ns a round because its `LTST` also fetches the element from the array.

What is left, and why the earlier targets are not met yet:

- `std::nvm` is at 152 ns a round against a target of 70. NodeVM runs a node at a time: 23 ns a
  node here, so the six or seven nodes of a round are the round. Trimming the events further does
  not pay; the executor that runs these graphs at interpreter speed is FastVM, which is what
  `std::fvm` is (40.9 ns a round, and 7.8 ns when the same work is compiled).
- `std::fvm` is at 40.9 ns against 24.5 (half of the 0.49 s the tail recursion took when the first reading was taken
  that recursion is 30.4 ns today, so half of it is 15.2 ns). Each bytecode of the round
  costs about 6.8 ns of dispatch and handler, and a round is six of them; a loop cannot be twice as
  fast as the same work written as a recursion over parameters, both being interpreted. Reaching
  24.5 ns needs fewer round trips through the dispatch loop: a compiled loop (next item), or a
  superinstruction that runs a round inside one bytecode. After that change the round is 28.4–30.2 ns
  against a 31 ns tail recursion — the rule's bar (no slower than the tail recursion of the same
  work) is met; 24.5 ns still needs the round as one instruction, which is the next step.
- `std::jit` now runs the rounds of the loop in compiled code (21.6 ns a round against 35.0 in the
  interpreter after that change; measured 36.3 when this was first written), but each round is
  still two calls of the helpers that run `loop_frame.h`, and the state is read from and written to
  the frame on both sides of them. The same work as a tail recursion is 8.4 ns a round when
  compiled; closing the gap needs the common shape lowered natively (the test and the end of the
  round inline, the state in registers) rather than the three events as calls, which is the next
  step, not item 2.

Next steps, in the order they are worth measuring: a superinstruction for the round (the condition,
the test and the body of a loop whose regions are straight-line bytecode run by one instruction —
the interpreter's remaining 16 ns a round, of which two dispatches and one back jump are the part
this removes); the native lowering of the common loop shape (LTST/LEND inline, state in registers);
the k values of the state passed as arguments to a compiled loop body; the translation of a loop to
an ONNX Loop; autodiff on the regions.

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
a loop of a loop in an arm, an arm holding a loop and another `if`, a macro function's arm, a
value computed in the arm) — every value the one the loop gives in the function body, on
`std::nvm`, `std::fvm`, `std::jit` (compiled, also without inlining), `std::prl`
(also `bytecode=false`) and `std::macro`. `loops_in_arms.regions` is the commit-time check that a
loop's condition and body are regions of where the loop is (section 4).

`test/plans/feat/linear/loop_collect.plan.toml` covers collection slots through two test-only
passes (`test::loop_collect`, `test::loop_collect_summary`, named in the pipeline and gated by no
environment variable): the four shapes and the three ways a loop runs no rounds; the same
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
`loops.grad_through_loop_fvm` are the source-level check under `std::macro` and `std::fvm` that
a loop of the program's own graph is differentiated (they replace the two refusals that the first step of that work
left).