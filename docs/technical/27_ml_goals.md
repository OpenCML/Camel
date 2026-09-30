# Machine Learning in Camel: Goals and Work Plan

Status: living document, updated as items land. Design background:
[25_tensor_stack_and_onnx_export.md](25_tensor_stack_and_onnx_export.md) (tensor stack, ONNX export,
benchmarks) and [26_autodiff_as_graph_rewrite.md](26_autodiff_as_graph_rewrite.md) (autodiff).

## 1. Goal

Camel supports AI workloads end to end, with graph rewriting as the one mechanism:

1. **Train** standard models (MLP, LeNet-class CNN, GRU, Transformer block) with static automatic
   differentiation. Gradients are verified against PyTorch, and training-step speed is competitive
   with PyTorch eager.
2. **Optimize** programs with generic, module-agnostic passes. After macros, a static program
   (including its gradient) reduces to straight-line tensor code.
3. **Export** to ONNX with a thin translation pass that only lowers an already-simplified graph.
4. **Evaluate** all of this against PyTorch and TensorFlow for the FSE 2027 paper, with results that
   can be reproduced.

Principles that constrain every item below:

- Differentiation is not part of the core. `grad` is an ordinary package operator, and the core
  places no restriction on what packages (including C++ packages) define. The core only hosts
  neutral derivative metadata (rules per operator, tangent spaces per type).
- Branches and recursion are handled statically. No runtime tape.
- Translation passes stay thin. Folding, inlining, unrolling and branch selection belong to generic
  passes.
- Prefer long-term, thorough refactors to compatibility shims. Remove technical debt instead of
  wrapping it.

## 2. Where We Are

Everything listed here is on `fsh-dev` and covered by tests. Numbers are medians of interleaved
trials on a 4-thread cloud VM (`benchmarks/results/train.json`). The ratio is PyTorch eager time
divided by Camel time (above 1 means Camel is faster).

| Area | State |
|---|---|
| Tensor stack | Typed kernels, a single operator registry (OpDef), refined static tensor types, `tensor::fuse`, compile-time shape errors |
| Autodiff | `grad`, `value_and_grad`, `vjp`, `stop_gradient` and `@vjp` custom rules, as a pullback transform with branches, recursion and activity analysis |
| Models | Plain trees. The optimizers are `sgd` and `adam` (with `OptimizerState`), plus `save_params`. `Parameter` and `apply_gradients` were removed |
| Verification | Finite-difference checks for every rule. The MLP, GRU and Transformer gradients match PyTorch within 1e-6 relative error (`benchmarks/train.py --check`) |
| Simplification | `std::opt` + `std::opt::fold` + `std::opt::dce` reduce the MLP and Transformer `grad(loss)` graphs to straight-line code (no calls, closures or tuples) |
| Training speed | Best Camel configuration vs PyTorch eager: MLP 1.12×, GRU 1.10×, Transformer 1.02× |
| Tests | `npm run test`: 226/228. The two known failures are `monte_carlo_pi` (timeout) and `mnist_py` (dataset download) |

## 3. Work Items

Each item has an acceptance criterion, and an item is done only when that criterion holds on
`fsh-dev`. IDs match the session task list.

### A. Thin ONNX export (#12) — next

The exporter still does partial evaluation itself: folding, inlining, unrolling, branch selection
and shape facts. That work moves into generic passes.

- A.1 **Shape specialization pass.** Bind example input types (for example `Tensor<float32, [64,
  784]>`, optionally with a dynamic batch dimension) to a function's ports and re-run operator
  inference, so shapes live in node types.
- A.2 **Simplification to a fixpoint** with the existing generic passes (`std::opt`, fold, cse,
  dce). Add branch pruning on static conditions if it is still missing.
- A.3 **`onnx::export` as translation only.** It lowers the simplified graph node by node,
  `BRCH`/`JOIN` to `If`, and reads shapes and dtypes from node types. It reports anything left over
  (calls, unsupported operators) instead of evaluating it. Delete the exporter's own evaluator.
- A.4 **`onnx.export_model(fn, x, path)`** stays as the convenience entry point that runs this
  pipeline.
- A.5 **Export a training step.** With gradients collapsed, the ONNX graph of
  `value_and_grad(loss)` for the MLP and Transformer should export and agree with Camel.

Accept: all existing ONNX tests pass, and the exporter has no folding, inlining or evaluation code.
The four benchmark models export and match Camel's outputs under ONNX Runtime. A.5 matches
Camel's gradients.

### B. Training performance

- B.1 **FastVM liveness (#25).** FastVM keeps every intermediate alive until its frame returns
  (MLP training heap: 75 MB vs NodeVM's 27 MB), so its kernels run 15–60% slower from the larger
  working set. Release registers after their last use in the bytecode compiler.
  Accept: FastVM's peak memory is within 1.5× of NodeVM's, and its training step is no slower than
  NodeVM's.
- B.2 **FastVM/JIT compile graphs created at run time (#19).** Graphs synthesized by macros or
  rewrites must go through the same compilation as user code.
  Accept: every autodiff test passes under `std::fvm` and `std::jit` with the simplification
  pipeline.
- B.3 **JIT tail-call trampoline bug (#20).** The trampoline reads from an empty frame. Blocks JIT
  for training.
  Accept: the autodiff and nn plans pass under `std::jit`.
- B.4 **Zero-copy reshape.** `reshape` copies today (about 3% of a Transformer step). Tensors need
  shared storage (views) owned by the GC.
  Accept: `reshape` of a contiguous tensor allocates no data, and the GC tests pass.
- B.5 **GEMM efficiency.** GEMM dominates the remaining kernel time. Measure against oneDNN/MKL
  (PyTorch's backend) per shape, and tune the built-in AVX2/AVX-512 kernels or add an optional
  BLAS backend (`CAMEL_TENSOR_BLAS` exists).

### C. Simplification quality

- C.1 **Algebraic simplification.** Autodiff emits `1 * x`, `x + 0` and zero tangents. Add identity
  rules to `std::opt::fold` as operator traits, so they stay module-agnostic.
- C.2 **Recursion.** Recursive helpers are excluded from the size-independent inlining criteria,
  because inlining them exposed inliner bugs. With a static depth, recursion (GRU over a fixed
  sequence length) should unroll. Fix the inliner for that structure, then allow unrolling when the
  depth is static.
  Accept: the GRU `grad(loss)` graph collapses like the MLP's, and the tests still pass.
- C.3 **Pass planning.** The pass planner drops repeated passes, so a pipeline cannot say
  `std::inline std::opt::fold std::inline`. Either allow repetition or give `std::opt` a documented
  fixpoint over fold/dce too.

### D. Language and compiler

- D.1 **Parser bugs (#24).** Postfix access after a with-call (`f<m>(x).0`), and annotated struct
  `let x: S = {...}`.
- D.2 **Known test failures.** Give `monte_carlo_pi` a timeout that fits the VM, or make the case
  cheaper. Make `mnist_py` skip cleanly without network access.

### E. Paper (#10)

- E.1 Rerun the paper benchmark suite (`benchmarks/run.py`) on a quiet machine. The earlier
  `paper_v1` results were taken under contention and must not be used.
- E.2 Add training results: the gradient check table and training-step timings (`train.py --time`),
  including the simplified pipeline.
- E.3 Update tables, figures and the capability list (`benchmarks/capabilities.py`) from the new
  runs.
- E.4 Document the methodology: steady-state timing (40 warmup steps), interleaved trials and
  bootstrap CIs, thread counts, and machine description.

Accept: every number in the paper can be regenerated from a script in `benchmarks/` and a results
file in `benchmarks/results/`.

### F. Documentation

- F.1 Keep [26_autodiff_as_graph_rewrite.md](26_autodiff_as_graph_rewrite.md) in sync. It should
  describe the collapse pipeline (lambda lifting, projection folding, inlining criteria) and phase 6
  once A lands.
- F.2 Add the shape specialization pass and the thin exporter to
  [25_tensor_stack_and_onnx_export.md](25_tensor_stack_and_onnx_export.md).

## 4. Order

1. A (thin ONNX), because it completes the architecture the paper argues for.
2. B.1–B.3 (FastVM/JIT), so that every execution backend trains.
3. E (paper runs), once A and B.1 have landed, on a quiet machine.
4. C, B.4, B.5, D as capacity allows. C.2 before the paper if GRU results depend on it.

## 5. Working Rules

- Work happens on `fsh-dev`. Push after each verified milestone.
- Before each push:
  - `npm run build` passes;
  - the affected plans pass;
  - `benchmarks/train.py --check` passes when autodiff, kernels or passes changed.
- Merging into `develop`, and any release to `main`, follows `agents/branching.md` and needs the
  user's confirmation.
