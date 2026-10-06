# Automatic Differentiation as Graph Rewriting

> **Read [`../design-principles.cn.md`](../design-principles.cn.md) first** (English translation: [`design-principles.en.md`](../design-principles.en.md)). It is the language author's normative statement of what Camel is and of its execution model; it prevails over code and other documents, and questions it leaves undecided go to the language author.
>
> In its terms (section 4): automatic differentiation is a **library built on the pass mechanism, not part of the language core**. Its precondition is that `std::macro` runs before `grad` / `value_and_grad` / `vjp` are used: a graph on which macros have not been executed lacks the information differentiation needs. When the precondition is not met the library must give a clear diagnostic. Pass order is otherwise unrestricted by the language and may change results.
>
> **Implementation status**: an unmet precondition can still surface as a run-time error instead of a clear located diagnostic (e.g. "branch arms must be subgraph calls" after `std::opt::simplify` on the GRU benchmark; tracking item N3 / D21).

Status: implemented through phase 5 and phase 7 (§8); phase 6 (thin ONNX) is
partly done: `onnx::export` exists as a module-contributed translation pass,
but shape specialization still happens inside it and `onnx.export_model` still
runs its own simplification (§7, "Status"). Supersedes the tape-based synthesis inside `nn.apply_gradients`, which
has been removed together with `Parameter`.

Implementation map:

- Core: `include/camel/core/derivative.h` (VjpBuilder, rule registry, tangent
  spaces, tangent types of aggregates). Scalar rules live in
  `src/builtin/operators/derivative.cpp`.
- Engine: `modules/autodiff` (`grad`, `value_and_grad`, `vjp`,
  `stop_gradient`; the pullback transform in `pullback.cpp`). It is an
  ordinary package, not part of the core: grad is a macro operator like any
  other, and third-party packages can ship their own.
- Tensor and nn rules: `modules/tensor/ops/vjp.cpp` and the `vjp` entries of the
  op definitions; tree optimizers in `modules/nn/optim.cpp`.
- Verification: `test/cases/modules/autodiff` (finite differences, branches,
  recursion, `@vjp`), and `benchmarks/train.py --check`, which matches the
  MLP, GRU and Transformer gradients against PyTorch weight by weight.

Deviations from the proposal below:

- Activity analysis: only values that have a tangent and depend on a
  differentiated input are differentiated, so operators on static data
  (shape arithmetic, integer comparisons) need no rule.
- `grad` is not restricted by the core; rules and engines are package
  contributions, and `@vjp` is a decorator evaluated by `std::macro`.
- Generic simplification (`std::opt::simplify`) collapses pullback closures
  into straight-line code, and FastVM/JIT compile graphs created at run time
  (a gradient built while the program runs) on first call, so training steps
  run under every execution pass.

## 1. Principles

1. **Graph rewriting is the core mechanism.** Camel processes programs as a
   pipeline of graph passes: optimization passes rewrite graphs, execution passes
   consume a graph (return an empty graph, produce side effects), translation
   passes emit another format. Differentiation is one more rewrite, not a
   runtime subsystem.
2. **After macros, the program is a static graph.** Python frameworks trace at
   runtime because `if`/`else` cannot be overloaded. In Camel, branches are
   `BRCH`/`JOIN` nodes of a static graph, so derivatives of programs with
   branches, calls, and recursion can be built by static analysis.
3. **Macros run what can run.** A macro function is directly executable; code
   that can be evaluated at macro time (recursion over constants, calls through
   static function values) disappears before differentiation. Whatever remains
   dynamic is handled with type information.
4. **Translation passes stay thin.** ONNX export is the last pass of a pipeline
   and translates an already-specialized graph; constant folding, inlining,
   unrolling, and branch selection belong to general passes.
5. **Derivative rules are layered.** Builtin rules ship with operator
   definitions, packages may add rules for their operators, and users attach
   rules to their own functions with the `@vjp` decorator at macro time.

## 2. Pipeline Placement

```text
source ── compile ──> GIR
  ── std::macro ──────────── grad(f) runs here: f's graph -> derivative graph
  ── std::devirtualize / std::specialize / std::inline
  ── std::opt::fold / cse / dce, tensor::fuse, ...
  ── execution pass (std::nvm, std::fvm)      -> runs the program
     or translation pass (onnx::export, ...)   -> emits a format
```

`grad(f)` is a macro operator: its argument is a static function value, and its
result (a new function value) is materialized as static data by `std::macro`.
The synthesized graph joins the reachable graph closure at commit time, so every
later pass sees it like user code.

### Pass order: `std::macro` and the inlining passes

`grad(f)` transforms f's graph **as compiled**: the pullback builder takes one
pullback per branch-arm call (§4.3), so it cannot differentiate an arm that
was merged into its owner, nor the shape left when a recursive call is inlined
into its own arm.

- **`std::macro` first** is the intended order: every `grad` whose function is
  static is expanded before any rewrite, and the passes after it optimize the
  derivative like user code.
- **Without `std::macro`, or with it after an inlining pass**, `grad` runs with
  the program on the graphs the passes left. The inlining engine
  (`std::inline`, `std::opt`, `std::opt::simplify`; `MacroOperandGraphs` in
  `src/passes/opt/inline/engine.cpp`) therefore inlines nothing into the
  graphs an unexpanded macro operator can still reach: its function operand
  and what that calls or holds as a function constant. Calls of those
  functions from elsewhere are inlined as usual. The program runs and gives
  the same results; the differentiated functions and their derivatives are
  just not optimized.
- **A macro operator whose function is not a constant** (`grad(f)` where `f`
  is a parameter) cannot be expanded by `std::macro` at all. While such an
  operator is in the program, any function may reach it, so the inlining
  engine inlines nothing anywhere. Pass the function so that it is static at
  the `grad` call to get the optimizations back.

Other rewrites of a function that `grad` later differentiates at run time
(`std::opt::cse`, `std::opt::fold`) keep the result valid but may change its
floating-point rounding, because the derivative is built from a different
expression.

## 3. User-Facing API

```camel
type Linear = { w: Tensor, b: Tensor }
type Mlp    = { l1: Linear, l2: Linear }

with <m: Mlp>
func loss(x: Tensor, y: Tensor): float {
    return mse(forward<m>(x), y)
}

let g        = grad(loss)              // macro: <m: Mlp> (x, y) => Mlp'
let dm       = g<model>(x, y)          // gradients, structured like the model
let (l, dm2) = value_and_grad(loss)<model>(x, y)
let model2   = sgd(model, dm, 0.01)    // optimizers are functions over trees
```

- Differentiation is with respect to the `with` parameters by default: they
  are the model, exactly like `forward<m>`. A variant can target norm
  arguments.
- **Tangent types** are derived statically: `Tensor -> Tensor`,
  `float -> float`, `int`/`bool`/`string` have no tangent,
  structs/tuples/arrays map element-wise and drop non-differentiable members.
  `grad(loss)` therefore type-checks at compile time.
- `apply_gradients` was removed; a training step is `value_and_grad` + `sgd`.

## 4. The Transformation

### 4.1 Pullback form

For a function graph `g(inputs) -> y`, the transform produces, per graph and
memoized:

```text
D[g](inputs) -> (y, pb)      pb: (dy) -> tangents of the differentiable inputs
```

`pb` is an ordinary Camel closure (a `FILL FunctionClosure` over a synthesized
backward graph) that captures the primal values its rules need. This is the
"pullback" formulation used by Swift and the "backpropagator" formulation of
Pearlmutter and Siskind. It needs no runtime tape object: the chain of captured
closures *is* the tape, and it is managed by the GC like any other value.

### 4.2 Per node kind

| Node | Primal side | Backward side |
|---|---|---|
| `DATA`, `PORT` | unchanged | constants have no tangent; ports return their accumulated tangent |
| `OPER` | unchanged | the operator's rule (§5) |
| `CAST`, `COPY`, `GATE` | unchanged | identity on the tangent |
| `ACCS` | unchanged | the tangent goes into the accessed field of a zero tangent of the source |
| `FILL` | unchanged | the tangent of each filled slot is read back out |
| `FUNC` to `h` | call `D[h]`, get `(y, pb_h)` | call `pb_h(dy)` |
| `CALL` of a value `f` | see §4.4 | call the returned pullback |
| `BRCH`/`JOIN` | each arm yields `(y_i, pb_i)`; `JOIN` joins both | call the joined pullback (§4.3) |
| `SYNC`, control edges | kept as they are | the backward is pure |

Recursion needs no special handling: `D[run]` calls `D[run]`, and the pullback
of one step captures the pullback of the next. When the recursion depth is
static, the passes after `std::macro` (devirtualize + inline + fold) unroll it
into straight-line code, which is what translation passes want.

### 4.3 Branches

Arms are inline regions of their graph. The transform treats each arm as a
nested region: its primal part yields `(value, pb_arm)`, where `pb_arm` maps the
arm's output tangent to tangents of the outer values the arm reads. `JOIN`
merges the values and the pullbacks, which share one function type. The
backward of the enclosing region calls the joined pullback and accumulates what
it returns. The arm that was taken is recorded by the closure itself, so the
backward needs no second branch on the condition. Data-dependent conditions
work the same as constant ones; when the condition is static, pruning and
inlining remove the closures.

### 4.4 Calls through function values

- **Static callee.** If the callee is a static value, it is devirtualized before
  the transform: macro-time values, closures with static captures, and values
  folded by `std::opt::fold`.
- **Dynamic callee with a known function type.** Function values that flow
  into differentiated code must be differentiable. The transform wraps them:
  a closure parameter `f: (x: Tensor) => Tensor` of a differentiated function
  gets the derivative type `(x) => (Tensor, pullback)`, and callers pass
  `D[f_value]`, which `std::macro` builds when `f_value` is static at the call
  site.
- Otherwise the call is rejected with a diagnostic naming the call site.

### 4.4a Variables

A variable (`var`) is a cell: the graph passes the cell and reads its content with
the operator `:var/get` (design principles 2.5). **Gradients do not flow through
variables.** This is a rule of the library, not of the language:

- A `var` parameter is not a differentiable input; `grad` and `value_and_grad`
  take no gradient with respect to it. The gradient function keeps it a `var`
  parameter, so the function's writes reach the caller's variable.
- A read of a variable of a differentiable type is a **constant of the call**,
  and it is accepted only where that is certain: in the differentiated function
  itself (not in a function or branch arm it calls), for a variable that comes
  in from outside (a `var` parameter or a captured variable), and only if every
  operation of that function that assigns the variable a value depending on a
  differentiated parameter, or that receives the variable and may write it (a
  call, a branch, a closure), is ordered after the read, that is, depends on it.
  `let a = acc * 1.0; acc = a * 0.5 + x` is accepted: the read feeds the
  assignment.
- Everything else is refused with a diagnostic that says so (a read after such
  an assignment or call, a read in a callee or an arm, a variable of a
  differentiable type declared inside the differentiated function). Taking those
  reads as constants would return a wrong gradient without any sign of it.
- Variables of types without a tangent (an integer counter, a flag) are not
  affected.

Not implemented: following a value through an assignment and a later read of the
same variable (`acc = m.w * x; ... sum(acc)`). Use `let` for values a gradient
has to pass through. Tests: `test/cases/modules/autodiff/var_tensor_*.cml`.

### 4.5 Macro interplay

`grad(f)` evaluates when `f` is static, after the macros inside `f` have run
(the macro fixpoint processes them first). The macro pass must accept an
operator whose argument is a function value. Today `nn:apply_gradients` is
hard-coded as an exception; this becomes a property of the operator type (a
macro operator over function values).

## 5. Derivative Rules

Precedence, most specific first:

1. **Function rules from `@vjp<rule>`.** Attached to a user function at macro
   time. The contract is `rule(inputs..., dy) => tangents` (a tuple when there
   are several differentiable inputs). When the transform meets a call to a
   function that has a rule, it uses the rule instead of differentiating the body.
2. **Operator rules registered by packages.** Keyed by operator URI, for
   operators defined outside `tensor`/`nn`.
3. **Builtin operator rules.** `OpDef::vjp`, already in place, written against
   the abstract `VjpBuilder`.

Operators without a rule are an error on differentiated paths. Structurally
non-differentiable operators (`shape`, `zeros`, comparisons, integer arithmetic)
stop gradients silently. The builder interface and the rule registry move from
the nn module to core, beside `OperatorTraitsRegistry`, because the transform
is a core pass and must not depend on nn.

Correctness is checked mechanically: every rule gets a finite-difference test,
and the benchmark models are compared parameter by parameter with PyTorch
autograd.

## 6. Parameters and Modules

Goal: PyTorch-like ergonomics (a few objects manage many parameters) without
mutable parameter objects.

- **A model is a tree.** Structs, tuples, and arrays with tensor leaves; any size.
- **Layers are Camel code in a library:** a struct type plus functions, e.g.
  `type Linear = { w, b }`, `func linear(l: Linear, x: Tensor)`,
  `init_linear(in, out)`.
- **`with <m: Model>` is the implicit `self`.** `forward<m>(x)` threads the
  whole tree without naming parameters.
- **Gradients have the same shape as the model** (tangent types, §3), so
  optimizers are generic tree functions: `sgd(tree, grads, lr)`, and
  `adam(tree, grads, state, ...)` with a state tree of the same shape. They are
  implemented once over `Struct`/`Tuple`/`Array` values with tensor leaves.
- **Freezing** uses `stop_gradient(t)`, or differentiation restricted to a
  subtree.
- **`Parameter` objects** were removed; models are plain trees.

## 7. ONNX as a Thin Translation Pass

The current exporter performs partial evaluation itself (folding, inlining,
unrolling, branch selection, shape facts). These move to general passes:

- **Shape specialization:** bind the example input types (`Tensor<float32,
  [64, 784]>`, or with a dynamic batch dimension) to the function's ports and
  re-run operator inference, so shapes live in node types.
- **Static simplification:** `std::devirtualize` / `std::specialize` /
  `std::inline` / `std::opt::fold` / branch pruning / `dce` until a fixpoint.
- **`onnx::export` (a module-contributed translation pass):** lowers the
  resulting graph node by node, `BRCH`/`JOIN` to `If`, reading shapes and
  dtypes from node types. It reports anything left over (calls, unsupported
  operators) instead of evaluating it.

`onnx.export_model(fn, x, path)` stays as a convenience that runs this pipeline
on `fn`.

### Status

What exists (`modules/onnx/passes/export.cpp`, `docs/cli.md`):

    CAMEL_ONNX_OUT=model.onnx camel m.cml std::macro std::opt::simplify onnx::export

- `onnx::export` is registered by the `onnx` module and is the last pass of the
  pipeline; it consumes the graph, so the program does not run. The export
  request is the program's own `onnx.export_model(fn, example, path, ...)`
  call, which the pass finds in the entry graph with constant arguments (no new
  syntax, no new command-line option). `CAMEL_ONNX_OUT` overrides the path.
- It refuses, with the diagnostic `RuntimeDiag::TranslationRefused` at the node
  and the name of the missing pass, a graph that still has a call (not inlined,
  or the entry graph still calling `main`) or an unexpanded macro operator. It
  never expands macros, devirtualizes, specializes calls, inlines or unrolls.
- For the programs both entries accept, the models are equivalent: same
  checker result and bit-identical ONNX Runtime outputs
  (`test/cases/modules/onnx/pass_export.py`).

What differs from the design above:

- **Shape specialization is not a general pass.** There is no pass that takes
  example input types, so the pass does that step itself: it binds the example
  types to the function's ports, re-infers node types, and runs the
  `std::opt::fold` / `std::opt::dce` fixpoint on its copy of the function
  (shape arithmetic such as `shape(x)[0] * shape(x)[1]` or `numel` in a
  gradient can only fold then). This is the one simplification step left
  inside the pass.
- **Shapes are not read from node types.** The lowering walks the graph as a
  value DAG and re-derives dtype and shape per value with operator inference
  (this is what carries dynamic axes); in that walk it also evaluates pure
  operators whose arguments are all constants and takes the arm of a branch on
  a constant condition, where folding left them (inside branch arms).
- **`onnx.export_model` does not run the pass pipeline.** It simplifies the
  bound function itself with settings `std::opt::simplify` does not have:
  inlining regardless of size, up to 4096 rounds (deeper static recursion),
  and the branched-call split that turns the pullback of a data-dependent
  branch into per-arm calls (`enableBranchedCallSplit`, valid only for a
  consumer that builds no execution order). Programs that need these export
  through `export_model` and are refused by the pipeline: of the exporting
  programs in `test/cases/modules/onnx`, the pipeline accepts nine and refuses
  the four `export_train_step_branch*` (indirect call left) and the two
  `export_gated_gru_*` (call left); `export_import_order` is refused because
  its captured weights come from `random` and only exist at run time.
- **Control dependencies are checked, not translated.** The lowering walk
  follows data dependencies only. Before it, both entries refuse a function in
  which an impure operator is reachable from the exit through data or control
  edges, in the function itself or in a function it calls or holds as a
  constant (`Evaluator::refuseEffects`, `modules/onnx/exporter.cpp`): an effect
  on the control path of a `sync` function (a `println` whose result nothing
  reads) is reported at the operator and no model is written. The check is
  structural: an effect in the arm of a branch on a constant condition that
  folding did not remove is refused too.
- **Variables.** ONNX has no cells. A variable that is declared, assigned and
  read directly in one function (`var acc = x * 2.0; acc = acc + 1.0; return
  acc`) is only a name for a sequence of values: each read is translated as the
  value of the assignment that reaches it, which is decided from the graph (the
  assignments that must run before the read form a chain; the last one is
  read; the declaration's initial value when there is none). No state appears
  in the model. Refused with a diagnostic: a variable read or assigned outside
  the function that declares it (a `var` parameter, a captured variable, a
  variable used in a branch arm) — its content would be state the model cannot
  hold; a variable passed to a call or a closure; an assignment that the graph
  does not order with a read. Tests: `export_var_local`,
  `pass_export_refusal_var_*`.

## 8. Phases

1. Move the VJP builder and registry to core; add tangent types; add
   `grad` / `value_and_grad` as macro operators (typing, macro eligibility).
2. The transform for straight-line graphs and direct calls (memoized per
   graph, recursion through pullback closures); validate that the static cases
   optimize to straight-line code.
3. Branch regions (§4.3); calls through function values (§4.4).
4. Rule coverage (`layer_norm`, `gelu`, `permute`, `where`, `mean`, `slice`,
   `concat`, `batch_norm`, ...), the finite-difference harness, and `@vjp`
   function rules.
5. Tree optimizers, layer library, `stop_gradient`, and `apply_gradients`
   re-expressed on `grad`.
6. Shape specialization and simplification passes; `onnx::export` reduced to
   translation.
7. End-to-end: train MLP, LeNet, GRU, and Transformer; compare gradients with
   PyTorch and benchmark training steps.
