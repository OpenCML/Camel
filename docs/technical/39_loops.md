# Loops: the `do` Form, the LOOP Node and How Each Back End Runs It

This note describes how a loop goes from source to execution. The normative text is
`docs/design-principles.cn.md`, sections 3.10 (a loop is an expression with a state; no `break`
or `continue`; a `LOOP` node lowered to tail recursion at each subsystem's entry, then made native
one subsystem at a time; one capture table for the condition and the body; `for … while`
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
2. The condition graph and the body graph (`f.loopN.cond`, `f.loopN.body`) are created with
   their ports (the state; the body the element first) before either is built, and are
   registered as **capture twins** (`captureTwins_`): `captureInto` makes every capture made
   into one also into the other, at the same position. So the two capture tables are equal at
   every moment, including captures the link step adds later.
3. Each graph is built as a body of its own (a block, `sync` if written so, `return` ends the
   block with the next state). The condition must be a `bool` (`LoopConditionNotBool`); the
   body must give the state (`LoopBodyTypeMismatch`), a value that enters it (an `int` for an
   `int?`) gets the CAST at the body's exit.
4. The capture ports become with-ports; the LOOP node is added with Norm inputs (the array,
   the first values); its With inputs are linked by the link step, in the twins' order, as the
   captures of a FUNC node are.
5. Like a branch, the loop uses every variable its graphs capture and writes those they write
   (ordering, `wait`, `IgnoredSideEffect`); in a sync block it joins the chain. A loop whose
   value nothing uses outside a sync block does not run; when its graphs have effects that is
   reported as `EffectNotExecuted` ("The loop").

Storage (`include/camel/runtime/graph.h`): kind 15; the frozen payload `GCLoopRef` holds the form
and the reference-table indexes of the two graphs (both marked callee); a draft holds
`GCLoopBody` with the graphs themselves. Both payloads also hold `collectionCount`, the number of
collection slots (section 3.1, zero for a loop written in source today). Decode, clone, both
encoders, the session's graph enumeration and reachability know it. Every commit checks the
payload (I12/I13) and `validateRuntimeLoopInterface` (op-semantics I18, I19).

`std::rgir`:

```
N2A [label="LOOP while cond=main.loop2.cond body=main.loop2.body", type="LOOP", ...];
```

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
(`include/camel/execute/loop_driver.h`) takes one element per round from it, and the lowering
(`src/runtime/loop_lowering.cpp`) uses `:struct/len_arr` and `:op/idx_arr` on it. No back end
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
| `std::nvm` | natively (`NodeVMSchedPass::evalLoop`): one call of the condition graph and one of the body graph per round, the state in the node's slot |
| `std::fvm`, `std::jit` | the private lowering at its entry, by default (faster today, section 5). Natively under a debugger and with `std::fvm(native_loop=true)`: one `SCHD` instruction (marks `LoopWhile`, `LoopFor`, `LoopForWhile`, the two graphs in extra words), run by `FastVMSchedPass::evalLoop` through the call path of the higher-order operators (a compiled body runs compiled), so that a debugger shows the loop's own graphs |
| `std::macro` evaluator | natively, round by round, at most 1,000,000 rounds |
| `std::prl` | the private lowering at its entry and when a runtime callback supplies an original graph containing a loop |
| `std::cpp` | the private lowering at its entry; the generated tail calls are loops |
| ONNX export | refused where the loop is written (the LOOP to ONNX Loop translation is the next step) |
| autodiff | differentiated through a synthesized reverse loop (section 3.2); the forward loop collects the tape |
| debugger | the loop's graphs are arms of their function: folded into its frame by default |

**The lowering** (`camel/runtime/loop_lowering.h`): per loop, three graphs,
`rec(i, S; c, xs) = if go then next(S; c, i, xs) else done(S)`, `next = rec(i + 1, body(xs[i],
S; c); c, xs)`, `done(S) = S`, with `go` the condition, `i < len(xs)`, or both (the condition
first). The arms are graphs under a BRCH, as the front end builds an `if`, so the call of `rec`
is in tail position on every back end, and the lowered loop is not counted against the recursion
limit. The program is committed **detached** (`RuntimeGraphDraftSession::commit(true)`): the
context's program keeps its LOOP nodes.

The parallel scheduler also accepts function graphs obtained during execution, for example
the CPU fallback of a native operator that reads a function's original graph. Its per-run
execution-graph cache lowers such a graph once, under a mutex, before allocating the callee
frame. Arguments and captures are bound through the resulting frame's own ports. Detached
copies and their original graphs remain rooted by the context's graph manager; the cache
holds no function values or invocation data. Concurrent publication of those graph roots is
serialized, with collections inhibited only while publishing or rewriting, never while
running the callee. The bytecode hand-off excludes original LOOP graphs, so bytecode compiled
for a source graph cannot be paired with a lowered graph's frame layout.

**Operators that read graphs see the program's own.** An operator that reads the graph of a
function value it receives instead of calling it (Tiles' `run`, `run_device`, `source`,
`inspect`, `benchmark`; `onnx:export_model`; `autodiff:grad`, `with_grad`) declares
`OperatorTraits::inspectsGraphs`. The lowering passes every operand of function type of such an
operator through `:graph/original` (`src/builtin/operators/graph.h`), which turns a function
value of the private copy into the same function over the program's graph it was copied from
(`Program::original()`, recorded by the detached commit; the closure is kept, a copy has its
original's ports). So Tiles sees a function's LOOP, and the ONNX export refuses it where it is
written, on every back end (`modules.tiles` `loop_structure_*`, `linear.loops`
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
inlined). The driver takes one element of an array per round; the other sequences are turned
into their array of elements before the loop starts (section 2a), so no second form reaches it.
The macro evaluator's limit on
the rounds of a loop is `--max-macro-rounds <n>` (else `CAMEL_MAX_MACRO_ROUNDS`; default
1000000, 1 to 2^40), the way the nesting limit is `--max-macro-depth`.

A collecting loop keeps its state and its tapes in one tuple, `(state…, tape…)`: the k state
components are updated in place, and each round's `v_c` is appended to tape `c` with
`Array::append` (O(1) amortised, O(n) for the loop; the tape is uniquely owned by the loop, so the
append is unobservable). The work tuple lives in the node's slot (NodeVM, FastVM) or in a rooted
tuple (the macro evaluator), so the arrays are traced; the tapes grow on the run-time heap, so a
limit hit is `RuntimeDiag::MemoryExhausted` reported while "collecting the values a loop produces
each round" at the loop's source location. There is no round limit. The lowering does the same
with two hidden operators, `:loop/collector` (a fresh array, once before the loop) and
`:loop/collect` (append in place, returns the tape), effectful by default so no pass removes or
reorders them; `next` carries the `(state…, tape…)` tuple.

**Lowered or native on std::fvm and std::jit.** The choice is made once at the entry: native when
an observer asks for loop frames (`ExecutionObserver::wantsLoopFrames`, true for the debugger) or
with `CAMEL_FVM_NATIVE_LOOP=1`, lowered otherwise; the choice is logged under the `FastVM` scope.
The two give the same results, faults and output.

## 5. Timings

Ten million rounds, whole process, one container, three runs each, the median
(`docs/design/loops/examples/bench_loop.cml` with two values of state and `bench_tail.cml`, the
same as a hand-written tail recursion; the one-value pair counts `i` to 10,000,000):

| Program | `std::nvm` | `std::fvm` | `std::jit` |
|---|---|---|---|
| loop, two values of state (default: nvm native, fvm/jit lowered) | 1.71 s | 1.33 s | 1.50 s |
| the same, native instruction (`std::fvm(native_loop=true)`) | | 2.61 s | 2.62 s |
| the same, hand-written tail recursion | 0.98 s | 0.49 s | 0.49 s |
| loop, one value of state (default) | 0.94 s | 0.48 s | 0.82 s |
| the same, native instruction | | 0.62 s | 0.59 s |
| the same, hand-written tail recursion | 0.70 s | 0.38 s | 0.37 s |

The native instruction pays a nested interpreter entry for each of its two calls a round, where
the lowered loop's calls stay in the dispatch loop; it is therefore not the default on std::fvm
and std::jit. A loop with several values of state also builds the tuple of the next state every
round (the body's FILL), which a hand-written recursion over separate parameters does not.

Next steps: a bytecode loop on std::fvm (the state in frame slots of its own, the two calls in
the dispatch loop, a backward jump), at least as fast as the lowering, then made the default;
compiling it with the JIT; passing the k values of the state separately in the lowering when the
body ends in a tuple FILL; and in their turn std::prl, std::cpp, ONNX and autodiff.

## 6. Tests

`test/plans/feat/linear/loops.plan.toml` (shapes, effects, captures, nesting, edges, a million
rounds under a recursion limit of 3, faults, passes, std::macro, dumps, the five diagnostics, the
refusals, and the iterables: `loops.iterate_string`, `loops.iterate_map`, `loops.iterate_tensor`,
`loops.iterate_empty`, the `*.native_loop` and `.macro` forms, `loops.err_not_iterable`,
`loops.err_tensor_rank0` and `loops.fault_tensor_rank0`), `parse.rulings` (syntax, AST, GCT, graph), `tools.debugger`
`debugger.loop_frames`, four loop programs in `test/tools/cppexec/expect.txt` (one of them over a
string), and progen's `loop` feature (all forms, sync bodies with effects).

refusals), `parse.rulings` (syntax, AST, GCT, graph), `tools.debugger` `debugger.loop_frames`,
three loop programs in `test/tools/cppexec/expect.txt`, and progen's `loop` feature (all forms,
sync bodies with effects).

`test/plans/feat/linear/loop_collect.plan.toml` covers collection slots through two test-only
passes (`test::loop_collect`, `test::loop_collect_summary`, enabled by
`CAMEL_ENABLE_TEST_PASSES=1`): the four shapes and the three ways a loop runs no rounds; the same
on `std::nvm`, `std::fvm`, `std::jit`, `std::prl`, and the native FastVM loop on fvm and jit,
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
