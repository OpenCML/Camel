# Native operators calling Camel subgraphs

Status: invocation service and NodeVM integration implemented; other backend and macro
bindings remain pending. No new language syntax or numerical permissions.
This is a prerequisite for the portable execution boundary in
[the approved Tiles route](../tiles-route/README.cn.md), section 3.3.

## Problem and evidence

`operator_t` receives `ArgsView` objects and `Context`, but has no public operation
for calling a function value. `OperatorTraits::callsGraphs` describes behavior;
it does not provide an invocation service. The schedulers implement built-in
higher-order operators separately. For example:

- `src/passes/sched/nodevm/nodevm.cpp`: `evalMarkedOperator_isolate` binds a
  function frame, checks recursion room and calls the current NodeVM.
- `src/passes/sched/fastvm/fastvm.cpp`: the corresponding implementation uses
  `higherOrderCallSite`, `populateMarkedCallFrame` and `callElement`.
- `src/passes/sched/taskflow/taskflow.cpp`: regular operators have both a cached
  and a first-resolution execution path, each running on the calling worker.
- `src/passes/sched/fastvm/jit/runtime/trampoline.cpp`: native operator calls
  also enter through JIT trampolines. Changing only the interpreted OPER case
  would leave compiled execution with different behavior.

NodeVM currently stores native-operator argument indices in `operIndices_`.
`FrameArgsView` refers to that storage. A nested call can clear or grow the same
vector, invalidating the outer operator's views. Reentrancy requires fixing this
lifetime before exposing a callback.

The test-only Tiles outlining passes verify region extraction and GPU execution.
They do not implement the portable `tiles:exec` contract. A GPU test succeeding
must not be used as evidence that CPU fallback exists.

## Proposed runtime interface

Add a public, synchronous function invocation service under `camel/execute`:

```cpp
slot_t invokeFunction(
    Context &context, Function *function,
    std::span<const slot_t> withArguments,
    std::span<const slot_t> normalArguments);
```

This is an execution service, not a new graph node or a Tiles-specific hook.
It reuses the current backend's function-call machinery, including captures,
parameter binding, faults, recursion accounting and observation. It never creates
an independent interpreter or resets recursion depth.

Keep `operator_t` unchanged. Each backend installs an RAII invocation scope while
entering a native operator declared `callsGraphs`. The scope supplies a callback
and its context. Scopes form a thread-local stack: a shared `Context` must not
hold a mutable current-worker callback. Lookup verifies both the active context
and the existence of a scope; outside such a scope invocation fails explicitly.
The scope and callback cannot escape the native call or be invoked on another
thread. Nested native operators restore the previous scope on every exit path.

The scope grants no purity or numerical permission. The module must still declare
`callsGraphs`, effects and possible faults correctly. Merely inspecting a graph
continues to use `inspectsGraphs` and requires no invocation scope.

## Lifetime and backend obligations

1. Give reentrant operator calls stable argument-index storage. In NodeVM use
   invocation-owned storage for `callsGraphs` operators; keep the existing fast
   path for operators that cannot invoke functions.
2. Bind argument slots and captures into the callee's traced frame before a
   collection point. Follow each backend's existing ownership rules for releasing
   that frame. Do not suppress collection for the entire subgraph execution.
3. The caller must keep the function and argument objects rooted until binding.
   The returned value must be stored in a traced slot before a later safepoint.
   A native operator making several calls must root intermediate results itself.
4. Reuse existing recursion checks. A synchronous invocation is an ordinary
   nested function call, not an implicit tail call. Preserve the caller's source
   location when admission fails and the callee's location for faults inside it.
5. Propagate faults; this service is not an isolation boundary. `attempt` retains
   its existing responsibility. Restore frame, depth and invocation state during
   unwinding, including a JIT call returning a recorded fault.
6. Cover FVM switch and computed-goto dispatch, JIT native-call trampolines, and
   both Taskflow regular-operator paths. A Taskflow call uses the current worker's
   normal evaluation/handoff policy rather than a new thread pool.
7. Native calls made during macro evaluation need an explicit evaluator binding
   or an explicit unsupported diagnostic. They must never inherit an unrelated
   runtime scope. Audit this path before publishing the interface.

## Tiles integration after the service exists

`tiles:exec` keeps the extracted function as a graph-visible input. With a usable
GPU it executes the region's physical plan. Without a GPU it invokes that same
function through this service. Configuration errors, compilation failures and
runtime faults are not silently converted into CPU execution.

This does not replace explicit boundary transfers, device-resident values,
symbolic compilation or cache identities required by the route. Those remain
part of the production pass. `run` and `@gpu` retain explicit GPU semantics.

## Acceptance tests

Use a test native operator that invokes its function input, then reads its own
arguments again and invokes another function. This specifically exposes shared
scratch-array invalidation. Run it in NVM, both FVM dispatch implementations,
JIT and the parallel backend, with nested captures, tuples and GC stress.

Also require concurrent independent contexts, nested native callbacks, an
escaping callee fault followed by another invocation under `attempt`, recursion
limits, and macro evaluation. Check diagnostic locations and observer events.
Finally run identical extracted Tiles regions through CPU fallback and both
GPUs; basic operations compare exactly and transcendental operations use their
published error bounds. Device failures must remain distinguishable from the
absence of a GPU.
