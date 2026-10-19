# Loops: the `do` Form, the LOOP Node and How Each Back End Runs It

This note describes how a loop goes from source to execution. The normative text is
`docs/design-principles.cn.md`, sections 2.10 (a loop is an expression with a state; no `break`
or `continue`; a `LOOP` node lowered to tail recursion at each subsystem's entry, then made native
one subsystem at a time), 2.12 (one capture table for the condition and the body; `for … while`
is a third form of the same node; several values of state are a tuple) and 2.13 (the `do`
syntax; `@numeric` covers loop bodies). The design work behind it is `docs/design/loops`, the
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

1. The first values and the array are built where the loop is; they do not see the names of
   the head. Their types are the state types; `null` and `[]` give none
   (`LoopStateWithoutType`); the sequence must be an array (`LoopSequenceNotArray`).
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
`GCLoopBody` with the graphs themselves. Decode, clone, both encoders, the session's graph
enumeration and reachability know it. Every commit checks the payload (I12/I13) and
`validateRuntimeLoopInterface` (op-semantics I18, I19).

`std::rgir`:

```
N2A [label="LOOP while cond=main.loop2.cond body=main.loop2.body", type="LOOP", ...];
```

## 3. Consumers: `camel/runtime/loop.h`

`LoopView` gives what op-semantics 11.1 lists: the form, k, the state types, the iterable, the
first values, the captures, the two graphs, the ports of the state, the element and the
captures in each graph, the next state and (for a tuple FILL) its components, and the effect
summaries (`effectSummaryOf`, computed from the nodes). Graphs are `GCGraph *`, nodes NodeIds,
types `Type *` (the design's GraphId and TypeId as this runtime spells them).

Passes treat the node as opaque: it carries ordering, the inliner's call graph and effect
analysis look into its two graphs, and nothing lowers it in the shared program.

## 4. Back ends

| Consumer | How it runs a LOOP |
|---|---|
| `std::nvm` | natively (`NodeVMSchedPass::evalLoop`): one call of the condition graph and one of the body graph per round, the state in the node's slot |
| `std::fvm`, `std::jit` | the private lowering at its entry, by default (faster today, section 5). Natively under a debugger and with `CAMEL_FVM_NATIVE_LOOP=1`: one `SCHD` instruction (marks `LoopWhile`, `LoopFor`, `LoopForWhile`, the two graphs in extra words), run by `FastVMSchedPass::evalLoop` through the call path of the higher-order operators (a compiled body runs compiled), so that a debugger shows the loop's own graphs |
| `std::macro` evaluator | natively, round by round, at most 1,000,000 rounds |
| `std::prl` | the private lowering at its entry and when a runtime callback supplies an original graph containing a loop |
| `std::cpp` | the private lowering at its entry; the generated tail calls are loops |
| ONNX export | refused where the loop is written (the LOOP to ONNX Loop translation is the next step) |
| autodiff | refused where the loop is written (forward and backward loops are a later step) |
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
`inspect`, `benchmark`; `onnx:export_model`; `autodiff:grad`, `value_and_grad`) declares
`OperatorTraits::inspectsGraphs`. The lowering passes every operand of function type of such an
operator through `:graph/original` (`src/builtin/operators/graph.h`), which turns a function
value of the private copy into the same function over the program's graph it was copied from
(`GCGraph::originalGraph()`, recorded by the detached commit; the closure is kept, a copy has its
original's ports). So Tiles sees a function's LOOP, and grad and the ONNX export refuse it where
it is written, on every back end (`modules.tiles` `loop_structure_*`, `linear.loops`
`loops.grad_refused_lowered`, `loops.onnx_refused_lowered`).

**Recursion limit.** A round calls the condition and the body one level deep and returns before
the next round; no number of rounds takes depth (`--max-recursion-depth 3` runs a million rounds
on every back end: `linear.loops` `loops.not_recursion`).

**One driver for the rounds.** NodeVM, the native loop of FastVM and the macro evaluator no
longer each write the round: `camel/execute/loop_driver.h` holds the order of a round (the
condition first, then the next element, then the body), the ports a round binds
(`bindLoopRound`: the element, the k values of the state, the captures) and what ends the loop;
each interpreter supplies a frame and a call (`driveLoopRounds` is a template, so the services are
inlined). Iterating over more than arrays will be one change there. The macro evaluator's limit on
the rounds of a loop is `--max-macro-rounds <n>` (else `CAMEL_MAX_MACRO_ROUNDS`; default
1000000, 1 to 2^40), the way the nesting limit is `--max-macro-depth`.

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
| the same, native instruction (`CAMEL_FVM_NATIVE_LOOP=1`) | | 2.61 s | 2.62 s |
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
refusals), `parse.rulings` (syntax, AST, GCT, graph), `tools.debugger` `debugger.loop_frames`,
three loop programs in `test/tools/cppexec/expect.txt`, and progen's `loop` feature (all forms,
sync bodies with effects).
