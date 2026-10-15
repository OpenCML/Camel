# Camel Tiles

Experimental bundled C++ module for compiling pure Camel tensor function graphs
to NVIDIA CUDA kernels. It builds with Camel and is imported with `from tiles`.
This is the first executable foundation of the
[indexed computation proposal](../../docs/design/indexed-computation/README.md),
not an implementation of its complete language, scheduling, or optimization model.

```cml
import { Tensor, reshape } from tensor
import { run } from tiles

func add(a: Tensor<f32>, b: Tensor<f32>): Tensor<f32> { return a + b }

func main(): int sync {
    let x = reshape([1.0, 2.0, 3.0], [3])
    println(run<add>((x, x)))
    return 0
}
```

## API

- `run<fn>(inputs)` specializes a function graph for actual input shapes, compiles
  CUDA with NVRTC, executes on the GPU, and returns a host `Tensor<f32>`.
  Pass one tensor for a unary function or a tuple for multiple parameters.
- `source<fn>(inputs)` returns generated CUDA without initializing CUDA or
  executing the function body. It performs the same graph validation as `run`.
- `inspect<fn>(inputs)` returns a versioned JSON plan summary without CUDA:
  semantic values, kernel groups, materialized values, physical allocations,
  logical bytes and allocated bytes. These are plan metrics, not timings.
- `benchmark<fn>(inputs)` compares the optimized and unfused schedules with
  the same input snapshots. It returns JSON containing raw CUDA-event samples,
  host compile/load/allocation/transfer times, hardware/compiler metadata, and
  correctness comparisons. It requires a nonempty computed output.
- `available()` reports whether driver/NVRTC initialization succeeds. A successful
  result does not guarantee a subsequent compilation or memory allocation succeeds.
- `device()` reports the selected GPU, or raises a diagnostic explaining failure.

The function context is ordinary Camel context, not a compile-time modifier.
The module inspects the supplied function graph at invocation. It never traces
the body by executing it and never silently falls back to CPU execution.

## Supported subset

Contiguous f32 tensor inputs and outputs; direct nonrecursive function calls;
captured tensors and supported numeric/static attributes; broadcasting arithmetic
(`+`, `-`, `*`, `/`, minimum, maximum); negation, abs, exp, log, sqrt, rsqrt, relu;
axis sum/max/mean with static attributes; reshape with static shape; rank-2 matmul.
Softmax is expressed as max/subtract/exp/sum/divide, not recognized by function name.
Unsupported graph nodes, effects, mutable parameters, dynamic attributes, other
element types, and unsupported tensor operations produce diagnostics.

## Execution and numerical behavior

The compiler snapshots inputs into an owned IR, retaining no Camel GC pointers
across CUDA work. Shape inference reuses the tensor operation registry.
Intermediates remain on the device. Equal-domain single-use elementwise chains
are fused into groups of at most 64 operations. Shared results and domain changes
retain materialization boundaries. Each fused node keeps an f32 rounding point;
the scheduler does not reassociate expressions or reorder reductions. Allocation
slots are reused only after their last reader on the ordered stream has finished;
a kernel cannot overwrite storage it reads. Inputs start live at upload time.
Plan validation checks topology, shapes, stage boundaries, launch dimensions,
storage capacities and nonoverlapping lifetimes. Matmul uses masked 16x16 shared-memory tiles and explicit
f32 FMA. Last-axis maximum uses a block-parallel implementation for widths of at
least 128, with original-index tie breaking. It preserves the first element's
NaN and signed-zero ties according to the serial tensor operation. Other axis
reductions are serial per output, with f64 accumulation for sum/mean.
There is no fast-math mode; GPU transcendental results need not be bitwise equal
to CPU results. Floating-point agreement is tested with tolerances.

Driver contexts, streams, buffers, and modules have scoped ownership. A bounded
32-entry cache stores compiled modules by generated source within the selected
device context; it does not cache tensor values. Each invocation uploads fresh
inputs. Calls are synchronous and serialized through the backend. This release
does not claim competitive performance with Triton.

## Build and runtime

Run `npm run build`. CMake discovers CUDA Toolkit headers if installed. Without
them, the module still builds and supports `source`; execution reports that CUDA
support was not built. CUDA Driver and NVRTC are loaded lazily, so importing the
module does not require a GPU. On Windows, the matching NVRTC builtins DLL must
be installed alongside NVRTC.

- `CAMEL_TILES_DEVICE`: nonnegative CUDA device ordinal (default 0), selected at
  first backend initialization.
- `CAMEL_TILES_NVRTC`: preferred NVRTC library path.
- Windows also searches `CUDA_PATH/bin` and the Toolkit directory found at build
  time. Linux uses the loader search path and the configured Toolkit library path.

## Verification

Capture performance records with
`python test/tools/benchmark-tiles.py --runs 3 --output results.jsonl`.
The benchmark performs a correctness execution, five warmups, then 21 timed
executions per variant with alternating order. Inputs are restored before every
execution because storage reuse may overwrite them. Uploads are outside the
CUDA-event interval, which includes kernel launch gaps and measures warmed
resident data. Download time is reported separately. This is not a cold-memory
bandwidth benchmark or end-to-end model throughput. Graph specialization and
snapshot creation are outside the reported phase timings. No timing threshold
is enforced by correctness tests.

The unfused baseline uses the same compiler, arithmetic contract and storage
planner with fusion and parallel maximum disabled. The report records this
reference policy. It is not a Triton or vendor-library comparison.
An `identical_device_code` result is a noise control, not an optimization result.

Run host-only compiler checks with
`node scripts/test.js test/plans/feat/modules/tiles.plan.toml`.
Run NVIDIA execution checks explicitly with
`node scripts/test.js test/plans/gpu/tiles.plan.toml` (also included by the
all-plans runner). These cover vector addition, softmax, odd-size tiled matmul,
broadcasting, fresh values with cached code, reshape, captures, direct calls,
negative-axis reduction, and empty dimensions across NodeVM, FastVM, and JIT.
The host plan also runs native mutation tests built by the standard build. Its
test executable stays in the build tree and is not a distributed runtime tool.

## Remaining architecture

Static `transpose`, `permute` (including negative axes) and positive-step `slice`
lower to a checked index map: source address equals an element offset plus the
dot product of output coordinates and element strides. Bounds are checked before
CUDA execution, including arithmetic overflow. Empty domains do not dereference
addresses. These operations currently materialize contiguous outputs; they do
not yet introduce aliasing or fuse through layout changes. This representation
separates logical indexing from thread placement and is a foundation for later
map composition and view elimination, not the complete indexed-domain design.

First-class index domains, masks, layouts, schedule/resource graphs, loop and
branch lowering, parallel reductions, cross-reduction fusion, asynchronous memory planning, persistent device
tensors, asynchronous transfers, Tensor Core lowering, autotuning, autodiff
integration, and additional backends remain future work. The current private IR
is an implementation detail, not a new normative Camel graph semantics.
