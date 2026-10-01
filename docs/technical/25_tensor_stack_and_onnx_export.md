# Tensor Stack, Graph Optimization, and ONNX Export

This document is the design record for turning Camel's `tensor`/`nn` modules
into a stack that can run standard small models (MLP, LeNet-class CNN, GRU,
Transformer block), optimize them at the graph level, export them to ONNX, and
be benchmarked against PyTorch and TensorFlow.

## 1. Experiment Shape

The evaluation compares the same models under these configurations:

| Configuration | What it measures |
|---|---|
| Camel native (`std::nvm`, `std::fvm`, `std::jit`) | Camel's own runtime with its own kernels |
| Camel -> ONNX -> ONNX Runtime | Camel as a graph front end on a shared kernel backend |
| PyTorch eager / `torch.compile` | Reference eager and compiled front ends |
| PyTorch -> ONNX -> ONNX Runtime | Tracing-based export on the same backend |
| TensorFlow eager / `tf.function` (+XLA) | Second reference framework |
| TensorFlow -> ONNX -> ONNX Runtime | Second tracing-based export on the same backend |

Holding the backend fixed (ONNX Runtime) isolates front-end costs: graph
capture, optimization, and export. The native configuration measures how far
Camel's own kernels are from a production BLAS-backed stack.

## 2. Primary Structural Contradiction

A tensor operator today is scattered across four unrelated places that must be
kept consistent by hand:

1. `getTensorOperatorGroups()` declares type resolvers. Most operators appear
   twice (symbol group `__add__` and named group `add`) with copy-pasted lambdas.
2. `getTensorOpsMap()` binds the URI suffix to the kernel entry point.
3. `nn::VjpRegistry` binds the URI to a backward rule, in another module.
4. Nothing records how an operator maps to an export target or whether it is
   pure, elementwise, or fusible.

The kernels themselves are written against a type-erased element API
(`getAsDouble` / `setFromDouble`) that re-dispatches on dtype for every element,
and the static `TensorType` is always `Dynamic(Float32)`: dtype is dropped and
shape is never inferred.

Adding operators, optimizations, or an exporter on top of this would multiply
the scattering. The redesign therefore starts by giving every tensor operator
one definition.

## 3. Target Architecture

```text
            +-----------------------------------------------+
  modules/  |  onnx module: onnx.export_model(fn, x, path)  |
            |  partial evaluator + lowering table (by URI)  |
            |  + protobuf writer                            |
            +-----------------------+-----------------------+
                                    | reads OpDef.infer / traits,
                                    | runs OpDef.kernel on constants
            +-----------------------v-----------------------+
            |  OpDef registry (tensor module, nn adds defs) |
            |  signature | infer | kernel | vjp | traits    |
            +-----+-----------------+----------------+------+
                  | generates       | registers      | registers
     OperatorGroups + executor map  | VJP rules      | core OperatorTraits
                  |                 |                |
            +-----v-----------------v--+   +---------v---------------------+
            |  kernel layer            |   |  core: OperatorTraits registry |
            |  typed loops, broadcast, |   |  generic passes: const-fold,   |
            |  parallel_for, GEMM      |   |  CSE, DCE; module pass registry|
            |  (builtin | CBLAS)       |   +--------------------------------+
            +--------------------------+
```

### 3.1 Kernel layer (`modules/tensor/kernels/`)

- Dtype dispatch happens once per operator call and selects a typed inner loop
  (`float`, `double`, `int32_t`, `int64_t`, `bool`). No per-element dispatch.
- Elementwise binary operators follow NumPy broadcasting. A shared broadcast
  iterator computes output shape and input strides once.
- `parallel_for(range, grain, fn)` is the only threading primitive kernels use.
  It is backed by one process-wide Taskflow executor (Taskflow is already a
  build dependency).
- GEMM is a single interface, `gemm<T>(transA, transB, M, N, K, alpha, A, lda,
  B, ldb, beta, C, ldc)`, with two backends:
  - `builtin`: cache-blocked, packed, compiler-vectorized, parallel over row
    blocks. Always available, no external dependency.
  - `cblas`: forwards to `cblas_sgemm` / `cblas_dgemm`. Enabled by the CMake
    option `CAMEL_TENSOR_BLAS` when `find_package(BLAS)` succeeds.
- Convolution is im2col + GEMM. Pooling, normalization, and reductions are
  typed loops over contiguous NCHW buffers.

Tensors stay contiguous and row-major. `reshape` (and `flatten`, `unsqueeze`,
`squeeze` through it) returns a view: a tensor with its own shape that reads
another tensor's buffer and keeps that owner alive as its one GC reference.
Tensors are never written after they are built, so the sharing is
unobservable, and a view is still contiguous, so kernels need not know about
it. Strided views are out of scope; `transpose`/`permute`/`slice` materialize.

### 3.2 Operator definitions (`modules/tensor/ops/`)

Every operator is one `OpDef` value:

```cpp
struct OpDef {
    std::string_view name;                  // "matmul"; also the URI suffix
    std::vector<std::string_view> aliases;  // extra export names, e.g. "__mat__"
    OpSignature signature;                  // parameter kinds and names
    InferFn infer;                          // static result type (dtype + shape)
    operator_t kernel;                      // runtime entry point
    nn::BuiltinVjpRule vjp = nullptr;       // optional backward rule
    OpTraits traits;                        // pure, elementwise, fusion class
};
```

Everything else is derived from the table, so no operator is declared twice:

- `OperatorGroup`s (one resolver built from `signature` + `infer`, exported under
  the name and every alias),
- the executor's URI -> kernel map,
- VJP registrations (the `nn` module consumes the table instead of maintaining
  its own URI list),
- core `OperatorTraits` registrations.

Backend lowerings are deliberately not a field of `OpDef`: each backend owns a
table keyed by operator URI (section 3.6), so operators stay independent of
export targets and a backend's table doubles as its capability registry.

`nn` operators (`conv2d`, `embedding`, pooling, normalization) are defined as
`OpDef`s in the `nn` module and registered into the same registry.

### 3.3 Static tensor types

`TensorType` carries a dtype and an optional shape. A shape is a list of
dimensions, where each dimension is either a known extent or unknown (`?`).
An unknown rank is represented by an absent shape.

- Every `OpDef::infer` computes the result dtype (with promotion) and propagates
  shape where inputs are known: broadcasting, matmul contraction, reductions,
  convolution and pooling arithmetic.
- A shape conflict that is certain at compile time is a semantic diagnostic
  (`TensorShapeMismatch`, carrying the operator and both shapes). Unknown
  dimensions never produce errors; they fall back to runtime checks.
- Runtime shape errors use `RuntimeDiag::TensorDimensionMismatch` instead of
  generic `RuntimeError` strings.

### 3.4 Operator traits and generic graph passes (core)

`camel::core::OperatorTraitsRegistry` (`include/camel/core/operator_traits.h`)
maps operator URIs to traits: `pure` (no side effects, no hidden state) and
`elementwise`. The builtin table registers its pure operators (`op/*` except the
in-place assignment family, and the non-mutating string/array helpers); the
tensor `OpRegistry` mirrors every `OpDef`'s traits. Unregistered operators are
treated as effectful.

Generic passes (`src/passes/opt/generic/`) use only traits, never module
knowledge:

- `std::opt::fold` evaluates pure operators whose inputs are all static DATA
  nodes through the regular kernels and materializes the result as a static
  value. Kernel failures are left for runtime to report.
- `std::opt::cse` merges pure operator nodes with the same URI, type, and inputs,
  after merging equal scalar constants (value numbering), but only within the
  same branch-arm region and never onto a node downstream of the replaced one.
- `std::opt::dce` removes value-only nodes (pure operators, static data,
  CAST/COPY/ACCS/FILL) that have no value users.

Ordering rule shared by all three: in `sync` code every call sits on a control
chain, and a pure node on that chain still orders its dependents after the
effects before it. When a node is replaced, *all* of its users (value and
control) therefore inherit its control predecessors; nodes that anchor the graph
(exit, output, return, entry, branch-arm heads and tails) are never rewritten.
A sweep over every test case with `fold cse dce` enabled matches the unoptimized
output (the only differences are three unseeded random nn cases that differ
between plain runs too).

### 3.5 Module-contributed passes

`registerModulePass(path, factory)` (`include/camel/execute/pass/base.h`) lets a
loaded module add a scoped pass; `findPassFactory` resolves it after the static
`std` scope, and the "pass not found" listing includes module passes. The tensor
module contributes `tensor::fuse`:

- It rewrites `x @ w + b` and `relu(x @ w + b)` into `matmul_add` /
  `matmul_add_relu`, one pattern at a time (a fused add can be the bias of the
  next pattern, as in GRU gates).
- Intermediates must be unobserved outside the pattern; their control
  dependencies move to the fused node, so patterns inside `sync` code fuse too.
- The fused kernel preloads a row bias as the GEMM accumulator and applies relu
  in place; for other bias shapes and dtypes it runs the unfused computation, so
  the rewrite needs no static shape information.
- The nn module has VJP rules for both fused operators (and for `relu`), so
  fused training graphs differentiate like the patterns they replace; five nn
  training tests run a second time under `tensor::fuse`.
- ONNX lowers the fused operators to MatMul/Add(/Relu).

### 3.6 ONNX export (`modules/onnx/`)

Export is a runtime operator, not a tracer:

```camel
import { export_model } from onnx
export_model((x: Tensor): Tensor => forward<model>(x), example_x, 'model.onnx')
```

(`export` is a Camel keyword, hence `export_model`.) The first argument is a
function value: a runtime graph (`GCGraph`) plus its closure. The exporter
partially evaluates that graph, demand-driven from its return value, with the
parameter bound to a symbolic tensor carrying `example_x`'s dtype and shape.
Every node evaluates to either a constant or a symbolic tensor:

- Nodes whose inputs are all constants run concretely through the same kernels
  the VMs use (`ExecutorManager::find(uri)`), so closure-captured weights,
  hyper-parameters, `shape(x)` arithmetic, struct field access, and helper
  calls fold away. Constant tensors that reach the ONNX graph become
  initializers named after the struct field they came from.
- Operators with a symbolic input are lowered through the backend's lowering
  table (`lowering.h`/`lowering.cpp`, keyed by URI and minimum opset). Result
  dtype and shape come from the operator's `OpDef::infer` with constant
  arguments supplied, so the emitted graph carries full shape information.
  Operand dtypes are converted to the inferred result dtype, which reproduces
  Camel's promotion rules (ONNX requires equal input types).
- `FUNC` and `CALL` nodes are inlined. Recursion driven by constants (for
  example the GRU time-step loop) unrolls; a call-depth bound turns recursion
  that depends on the model input into a diagnostic.
- Symbolic values have a form: a tensor, a Camel scalar (rank-0 ONNX
  tensor), or an `int[]` (1-D int64 tensor, e.g. `shape(x)` with a dynamic
  batch). The lowering table also covers builtin scalar arithmetic,
  comparisons and conversions (`:op/add_l`, `:op/gt_d`, `:op/ltod`, ...),
  `math:sqrt`, array indexing, element reads (`t[i]`), and full reductions,
  so conditions and shape arithmetic that depend on the input can be
  exported.
- `BRCH`/`JOIN` with a constant condition select their arm, using the VMs'
  selection rule. An if-then-else on a condition computed from the input
  becomes an ONNX `If`; each arm is emitted into its own subgraph (values first
  computed inside an arm stay in that arm). Matches on input-dependent values
  are rejected.
- `export_model(fn, x, path, dynamic_axes)` leaves input axes dynamic (axis 0
  is named `batch`). `shape(x)` then becomes `Shape`, but its statically known
  entries are tracked, so `shape(x)[3]` still folds and only the dynamic
  extent flows through `Gather`/`Concat`; `zeros(shape)` becomes
  `ConstantOfShape` and `reshape` takes the computed shape. All four benchmark
  models export with a dynamic batch and match the reference at batch sizes
  64/32/8 and 5.
- The emitter merges structurally identical nodes (same operator, inputs and
  attributes) per scope; every emitted operator is deterministic, so this is
  plain CSE (it removes the per-step `Shape` of the unrolled GRU).
- Impure operators (`OpTraits::pure == false`, e.g. `randn`) are rejected even
  with constant arguments. Operators without a lowering are rejected with their
  URI and the opset. Values that depend on the input may not be stored in
  tuples, structs, arrays, or closures.
- Only data dependencies are followed; control-only (`SYNC`) ordering has no
  ONNX counterpart.
- Constants produced during export are rooted (`mm::RootHandle`) until the
  model is written, because an allocation failure may run a non-moving
  collection.
- The model is written with a minimal protobuf wire-format writer
  (`proto/onnx_writer.*`, ONNX `ModelProto`, IR 8, opset 17). No protobuf
  library dependency, so Windows builds are unaffected.
- `LoweringRegistry::supported(opset)` lists what the backend can express at an
  opset, and `onnx.supported_operators()` exposes it to Camel programs. That
  per-operator capability table is what the paper's ORT_007 exemplar needs.

Export covers inference graphs. Training steps produced by `apply_gradients` are
not exported.

### 3.7 Tensor I/O

`tensor.load_npy(path)` and `tensor.save_npy(t, path)` read and write NumPy
`.npy` files. Benchmarks use them to share identical weights and inputs across
Camel, PyTorch, and TensorFlow, and tests use them to compare exported-model
outputs against Camel's own execution.

## 4. Operator Coverage

Driven by the four benchmark models:

| Group | Operators |
|---|---|
| Elementwise | add, subtract, multiply, divide, pow, neg, abs, sqrt, rsqrt, exp, log, relu, gelu, sigmoid, tanh, where, cast |
| Reduction | sum, mean, max, argmax (all with `axis` and `keepdims`) |
| Linear algebra | matmul (batched, broadcasting), linear (matmul + bias) |
| Shape | reshape, flatten, transpose/permute, concat, slice, squeeze/unsqueeze |
| NN | softmax, log_softmax (any axis), conv2d (stride, padding), max_pool2d, avg_pool2d, layer_norm, batch_norm (inference), dropout (identity at inference), embedding |
| Recurrent | expressed in Camel from the primitives above (GRU cell as a function), not as a fused op |

## 5. Benchmark Harness (`benchmarks/`)

- One directory per model with `model.cml`, `model_torch.py`, `model_tf.py`.
- `benchmarks/common/weights.py` generates deterministic weights and inputs as
  `.npy`; all three implementations load the same files.
- `benchmarks/run.py` runs every configuration from section 1 with fixed thread
  count, warmup, and repetition settings; it checks numerical agreement first,
  then records compile/export time, warmup time, steady-state latency,
  throughput, and peak RSS to CSV.
- The harness never contributes kernels or model logic; it only drives the
  implementations and records results.

## 6. Landed: Foundation (step 1)

Implemented in `modules/tensor/`:

| Path | Responsibility |
|---|---|
| `dtype.h/.cpp` | storage dtypes (float32, int64, bool), promotion, `dispatchDType` |
| `tensor.h/.cpp` | `TensorObject`, `ShapeError` |
| `interop.h/.cpp` | scalar/array/shape/DLPack conversions |
| `type.h/.cpp` | interned `TensorType` with optional dtype and partially known shape |
| `kernels/` | `parallelFor`, broadcasting elementwise, reductions/normalization, layout, GEMM |
| `ops/` | `OpDef`, `OpRegistry`, and the operator catalog (one file per family) |

Decisions recorded here because they change semantics outside the module:

- **Composite flag invariant (core).** `registerOtherType` rejects the
  `Composite` flag. Code that sees `Composite` casts to `CompositeType`, which an
  Other type never is; Tensor and Parameter previously carried the flag and only
  worked because vtable slots happened to line up. `Type::assignableFrom` now
  dispatches Other types before composite types.
- **`Type::unify` (core).** Branch joins use the least common type instead of
  requiring equality. The default keeps the old behavior (equal or error);
  `TensorType` widens disagreeing facts to unknown.
- **`Type::widened` (core).** A `var` binding takes the widened type of its
  initializer, and `:op/assn` accepts any value assignable to the target. For
  tensors the widened type is plain `Tensor`, so a variable initialized with a
  `[2, 1]` tensor can later hold any tensor.
- **Runtime shape errors** use `RuntimeDiag::TensorDimensionMismatch`
  ("Tensor shape mismatch in '<op>': <detail>") instead of generic runtime errors.
- **GEMM dispatch.** The builtin GEMM is compiled twice (portable, AVX2+FMA) and
  selected by a runtime CPU check, so portable builds still use wide vectors.
  `CAMEL_TENSOR_BLAS=ON` forwards to a system CBLAS instead.
- **Operator semantics generalized** (all within the previous behavior for the
  old supported cases): arithmetic and comparison broadcast fully; matmul
  supports batched and vector operands; reductions take any axis and optional
  `keepdims` and keep the input dtype; softmax takes any axis; transpose swaps
  the last two axes of any tensor of rank >= 2.

Measured on the development container (4 cores): 512x512 float32 matmul went
from about 1 GFLOP/s (per-element dtype dispatch) to 52 GFLOP/s with the
portable kernel and 160 GFLOP/s with the AVX2+FMA kernel.

Not yet done in step 1: compile-time shape conflicts are reported as overload
rejections (`NoMatchingFunction` listing the refined argument types) rather than
as a dedicated semantic diagnostic, because `FuncTypeResolver` cannot carry a
rejection reason. That resolver extension is part of step 2.

### Step 2 progress: spatial operators and `.npy` I/O

- `kernels/conv.*`: conv2d as im2col + GEMM with stride and padding, its input,
  kernel, and bias gradients (col2im), max/average pooling with gradients, and
  inference batch normalization. Verified against PyTorch through committed
  fixtures (`test/cases/modules/nn/fixtures/spatial`, regenerated by
  `generate.py`).
- The nn module's tensor computations (conv2d family, pooling, batch_norm,
  embedding, softmax cross-entropy) are now OpDefs registered under the `nn`
  protocol; only the Parameter/training operators (`parameter`, `value`,
  `grad`, `sgd`, `apply_gradients`, `vjp`, ...) keep hand-written groups,
  because they operate on Parameter objects and graphs, not tensors. The
  conv2d VJP forwards stride and padding; pooling has VJP rules.
- `tensor.load_npy` / `tensor.save_npy` read and write NumPy files.

### Step 4 landed: ONNX exporter

- `modules/onnx`: `value.h` (constant / symbolic values), `emitter.*` (graph
  construction, initializer deduplication, dtype casts), `lowering.*` (49
  tensor and nn lowerings, opset-aware), `exporter.*` (partial evaluator),
  `module.*` (`onnx.export_model`).
- All four benchmark models export; the models pass `onnx.checker` (full check)
  and match the NumPy float64 reference under ONNX Runtime (max abs error
  7e-7 MLP, 5e-7 LeNet, 4e-7 GRU, 1.7e-6 Transformer). The GRU's 16 steps
  unroll to 399 nodes; the zero initial state makes the first step's hidden
  matmuls fold at export time.
- Benchmark integration: with `CAMEL_BENCH_EXPORT=<path>`, `camel_bench`'s
  `run_benchmark` exports instead of timing, so each model has one source for
  both the native and the `camel_onnx_ort` configurations.
- Fixes found on the way:
  - `isOfSameCls` compared vtable pointers. Modules are loaded with
    `RTLD_LOCAL`, and classes without a key function (`String`) get a private
    vtable copy per module, so `get_env(...) == ''` was false. It now falls
    back to comparing dynamic types.
  - Struct, tuple, and array assignability is covariant in the element types
    (they are immutable), and array literals take the unified element type.
    Precise tensor types previously made `{ w: Tensor<float32> }` unusable
    where `{ w: Tensor }` was declared.

### Step 3 landed: graph optimization and kernel quality

- Traits registry, module pass registry, `std::opt::{fold,cse,dce}`, and
  `tensor::fuse` as described in sections 3.4 and 3.5.
- Per-operator profiling of the native Transformer showed that the dominant
  cost was not the VMs but the unary kernels: a per-element `switch` on the
  operator and scalar libm calls kept every elementwise loop scalar (gelu on
  8x64x512 took 1.9 ms, a third of the forward pass). Unary operators now
  dispatch once to operator-specialized loops, and exp/tanh/sigmoid/gelu use
  branch-free float implementations (`kernels/vmath.h`, exp within 1 ulp,
  IEEE-like saturation) that vectorize; softmax computes exp once per element.
- Effect on the 4-core container (medians, ms):

  | Model | before | kernels | kernels + `tensor::fuse` |
  |---|---|---|---|
  | GRU | 4.75 | 2.6-2.8 | 2.5-2.6 |
  | Transformer | 5.87 | 2.2 | 1.97 |

  The container is shared (load average ~2.5 on 4 cores), so sub-millisecond
  results vary by up to 1.7x between runs; paper numbers need a quiet machine
  and repeated trials.

## 7. Delivery Order

1. Kernel layer + `OpDef` registry + static tensor types (the foundation; all
   later work depends on it).
2. Operator coverage and `.npy` I/O.
3. Core traits registry, generic passes, module pass registry, `tensor::fuse`.
4. ONNX exporter.
5. Benchmark harness and results.

Steps 2-4 can proceed in parallel once step 1 has landed. The benchmark
harness's PyTorch/TensorFlow side has no dependency on Camel and can proceed
from the start.
