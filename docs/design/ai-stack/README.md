# Camel AI Stack: Architecture and Delivery Roadmap

Status: design proposal; no implementation or normative semantic changes.
Date: 2026-10-15. Inspected branch: `wzj-dev`, HEAD `1ba545b4`, including the
working-tree snapshot. Other work is in progress, so this is not a release audit.

## 1. Objective and evidence standard

Deliver a coherent path from model definition and data to differentiation,
accelerated training, recovery, and deployment. The first complete execution
target is a single NVIDIA GPU. Multi-device training and serving are later
products built on the same contracts, not prerequisites for the first milestone.

Distinguish four statuses throughout:

- **Implemented, read-code evidence**: source exists and was inspected; not a
  claim that the feature was built or passed tests in this design session.
- **Prototype**: partial implementation with important boundary limitations.
- **Proposed**: architecture or API to implement.
- **Not established**: no complete capability was verified in this inspection;
  this is not a claim that no relevant code exists anywhere in the repository.

The primary structural gap is between existing semantic graphs and persistent,
typed device execution. A gradient graph plus a GPU call is not yet a device-
resident training system. Solve that boundary before expanding the model catalog.

The normative [design principles](../../design-principles.cn.md) prevail.
The [indexed computation proposal](../indexed-computation/README.md) supplies
the long-term compiler architecture; this roadmap narrows delivery scope and
assigns ownership. Neither document changes source semantics by itself.

## 2. Baseline inventory

| Component | Evidence and existing capability | Important gap or qualification |
|---|---|---|
| Language/GIR | Functions, closures, static typing, graph passes and CPU execution | Structured `LOOP` is decided in the principles but absent from the inspected runtime node enum; generic graph work remains shared infrastructure |
| `tensor` | CPU kernels, broadcasting, shape inference, reductions, matmul, normalization, NPY I/O | Storage normalization is f32/i64/bool; complete mixed precision, general device storage and strided device interchange are not established |
| `autodiff` | `grad`, `with_grad`, custom `vjp` registration, `stop_gradient`, graph pullback engine | General JVP/Jacobian/Hessian interfaces, automatic batching, and a complete checkpoint/recompute product are not established; custom VJP registration is not a general public VJP evaluator |
| `nn` | NN operations; parameter-tree SGD, Adam, optimizer state, `save_params` | Composable graph-visible optimizer transformations and complete training-state recovery need work |
| `tiles` | Working-tree C++ module with `source`, `run`, `available`, `device`; shape-specialized CUDA generation and lazy driver/NVRTC loading | Prototype, not validated here. Current execution allocates buffers per invocation, uploads host snapshots, synchronizes, returns host floats, and frees invocation resources |
| Graph optimization | Generic simplification and fixed tensor fusion patterns | Broad device fusion, cost-driven scheduling, buffer reuse, structured specialization and autotuning remain work |
| `onnx` | Export implementations and test plans; external runtime route | Export coverage is bounded; export is not a native serving runtime or full training distribution layer |
| `python` / interchange | Embedded Python; CPU-copy DLPack helpers in tensor | GPU zero-copy ownership and stream/event protocol need implementation; arbitrary Python calls remain opaque to Camel graph transformations |
| Examples/tests | Linear training example; Transformer training benchmark; tensor, NN, autodiff, ONNX plans; new tiles sample cases | Existence of plans/examples is not proof of current pass status or numerical/performance parity |
| Data/distributed/serving | File/random/general-purpose libraries and ecosystem access exist | Complete native training data pipeline, distributed stack and request-serving engine were not established |

Baseline evidence:

- [Tensor type](../../../modules/tensor/type.h), [dtype](../../../modules/tensor/dtype.cpp), [interop](../../../modules/tensor/interop.cpp).
- [Autodiff interfaces](../../../modules/autodiff/operators.cpp), [pullback contract](../../../modules/autodiff/pullback.h).
- [Optimizer contract](../../../modules/nn/optim.h), [optimizer implementation](../../../modules/nn/optim.cpp).
- [Tiles module](../../../modules/tiles/src/module.cpp), [CUDA execution](../../../modules/tiles/src/cuda.cpp), [program representation](../../../modules/tiles/include/tiles/program.h).
- [Runtime node kinds](../../../include/camel/runtime/graph.h), [pass API](../../../include/camel/execute/pass/base.h).
- [Transformer training example](../../../benchmarks/finetune/native_train_transformer.cml).

## 3. Target architecture

```text
Models / applications / finetuning packages
             |
       training orchestration -------- data pipeline
             |                            |
   nn + optimizer transformations + checkpoint
             |
 tensor values + autodiff + function transformations
             |
     typed semantic computation graph
             |
 graph optimization / specialization / placement
             |
 tiles: indexed kernels / schedules / device lowering
             |
 shared storage and execution runtime ---- external library kernels
             |
       NVIDIA CUDA (first target)

Side paths: ONNX/export -> external deployment; Python/format interchange.
Cross-cutting: numerical contracts, RNG, diagnostics, profiling, correctness CI.
Later: distributed placement/communication and serving-specific scheduling.
```

This is a responsibility diagram, not an instruction to make every box a C++
module or a mandatory runtime layer. High-level composition should be written in
Camel where practical; C++ owns primitives, system interfaces and compiler/runtime
mechanisms. A compiled program need not retain orchestration dependencies it does
not use.

## 4. Module ownership

Names marked proposed are design boundaries, not newly registered modules.

| Owner | Responsibility | Explicit boundary | Packaging direction |
|---|---|---|---|
| Core + compiler | Graph/schema/type/effect contracts, loops, source origins, pass infrastructure | No model catalog or CUDA-specific semantics in the language core | Existing core |
| `tensor` | Logical tensor values, mathematical ops, dtype/shape semantics, views and storage-facing tensor API | Does not own all hardware schedules | Existing standard C++ module plus Camel compositions |
| `autodiff` | Differentiation transforms, derivative rules, active-value analysis, recompute contracts | Does not own device driver or training loop | Existing module; extend |
| Function-transform interfaces | Automatic batching, specialization, graph transformation composition | `map` is not silently redefined as `vmap`; backend JIT is not the existing CPU `std::jit` by another name | Shared graph facilities, APIs grouped by owning module; no new public module required yet |
| `nn` | Layers, losses, initialization, parameter/state trees, train/eval behavior | No duplicate Tensor or independent autograd engine | Existing module; prefer graph-visible Camel functions |
| `nn::optim` (proposed namespace) | Composable updates, parameter groups, schedules, clipping and optimizer state | Keep existing SGD/Adam usable; avoid a premature module split | Organize inside `nn`; separate `optim` only when justified |
| `tiles` | Indexed kernel expression, kernel schedules, source/code generation, target capability diagnostics | Not the owner of data loading, distributed training or serving | Bundled C++ module, optional CUDA capabilities; `tiles::cuda` for target-specific APIs |
| Shared storage/execution runtime | Allocation, devices, events, leases, buffer reuse and external storage | One ownership model shared by tensor, tiles, interchange and later collectives | Internal runtime layer first, not necessarily a new public module |
| `data` (proposed) | Iteration, sampling, batching, streaming, prefetch and resumable cursor | Reuse parsers/tokenizers and external datasets rather than reproduce all formats | Camel library with C++/external adapters |
| `train` (proposed) | Train-step orchestration, accumulation, evaluation, precision policy, metrics and recovery | Does not hide model math or duplicate the compiler | Mostly Camel library plus launcher/runtime services |
| `checkpoint` (proposed) | Versioned model/optimizer/RNG/data state persistence and recovery | Weight files alone are not a full checkpoint | Library + format/I/O primitives |
| `distributed` (proposed) | Groups, mesh/shards, collectives, placement and communication contracts | Reuse communication libraries; not part of tiles kernel syntax | Later C++ primitives and graph passes |
| Model packages | Transformer/CNN/etc. compositions, weight mapping, adapters and generation | No hardcoded requirement that every model is built into the language | Packages, examples and compatibility adapters |
| `onnx` / export | Graph exchange and deployment artifacts | Exported execution is a distinct measured route from native tiles | Existing module, extend by supported contracts |
| Serving integration | Request batching, KV state, cancellation, latency metrics | Request scheduling differs from kernel scheduling | External integration first; optional later product |
| Tools | Traces, memory reports, compile diagnostics, numerical regression | Do not require a proprietary experiment platform | Existing CLI tools + interoperable output formats |

Tensor operations keep logical meaning. Physical device placement and synchronization
are implemented through the shared runtime; `tiles` must not create a second
incompatible tensor/ownership universe. Host-copy execution may remain an explicit
convenience entry point, but it must not be the training ABI.

## 5. Capability benchmarks and build-versus-integrate choices

| Benchmark family | Capability to learn from | Camel strategy |
|---|---|---|
| NumPy / PyTorch tensors / JAX arrays | Array semantics, numeric coverage, device-aware values | Own semantics/runtime; reuse selected backend kernels |
| PyTorch autograd / JAX transformations | Derivatives, batching and composable transformations | Own graph transforms and conformance tests |
| torch.nn / Flax | Parameter/state organization and model composition | Functional model/state interfaces; no requirement to copy class inheritance |
| torch.optim / Optax | Update rules, composable gradient transformations and state | Extend graph-visible `nn::optim` |
| TorchInductor / XLA | Graph compilation, guards, caches, memory planning | Own Camel pipeline; reuse downstream infrastructure where useful |
| Triton | Device kernel expression, scheduling and tuning | `tiles`; first target NVIDIA, no claim of present parity |
| DataLoader / HF Datasets | Sampling, streaming, preprocessing and batching | Native iteration contract plus adapters |
| Accelerate / Lightning / Trainer | Training orchestration, mixed precision and launch strategy | Lean `train` library; explicit execution configuration |
| FSDP / DeepSpeed / distributed JAX | Sharding and communication-aware execution | Later placement/collective layer; reuse NCCL for NVIDIA |
| Transformers / PEFT | Model assets, architecture configs and parameter-efficient finetuning | Small validated model set, compatible assets, model packages |
| Safetensors / DLPack / ONNX | Weights / live storage / computation exchange respectively | Implement separate contracts; none substitutes for the other two |
| vLLM / TensorRT-LLM | Serving, KV memory, batching and optimized inference | Integrate first; native serving deferred |
| Profiler / TensorBoard-style tools | Device timelines, allocations, metrics and reproducibility | Produce inspectable traces and standard metric streams |

The comparison is capability-oriented. These projects are not all pure Python
implementations; Python often fronts substantial native compiler/runtime code.
Do not equate the presence of similarly named functions with feature parity.

## 6. Cross-module contracts to settle first

1. **Tensor/storage:** dtype, shape, strides/offset, logical aliasing, ownership,
   device placement, host access and asynchronous readiness. GC owns wrappers;
   pending device work pins the necessary external storage until completion.
2. **Operator capability:** type/shape inference, effects, derivative/batching
   rules, semantic decomposition and backend lowering. Unsupported cases carry
   source-located diagnostics; fallback must be explicit.
3. **Training state:** `(parameters, model_state, optimizer_state, RNG, step)`
   are distinguishable values. Shared/tied parameters have explicit logical paths
   and update rules; incidental buffer aliasing cannot silently define tying.
4. **Numerics:** storage/compute/accumulator dtype, conversion, reduction order,
   permitted fusion and reproducibility scope. Mixed precision is a policy, not
   a hidden global rewrite. BF16 need not use the same scaling policy as FP16.
5. **Transforms:** state requirements and supported compositions. Existing `with`,
   `macro`, `wait`, `sync`, ordered reduce and mutable-gradient rules do not change.
6. **Randomness:** explicit state or counter-based logical indexing; checkpoint
   replay and activation recomputation must reproduce the specified draws.
7. **Artifacts:** independent versions for semantic model, weights, optimizer state,
   compiled code/cache and checkpoint metadata. Changing weights must not always
   force recompilation; deliberately folded constants must affect cache validity.
8. **Observation:** source -> graph -> generated kernel mapping; separate compile,
   transfer, execution and synchronization costs. GPU benchmarks synchronize at
   defined boundaries; host enqueue time is not device execution time.

These are proposed implementation contracts. Any required new semantic ruling
must be recorded with the language author rather than inferred from a benchmark.

## 7. Priority and dependency plan

Priority labels: **P0** blocks a correct single-GPU training product; **P1** makes
it practically useful; **P2** develops performance and scale; **P3** expands the
ecosystem and deployment product. Correctness and diagnostics apply at every level.
Stages are dependency gates, not promised calendar dates.

```text
S0 contracts + reference validation
  -> S1 GPU execution/runtime foundation
  -> S2 device-resident FP32 training
  -> S3 practical training + recovery + mixed precision
  -> S4 graph-native performance and transformations
  -> S5 distributed training

S2/S3 -> S6 ecosystem/deployment integration (can advance independently of S5)
Profiling, test infrastructure and interoperability evolve throughout.
```

### S0 / P0: establish the trusted baseline

Owners: core, tensor, autodiff, tiles, tools.

Deliver a versioned capability matrix, operator/shape/numerical contracts, a
documented supported graph subset, CPU references, and an NVIDIA test environment
record. Validate the current tiles prototype instead of assuming it works from
source presence. Preserve its useful source-inspection and missing-device paths.

Exit gate: manually run vector addition, row softmax and matmul on boundary sizes
and inspect output/diagnostics, then add appropriate plan assertions. Exercise
empty/tail dimensions, invalid shapes, unsupported operators and no-CUDA startup.
Record actual device, driver, compiler and numerical policy. No speed target yet.

### S1 / P0: device execution foundation

Owners: shared runtime, tensor, tiles.

Implement device-resident storage, events/leases, explicit host transfers, reusable
allocation, multi-result graphs, and compiled-plan caching. Add a supported path
for CPU/device interchange and device-located failure reporting. Audit the current
per-call host snapshot and allocation model against the target runtime boundary.

Exit gate: compose two GPU computations without intermediate device-to-host
copies. Repeated execution reuses compiled code when metadata and code are
unchanged. Modified weights are observed without stale constant capture. Failure
and cancellation cannot free in-flight buffers. Allocation telemetry stabilizes
after warmup in a 1,000-iteration fixed-workload stress case, within a documented
pool bound; cached/reserved memory is reported separately from live memory.

### S2 / P0: complete FP32 single-GPU training

Owners: autodiff, nn, tiles, compiler.

Compile forward, backward and parameter update with parameters and optimizer
state remaining device-resident. Make SGD and Adam decomposable or provide exact
device lowerings; the existing C++ update kernels alone are not enough. Start with
a linear model/MLP, then one documented small Transformer block. Support the
control-flow subset required by these workloads; do not wait for every feature.

Exit gate: fixed-seed multi-step runs match reference loss, gradients and parameter
updates within per-fixture declared tolerances. Check gradients on small inputs
with finite differences where meaningful. A transfer trace shows only deliberate
batch upload, logging and checkpoint download, not whole-state copies each step.
Unsupported branches/types fail explicitly. Tiny-case correctness and representative
GPU timing are separate fixtures; no claim to outperform a mature stack yet.

### S3 / P1: useful and recoverable training

Owners: tensor, nn::optim, data, train, checkpoint.

Add BF16/FP16 storage with explicit accumulator rules, selective precision,
overflow handling/scaling where needed, AdamW, clipping, gradient accumulation,
parameter groups and schedules. Add bounded-memory prefetch, batch preparation,
resumable sampling and pinned/asynchronous transfer where supported.

Define a complete versioned checkpoint: parameter/model state, optimizer state,
step/schedule, RNG, sampler position, precision/scaler state and configuration
fingerprints. Implement optimizer-state serialization rather than treating its
current opaque value as automatically serializable. Save with a committed manifest
or equivalent atomic publication; detect partial/incompatible artifacts.

Exit gate: interruption/resume after step k reproduces the continuation of a fixed
N-step reference run under the declared determinism policy. For a deterministic
same-target fixture compare exact state; otherwise document the permitted numerical
comparison while still requiring exact step/RNG/data-cursor restoration. Exercise
prefetch cancellation, injected overflow, accumulation boundaries, FP32-sensitive
ops and checkpoint write failure. Report throughput and convergence separately.

### S4 / P2: demonstrate graph-native optimization value

Owners: compiler, tiles, autodiff, tensor, nn::optim, tools.

Implement profitable fusion, liveness-driven buffer reuse, bounded shape variants,
structured kernel schedules, layouts and async pipelines. Extend automatic batching
and derivative composition, add explicit rematerialization/checkpoint policies,
and make optimizer transformations graph-visible. Keep the chosen fast path
inspectable and allow external library kernels where they are better.

Exit gate: compare the same workload, device, precision, inputs and allowed
numerical policy against a documented PyTorch eager and compiled baseline.
Report cold compile, warm step latency/throughput, peak live/reserved memory,
kernel count and transferred bytes. Include ablations that disable individual
optimizations. Require a repeatable benefit on at least one selected workload,
without hiding significant regressions in the declared supported workload set;
freeze explicit regression budgets before running the comparison. Test dynamic
shape guard failure, bounded recompilation, stochastic replay and tail cases.

### S5 / P2: scale to multiple GPUs

Owners: distributed, compiler/runtime, train, checkpoint.

Begin with data parallelism and matching collectives, then communication overlap
and state sharding. Add mesh/placement descriptions, global RNG/data partitioning,
distributed checkpoint publication and failure diagnostics. Tensor/pipeline/expert
parallelism follow demonstrated workloads, not an immediate requirement to support
every strategy.

Exit gate: a two-GPU run matches the corresponding effective-batch single-device
reference within its stated numerical policy. Verify collective matching, gradient
scaling, shard reconstruction, tied parameters, restart and rank failure detection.
Report communication volume, peak per-rank memory and scaling efficiency. A driver
or rank failure must not appear as successful partial training or hang indefinitely.

### S6 / P3: ecosystem adoption and deployment

Owners: model packages, interchange, data, export, integration tools.

Support selected external weight/config assets, safetensors, tokenizer adapters
and a validated LoRA path. Establish device DLPack lifetime/synchronization tests.
Extend ONNX/native compiled artifacts and connect an external serving route before
building a native request scheduler. Quantization needs format, calibration or
weight conversion, kernel and accuracy contracts; it is not just another dtype.

Exit gate: reproduce a named small pretrained model's outputs, perform a short
finetune, save/reload it, and deploy through a documented route. Verify weights,
tokenization, model configuration, adapter merging and output equivalence separately.
For a serving target additionally measure first-token/steady-token latency,
throughput, cancellation and KV lifetime. Success on this gate does not require
all models from an external model hub to work.

## 8. Initial backlog and critical path

| Work item | Priority / stage | Dependency | Completion evidence |
|---|---|---|---|
| Audit/validate tiles prototype | P0 / S0 | Build/toolchain access | Manual runs, supported-op matrix, executable plans |
| Storage/event/lease ABI | P0 / S1 | Tensor/effect contracts | Lifetime tests and no intermediate host copy |
| Multi-output device graphs | P0 / S1 | Storage ABI | Loss + gradient/tree results without repeated execution |
| Device AD/update coverage | P0 / S2 | Device ops and decompositions | Multi-step weight-by-weight reference comparison |
| Runtime profiling | P0 onward | Execution hooks | Transfer/allocation/kernel trace tied to source |
| Mixed precision | P1 / S3 | Dtype and numerical contracts | Overflow and convergence tests |
| Full checkpoint/recovery | P1 / S3 | Serializable state/cursors | Interrupted versus uninterrupted continuation |
| Data prefetch/interop | P1 / S3 | Events and data contracts | Bounded pipeline and overlap trace |
| Automatic batching / optimizer composition | P1-P2 / S3-S4 | Rule interfaces and typed parameter trees | Transform composition/conformance cases |
| Fusion/layout/pipeline optimization | P2 / S4 | Resource IR and legality checks | Correctness, generated-code inspection, ablations |
| Distributed primitives/placement | P2 / S5 | Stable storage and state | Two-GPU correctness and failure tests |
| Model assets and deployment adapters | P3 / S6 | S2/S3 and format contracts | Selected model round trip |

Higher-order differentiation and broad model families are valuable extensions,
but are not blockers for the first training product. Basic serialization, profiling
and numerical tests start early even though their richer products have later gates.

## 9. Execution, testing and compatibility policy

- Follow the repository build/test commands. New cases are run manually before
  automated verification is added. This design session ran no builds or AI tests.
- Do not infer readiness from source names, sample files, documentation, or an
  exported ONNX model that happens to run on an external CUDA backend.
- Keep CPU correctness checks available without CUDA. GPU CI records supported
  architectures; missing hardware is a visible skip, never a passing GPU result.
- Do not change source semantics to improve a benchmark. Faults, masks, mutation,
  numerical freedom and transform preconditions remain explicit contracts.
- Avoid permanent duplicate runtimes/optimizer representations. Evolve toward
  one storage and state model; any necessary migration must have a removal plan.
- Reuse drivers, NCCL, file formats and compatible math libraries. Keep graph
  semantics, derivative/batching rules and scheduling knowledge under Camel control.
- Native implementations, external-library kernels and ONNX deployment must be
  labeled separately in performance reports. Kernel speed is not serving speed.
- Do not fork the user-facing API into incompatible CPU and GPU tensor languages.
  Target-specific expert interfaces are allowed under `tiles::cuda`.

## 10. Deferred scope and risks

Deferred: a universal model zoo, full Python framework emulation, a new experiment
tracking platform, all-GPU-vendor support, a native large-scale serving engine,
and exhaustive higher-order differentiation coverage.

Main risks and responses:

| Risk | Response |
|---|---|
| Many modules but no usable training loop | Gate progress on end-to-end S2/S3 evidence |
| CPU copies hide behind a successful GPU run | Require transfer traces and persistent-state tests |
| Optimizers or custom ops remain opaque | Provide semantic decompositions or explicit device rules |
| Numerical policy changes with backend | Version contracts and maintain cross-backend fixtures |
| Cache explosion for dynamic shapes | Variant budgets, guards, cache metrics and explicit general paths |
| Existing graph infrastructure lags the design | Implement only needed shared foundations first; preserve intended final interfaces |
| Implementation scope exceeds team capacity | One NVIDIA target, one small training workload, external ecosystem adapters |

The near-term product claim should be narrow and testable: **a graph-native
single-GPU training stack with inspectable compilation and reliable recovery**.
Broader parity claims follow measured coverage, not the size of this roadmap.

## 11. Benchmark references

Primary documentation consulted in the accompanying discussion, 2026-10-15:

- [JAX transformations](https://docs.jax.dev/en/latest/key-concepts.html)
- [Optax](https://optax.readthedocs.io/en/latest/getting_started.html)
- [Flax](https://flax.readthedocs.io/en/latest/)
- [PyTorch compiler](https://docs.pytorch.org/docs/stable/torch.compiler.html)
- [PyTorch AMP](https://docs.pytorch.org/docs/stable/notes/amp_examples.html)
- [PyTorch fully_shard](https://docs.pytorch.org/docs/stable/distributed.fsdp.fully_shard.html)
- [Triton](https://triton-lang.org/main/python-api/triton.language.html)
- [HF Accelerate](https://huggingface.co/docs/accelerate/index)
- [HF Datasets](https://huggingface.co/docs/datasets/index)
- [Transformers](https://huggingface.co/docs/transformers/index)
- [PEFT](https://huggingface.co/docs/peft/index)
- [Safetensors](https://huggingface.co/docs/safetensors/index)
- [vLLM](https://docs.vllm.ai/en/latest/)

Capability grouping and implementation priorities above are Camel design choices,
not requirements imposed by those projects.
