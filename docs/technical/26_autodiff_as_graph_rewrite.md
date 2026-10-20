# Automatic Differentiation as Graph Rewriting

> **Read [`../design-principles.cn.md`](../design-principles.cn.md) first** (English translation: [`design-principles.en.md`](../design-principles.en.md)). It is the language author's normative statement of what Camel is and of its execution model; it prevails over code and other documents, and questions it leaves undecided go to the language author.
>
> In its terms (section 4): automatic differentiation is a **library built on the pass mechanism, not part of the language core**. Its precondition is that `std::macro` runs before `grad` / `with_grad` / `vjp` are used: a graph on which macros have not been executed lacks the information differentiation needs. When the precondition is not met the library must give a clear diagnostic. Pass order is otherwise unrestricted by the language and may change results.
>
> **Implementation status**: an unmet precondition can still surface as a run-time error instead of a clear located diagnostic (e.g. "branch arms must be subgraph calls" after `std::opt` on the GRU benchmark; tracking item N3 / D21).

Status: implemented through phase 5 and phase 7 (§8); phase 6 (thin ONNX) is
partly done: `onnx::export` exists as a module-contributed translation pass,
but shape specialization still happens inside it and `onnx.export_model` still
runs its own simplification (§7, "Status"). Supersedes the tape-based synthesis inside `nn.apply_gradients`, which
has been removed together with `Parameter`.

Implementation map:

- Core: `include/camel/core/derivative.h` (VjpBuilder, rule registry, tangent
  spaces, tangent types of aggregates). Scalar rules live in
  `src/builtin/operators/derivative.cpp`.
- Engine: `modules/autodiff` (`grad`, `with_grad`, `vjp`,
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
- Generic simplification (`std::opt`) collapses pullback closures
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
  ── std::opt::devirtualize / std::opt::specialize / std::opt::inline
  ── std::opt::fold / cse / dce, tensor::fuse, ...
  ── execution pass (std::nvm, std::fvm)      -> runs the program
     or translation pass (onnx::export, ...)   -> emits a format
```

`grad(f)` is a macro operator: its argument is a static function value, and its
result (a new function value) is materialized as static data by `std::macro`.
The synthesized graph joins the reachable graph closure at commit time, so every
later pass sees it like user code.

To inspect the generated gradient before and after optimization, use
`test/cases/modules/autodiff/collapse_runtime.cml`, whose `main(x: float)` leaves
the evaluation point open. Dump with `std::macro std::gir`, then with
`std::macro std::opt std::gir`, **without `--arg`**: the former exposes `grad(h)`
and the pullback subgraphs, while the latter retains runtime arithmetic over the
input port. Bind `--arg x=2` when executing (the derivative is 22), or `x=3` (30).
Binding an argument before optimization may specialize and fold the entry graph.
The older `collapse.cml` has equal constant branch values and can legitimately
fold its caller's derivative calculation; it is not an open-input inspection case.

For interactive execution without argument binding, use `collapse_random.cml`.
Its `rand:rand` input is effectful and remains a runtime operator through both
macro evaluation and optimization. The fixed runtime seed makes backend results
repeatable; it does not make random draws compile-time constants. This example
keeps the derivative arithmetic visible even when all entry arguments are bound.

`std::opt::algebra` is a general scalar arithmetic pass, included after CSE in each
`std::opt` round; it has no knowledge of autodiff. In `collapse_random.cml` it turns
the shared `t = (x + 1) * 3; t + t` into `(x + 1) * 6` and the analogous unit-seed
path into `x * 2`, reducing main's derivative arithmetic from six nodes to four.
Default algebra permits local reassociation, so bitwise equality with the original
program is not promised. Use `'std::opt(safe_fp=true)'` or the standalone
`'std::opt::algebra(safe_fp=true)'` to restrict this stage to exact float rewrites;
the coefficient doubling above is exact and remains enabled in safe mode.

Matching inspects only adjacent scalar add/multiply nodes and direct constants;
it neither expands shared expression trees nor crosses GATEs. A product with
other consumers is retained. A deduplicated work queue has at most four times
the original node count in visits. Eliminated products transfer their immediate
CTRL predecessors, while consumer identities, origins and numeric permissions
remain intact, including branch-arm/output anchors. Integer coefficients use
unsigned arithmetic at their declared width to preserve wrapping.
Written slots are classified once through GATE alias groups, rather than walking
the same shared value paths separately for every arithmetic pattern.

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
  (`std::opt::inline`, `std::opt`, `std::opt`; `MacroOperandGraphs` in
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
let (l, dm2) = with_grad(loss)<model>(x, y)
let model2   = sgd(model, dm, 0.01)    // optimizers are functions over trees
```

- Differentiation is with respect to the **first `with` parameter** (9.9): weights
  are `with` parameters, exactly like `forward<m>`; the remaining `with`
  parameters (hyperparameters, targets) are constants, and ordinary parameters
  are data. There is no variant that targets ordinary parameters: to
  differentiate data, wrap it in a lambda that makes it the first `with`
  parameter. In a loop, bind the transform once and call it with the weights:
  `let loss_with_grad = with_grad(loss)`, then
  `let (l, g) = loss_with_grad<m>(x, y)`.
- **Tangent types** are derived statically: `Tensor -> Tensor`,
  `float -> float`, `int`/`bool`/`string` have no tangent,
  structs/tuples/arrays map element-wise and drop non-differentiable members.
  `grad(loss)` therefore type-checks at compile time.
- `apply_gradients` was removed; a training step is `with_grad` + `sgd`.

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

Implementation status (2026-10-06). The library handles the callee itself when
`grad` runs; no pass has to run first. One rule decides what is accepted:

> A call of a function value is differentiated when the function is **named, or
> built by a lambda, in the function being differentiated or in a function it
> calls**, and gets from there to the call only by being **bound, captured,
> passed as an argument, or returned**.

So besides a `let`-bound lambda called where it was built: a closure called
inside a second closure that captured it, a closure returned by a factory, a
closure or a named function passed to a function that calls it, a function
that takes a closure and returns one capturing it, a lambda returning a lambda,
and a closure built before a branch and called in its arms. Captured values
are inputs of the call like any other, so gradients flow into captures that
depend on the differentiated parameter.

How (`modules/autodiff/pullback.cpp`):

- *Following* (`ForwardRewrite::followFunctionValues`). Before a graph is
  differentiated, the calls in it that carry a function value are inlined into
  it: a call that is passed an evident function value (as an argument or a
  captured value) and a call that returns one. "Evident" means that the graph
  itself says which function the value is: a constant function, or a
  `FILL FunctionClosure` over one. Afterwards the call of the value is a call
  of a closure built in the graph. Inlining is `std::opt::inline`'s own
  (`camel::passes::opt::inlineCallInOrder`), so the order and the effects of the
  inlined code are kept by the same code as in the pass; a callee value behind
  the gates an inlined call puts on its arguments is seen through.
- *The call* (`rewriteIndirectCall`). A constant function without captures
  becomes a direct call; a closure is differentiated through the function's
  own graph with the captured values as trailing arguments
  (`Engine::planForClosure`, one derivative per function, shared by its call
  sites).
- *Branch arms* (`rewriteBranch`). An arm is a graph of its own and is not
  inlined. A closure built before the branch is handed to the arm as the values
  it captured, and the arm builds it again in its own graph.
- *Bounds.* Macros and functions with a derivative rule of their own are never
  followed into. A recursive function that passes a function value on to itself
  is followed into once: the arm that holds its recursive call knows which
  function it came from and leaves that call a call. One transformation follows
  at most 256 calls.

What is refused, and how. A function value that does not fit the rule is
refused at its call: one chosen by a branch at run time (also when the
condition is a constant expression: nothing folds it before `grad` runs), read
from a tuple, struct, array or variable, a parameter of the differentiated
function itself, or reached through the recursive call of a function that
passes it on. The diagnostic is located at the refused call, in the default run
and under `std::macro` (a runtime fault names its origin; under `std::macro`
the text adds where the macro call is); when several calls are refused, the
first in source order is reported; the message gives the one reason that holds
for that call. A direct call that is passed such a function value is refused
as well when the value depends on the differentiated parameter: the callee
may have no differentiable parameter at all, and the call would otherwise
count as a constant and drop that part of the gradient silently. For this a
function value that captured an active value is itself active in the activity
analysis (`Backward::run`), as a carrier that never holds a gradient. (The
wrapping of function-typed parameters described above is not implemented.)

`grad` of a function with effects runs the function: the gradient waits for
everything the function's own result waited for, so effects after the last
value the gradient reads still happen.

Arrays are not differentiable values today (`:op/idx_arr` and `map` have no
derivative rule), so a lambda passed to `map` is refused for that reason.
Tests: `grad_closure.*`, `grad_function_values*.*` (accepted structures,
against finite differences), `grad_function_value_*.*` (each refusal with its
position), `grad_effects_kept.*`.

### 4.4a Variables

A variable (`var`) is a cell: the graph passes the cell and reads its content with
the operator `:var/get` (design principles 3.5). **Gradients do not flow through
variables.** This is a rule of the library, not of the language:

- A `var` parameter is not a differentiable input; `grad` and `with_grad`
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
the nn module to core, beside the operator table (`OperatorTable`), because the transform
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
- **Freezing** uses `stop_gradient(t)`.
- **`Parameter` objects** were removed; models are plain trees.

## 7. ONNX as a Thin Translation Pass

The current exporter performs partial evaluation itself (folding, inlining,
unrolling, branch selection, shape facts). These move to general passes:

- **Shape specialization:** bind the example input types (`Tensor<float32,
  [64, 784]>`, or with a dynamic batch dimension) to the function's ports and
  re-run operator inference, so shapes live in node types.
- **Static simplification:** `std::opt::devirtualize` / `std::opt::specialize` /
  `std::opt::inline` / `std::opt::fold` / branch pruning / `dce` until a fixpoint.
- **`onnx::export` (a module-contributed translation pass):** lowers the
  resulting graph node by node, `BRCH`/`JOIN` to `If`, reading shapes and
  dtypes from node types. It reports anything left over (calls, unsupported
  operators) instead of evaluating it.

`onnx.export_model(fn, x, path)` stays as a convenience that runs this pipeline
on `fn`.

### Status

What exists (`modules/onnx/passes/export.cpp`, `docs/cli.md`):

    camel m.cml std::macro std::opt 'onnx::export(out="model.onnx")'

- `onnx::export` is registered by the `onnx` module and is the last pass of the
  pipeline; it consumes the graph, so the program does not run. The export
  request is the program's own `onnx.export_model(fn, example, path, ...)`
  call, which the pass finds in the entry graph with constant arguments (no new
  syntax, no new command-line option). the parameter `out` of the pass overrides the path.
- It refuses, with the diagnostic `RuntimeDiag::TranslationRefused` at the node
  and the name of the missing pass, a graph that still has a call (not inlined,
  or the entry graph still calling `main`) or an unexpanded macro operator. It
  never expands macros.
- For the programs both entries accept, the models are equivalent: same
  checker result and bit-identical ONNX Runtime outputs
  (`test/cases/modules/onnx/pass_export.py`).

What differs from the design above:

- **Shape specialization is not a general pass.** There is no pass that takes
  example input types, so the pass does that step itself: it binds the example
  types to the function's ports, re-infers node types, and runs the export
  simplification on its copy of the function (shape arithmetic such as
  `shape(x)[0] * shape(x)[1]` or `numel` in a gradient can only fold then, and
  a recursion bounded by a shape can only unroll then).
- **Shapes are not read from node types.** The lowering walks the graph as a
  value DAG and re-derives dtype and shape per value with operator inference
  (this is what carries dynamic axes); in that walk it also evaluates pure
  operators whose arguments are all constants and takes the arm of a branch on
  a constant condition, where folding left them (inside branch arms).
- **Both export entries run the same export simplification** on the bound
  function (`exportRewriteConfig`, `modules/onnx/exporter.cpp`), with settings
  `std::opt` does not have: inlining regardless of size, up to 4096
  rounds (deeper static recursion), and the branched-call split that turns the
  pullback of a data-dependent branch into per-arm calls
  (`enableBranchedCallSplit`, valid only for a consumer that builds no
  execution order, which is why it cannot be a pass before a scheduler).
  `onnx.export_model` also expands macros; the pass leaves that to `std::macro`.
  For twenty-six programs of `test/cases/modules/onnx` the two entries write
  models with bit-identical ONNX Runtime outputs (`pass_export.py equiv`),
  among them the four `export_train_step_branch*`, `export_gated_gru_forward`
  and `export_gated_gru_step` (the training step of the gated GRU, checked
  against the native run on all eight gate patterns by `ort_gated_gru_step.py`).
  `export_import_order` is refused by the pass because its captured weights
  come from `random` and only exist at run time.
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
  read; the declaration's initial value when there is none). A compound
  assignment (`+=`, `*=`, ...) writes the content it finds combined with its
  operand. No state appears in the model. Refused with a diagnostic: a variable read or assigned outside
  the function that declares it (a `var` parameter, a captured variable, a
  variable used in a branch arm) — its content would be state the model cannot
  hold; a variable passed to a call or a closure; an assignment that the graph
  does not order with a read. Tests: `export_var_local`,
  `pass_export_refusal_var_*`.
- **Tail recursion of dynamic depth is an ONNX Loop.** Simplification unrolls
  a recursion whose depth is known at export time. A call that is left is a
  recursion whose depth depends on the model input. When every recursive call
  is in tail position, the function is a loop over its arguments, and the
  exporter emits one `Loop` (`Evaluator::translateLoop`,
  `modules/onnx/exporter.cpp`):
  - the carried values are the arguments a recursive call changes (found by a
    first, discarded evaluation of the body); the others stay what they are
    outside the loop, so constants keep folding inside the body;
  - the body is the function's own graph evaluated once. A branch that
    separates "return" from "call again" becomes an `If` whose outputs are the
    continue flag and the next state: each side is computed only when it is
    taken. (Until 2026-10-06 both sides were computed and selected with
    `Where`, on the assumption that exported operators are total; a step taken
    past the end of the data is not — a slice outside its axis is empty and the
    reshape after it fails — and exported loops failed at run time for some
    lengths and inputs. Found by the pass-quality cycle; test `ort_time_axis`.)
    Branches without a recursive call remain `If`s as before;
  - the body always runs once (as a call does) and continues while the path
    taken was a recursive call; the result is the body evaluated once more
    after the `Loop`, at the final state, so no carried value needs an initial
    result;
  - a recursion that goes through helpers or branch arms is followed (the arm
    case is what the tests exercise; a loop inside a loop body would become a
    nested `Loop` and is not tested);
  - **several values.** The recursion may carry several values and return
    them: the result is a tensor, a scalar (float, int, bool), or a tuple or
    struct of these, nested to any depth. Each leaf of the result is a carried
    value, a value computed from the carried values where the recursion
    returns, or a constant; different returning paths may yield different
    values, of the same structure and leaf types. The result is merged leaf
    by leaf: a leaf that differs between two returning paths is an output of
    the `If` that separates them; next to a path that calls again, zeros of
    the leaf's type and rank stand in (they are never read); a constant needs
    no output. An integer counter stays an int64 scalar and is an int64
    output of the model, a bool a bool output. This is the usual shape of a
    data-dependent loop: `func spin(h: Tensor, k: int): (Tensor, int)`,
    stopped by a threshold on the state or a maximum count.
  Refused, each with a diagnostic at the recursive call or the export request:
  a carried value whose dtype or shape changes between calls; a tuple, struct
  or array argument that changes (the carried values themselves are the
  arguments: tensors and scalars, not a tuple passed whole); a leaf of the
  result that is neither a tensor nor a scalar: a function, or an int array
  built from values that depend on the model input (the diagnostic names the
  element of the result and what it is); returning paths whose results differ
  in structure or in a leaf's type; a `match` whose arms call the function
  again; a recursion that never returns. Tests: `export_loop`,
  `pass_export_loop`, `export_dynamic_recursion`, `ort_loop_values` (tuple,
  two returning paths, struct, three carried values, nested tuple with a
  carried float and a bool: NumPy and the native run for 0, 1, several and
  the maximum number of steps), `export_loop_values_array` (refusal); both
  entries write equivalent models (`pass_export.py equiv`).
- **Linear recursion** (the recursive call is not the last action). A function
  `f(a) = E(a, if c(a) then E'(a, f(step(a))) else B(a))` with ONE recursive
  call on one side of ONE two-way branch is the pair of loops it denotes:
  with `a_0` the first arguments, `a_{i+1} = step(a_i)` and `n` the first `i`
  where the branch takes the other side, `V_n` is the body at `a_n` and
  `V_i` the body at `a_i` given `V_{i+1}`. The exporter emits a forward `Loop`
  (the last arguments and `n`), the value of the last call, and a `Loop` of
  `n` iterations that applies the body in reverse order; the arguments of call
  `i` are recomputed from `a_0` by a counted inner `Loop`. There is no tape:
  the step runs `n + n(n-1)/2` times plus a constant (measured), which is the
  stated cost; a tape (scan outputs of the forward loop) is the follow-up.
  The identity is applied by symbolic evaluation of the function's own graph
  and does not depend on where the function comes from; hand-written
  recursions (a right fold) are tested as such.
  - Functions as results. The forward function autodiff derives for a tail
    recursion is such a linear recursion returning `(value, pullback)`, the
    pullback being a closure that captures values of its call and the pullback
    returned by the recursive call. A closure cannot be carried by a `Loop`:
    it is kept as an export-time value (function + captured values) and the
    recursion is unrolled where the returned function is CALLED:
    `g_i(x) = P(a_i, x, g_{i+1}(x))`, `g_n` the closure of the last call, i.e.
    one bottom call and a `Loop` applying `P` in reverse order. A branch whose
    arms yield such functions is evaluated on both sides and selected when the
    function is called.
  - Accepted, exactly: one recursive call in the function's own graph; one
    if / else on a scalar condition that depends on the model input with the
    call on exactly one side (branches inside that side which use the call's
    result on both of their sides are part of the post-processing); the
    arguments the call changes are tensors or scalars of fixed dtype and
    shape; the result's leaves that are not functions are tensors or scalars
    of fixed dtype and shape, and when the result also holds a function those
    leaves are passed through unchanged; a function in the result calls the
    function returned by the recursive call exactly once, with its own
    arguments unchanged, and returns tensors or scalars (or tuples of them);
    it is called with tensors, scalars, constants, or tuples and structs of
    these (the arguments are not carried by a `Loop`: every call of the chain
    receives them unchanged).
    Closures may capture any export-time value: constants, tensors and
    scalars that depend on the model input, tuples and structs of these, and
    other closures.
  - The gradient of a loop that returns several values. For a tail recursion
    that returns `(state, counter)` (or a struct, or more values) the derived
    forward function returns `((state, counter), pullback)` and the pullback
    takes one cotangent per differentiable element of the result, as a tuple
    or struct; the counter has no tangent and is not differentiated. This is
    inside the class above, so `with_grad` of a loss computed from the
    returned values exports as paired `Loop`s, with the counter usable as a
    number in the loss and as an output of the model. What is accepted for
    the gradient is NARROWER than for the forward loop: ONE if / else must
    decide the recursion (write a maximum count into the condition,
    `if k >= limit || done(h) then (h, k) else spin(step(h), k + 1)`; a
    second, nested branch that also ends the loop exports forward and is
    refused in the gradient as "several branches decide"). The cost is that
    of the route: no tape, the step is recomputed, quadratic in the number of
    iterations.
  - Refused, each located: two or more recursive calls; several branches that
    decide the recursion; a `match` that decides it; a condition that uses the
    call's result; a result or state whose dtype or shape changes; a function
    returned together with a value the recursion changes on the way back; a
    returned function that calls the one below it more than once or with
    changed arguments, or that is called with a function or with an array
    whose elements depend on the model input. The conditions on the returned
    function are reported at the recursive call (they used to be reported at
    the `with_grad` expression, where the pullback is called).
  Tests: `ort_linear_recursion` (NumPy, depths 0, 1 and more), `ort_grad_loop`
  (gradient of a data-dependent loop, with a branch in the body, against
  Camel's native gradient and NumPy), `ort_grad_loop_values` (gradient of
  loops returning a tuple, a struct and four values, the counter in the loss
  and as an output: NumPy reverse sweep and the native run for 0, 1, several
  and the maximum number of steps), `ort_loop_values_dynamic` (the same loop
  and its gradient with a dynamic batch axis), `export_nontail_recursion`
  (refusal).
- **Constant tuples, structs and arrays in a branch arm.** Simplification
  folds a tuple, struct or array literal of constants only where replacing it
  is execution-safe; one inside a branch arm reaches the export unfolded (the
  zero gradient `{ w: zeros }` on the path where a loop takes no step; `[k, k
  + 1]` in the first, specialised call of a recursion, where `k` is 0). The
  exporter keeps such a tuple or struct as the export-time aggregate of its
  elements and such an array as the 1-D tensor it stands for, as it does when
  some element depends on the model input. (It used to refuse them with "a
  tuple, struct or array of constants was not folded".)

## 8. Phases

1. Move the VJP builder and registry to core; add tangent types; add
   `grad` / `with_grad` as macro operators (typing, macro eligibility).
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
