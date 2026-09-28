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

Tensors stay contiguous and row-major. Views (strided tensors) are out of scope;
`transpose`/`permute` materialize.

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

Core gains a small, module-agnostic `OperatorTraits` registry keyed by URI:
purity (pure / effectful / stateful) and whether the operator is elementwise.
Modules register traits for their operators at load time.

Generic passes use only traits, never module knowledge:

- `std::opt::fold`: evaluates pure operators whose inputs are all static, and
  materializes the result as a static slot.
- `std::opt::cse`: merges identical pure operator nodes.
- `std::opt::dce`: removes pure nodes with no consumers.

### 3.5 Module-contributed passes

Tensor-specific rewrites (for example `matmul + add (+ relu)` -> `linear`)
need tensor knowledge, so they belong in the tensor module, not in core. Core
gains a context-scoped pass registry that modules populate on load
(`tensor::fuse`). `findPassFactory` consults it after the static `std` scope.

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
- `BRCH`/`JOIN` with a constant condition select their arm, using the VMs'
  selection rule. A condition that depends on the model input is rejected:
  lowered values are tensors while Camel conditions are scalars, so such a
  condition needs a model-dependent scalar (an element read), which has no
  lowering. Mapping those branches to ONNX `If` is future work.
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
  opset. That per-operator capability table is what the paper's ORT_007
  exemplar needs.

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
