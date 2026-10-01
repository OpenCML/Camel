# Automatic Differentiation as Graph Rewriting

Status: implemented through phase 5 and phase 7 (§8); phase 6 (thin ONNX) is
open. Supersedes the tape-based synthesis inside `nn.apply_gradients`, which
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
