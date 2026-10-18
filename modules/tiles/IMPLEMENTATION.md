# Tiles implementation map

The public API, supported operations and limitations are documented in
[README.md](README.md). The language-level route is defined in
[the Tiles design](../../docs/design/tiles-route/README.cn.md); numerical rules
follow [the design principles](../../docs/design-principles.cn.md).

## Compilation and execution

- `src/compiler.cpp` reads Camel function graphs and lexical permissions. It uses
  core operator declarations and validated `core.instance` facts, with isolated
  call specialization when concrete input shapes are required. Compiled programs
  own their data and retain no GC graph or value pointers.
- `src/access.cpp`, `pointwise_kernel.cpp` and `reduction_kernel.cpp` compose
  core index maps with physical addresses and check supported domains and bounds.
  `matmul_kernel.cpp` additionally checks the ascending f32 FMA-chain contract.
- `src/schedule.cpp` forms launch groups and assigns storage by lifetime. Execution
  validates topology, shapes, declaration dependencies, launch legality and storage
  non-overlap. A changed declaration or incompatible core map invalidates reuse.
- `src/graph_values.cpp` shares resident leaf types across dispatch and region
  rewriting. `region_outline.cpp` preserves tuple/struct structure with explicit
  leaf transfers. Pure fixed tuple projections of an eligible producer remain in
  its dependency-convex region; selected results avoid sibling downloads.
- `src/scalar_expr.cpp`, `index_expr.cpp` and `kernel_ir.cpp` represent typed scalar
  expressions, checked index expressions and structured kernel statements. Serial,
  scalar, parallel maximum, matrix and row-region lowering emit this representation;
  CUDA printing is centralized. Statement validation checks bindings, types, memory
  permissions and barrier uniformity; it is not a general race or bounds proof.
- `src/loop_kernel.cpp` lowers a pure static-array `For` recurrence to one device
  loop with f64 scalar states or same-shape, lane-local f32 tensor states. The compiler
  owns and lowers the body once, with explicit state and capture ports. The static
  table retains exact int64 elements; `float(k)` uses an explicit nearest-even
  i64-to-f64 instruction. Empty domains retain every initial state. A producer owns
  ordered result descriptors and no value allocation; generic result projections
  hold the individual values. One launch writes all projections, whose lifetimes
  start at that producer launch. All next-state expressions are bound before any
  state assignment. Dynamic domains, while conditions, mixed recurrent lane
  domains or dtypes, nested loops, integer element arithmetic and
  non-pointwise or shape-changing bodies remain unsupported computations.
- `src/codegen_cache.cpp` recursively keys recurrence domains, port maps, next
  states, body constants, index instances, numeric grants and declaration revisions.
  Embedded body constants retain their exact bits; outer invocation values and
  their owners remain outside the source key. Validation rejects unsupported
  nested loop bodies before recursively validating their dependencies.
- `src/row_kernel.cpp` and `row_body.cpp` plan shared row intermediates. Maximum is
  order-independent under the canonical NaN/zero policy. Sum uses an exactness proof
  for a parallel f64 sum and falls back to the canonical ordered chain when needed.
- `src/cuda_backend.cpp`, `cuda_resources.cpp` and `device.cpp` manage driver/NVRTC
  ownership, compiled-module leases and device resources. Resident inputs may be
  borrowed; resources retain their context/module ownership until completion.
  Current public execution waits for completion before CPU use.

## Validation

Build with `npm run build`, manually inspect new cases, then run the relevant plans:

```text
node scripts/test.js test/plans/feat/modules/tiles.plan.toml test/plans/gpu test/plans/feat/modules/graph_facts.plan.toml
```

The native plan checker exercises malformed plans, index-map mutations, numerical
contracts, cache invalidation and structured-kernel validation. GPU plans exercise
actual registered operators across execution engines, strict reduction bits,
gradients, resident values and transfer lifetimes. Synthetic map tests alone do
not prove arbitrary custom operators execute correctly on GPU.

Keep ordinary test logs in local `tmp/` or CI artifacts. Include relevant commands,
results and limitations in feature commit descriptions. Durable performance evidence
and reproduction commands live in [measurements](../../docs/performance/tiles/README.md).
Do not run builds and tests concurrently against the same output directory.

## Remaining work

The existing kernel families use structured physical statements and consume core
index contracts. `std::tiles` and `@gpu` extract supported regions, compile static
shapes and rewrite their boundaries with resident leaf transfers. Static-array
lane-local For loops support multiple compatible states in one device recurrence.
Remaining work includes symbolic shape code generation, dynamic domains and
conditions, mixed recurrent lane domains, nested loops and non-pointwise loop
bodies, expert block kernels, general masked/indirect accesses, asynchronous
device plans, and broader optimized implementations and tuning. See the design
route for the full intended scope; passing the current test suite does not
complete that scope.
