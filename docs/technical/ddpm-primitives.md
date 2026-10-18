# DDPM experiment: missing runtime primitives

The standalone Camel-DDPM experiment exposed missing scalar/file primitives and
GPU lowering needed by dense and patch-based denoisers.
These changes add library capabilities without changing language semantics.

- `math:sqrt` now publishes an f32/f64 VJP in its owning module. It computes
  `(dy * 0.5) / y` in the result's float width; integer inputs have no tangent.
  The rule is intended for the differentiable domain `x > 0`; no derivative at
  zero or outside the real domain is promised. The normal scalar arithmetic
  fault/IEEE behavior remains in force there.
- `fs.write_text` writes the string's stored byte length, including embedded
  NUL. Its binary-mode contract already promised byte-preserving output;
  streaming `c_str()` had incorrectly truncated PGM images at the first NUL.
  Write/close failures now raise a file-system fault instead of reporting success.
- Tiles specializes `tensor:numel` to an integer attribute using the concrete
  invocation shape. This lets the `mean` VJP compute its denominator without
  treating the shape query as a device tensor. A second invocation with a
  different shape is specialized independently.
- Tiles also specializes `tensor:shape` to a host-only integer-array attribute.
  Reshape pullbacks consume it using the normal core layout instantiator. No
  synthetic array pointer survives graph extraction.
- `tensor:permute_inverse`, emitted by the permutation VJP, lowers to a checked
  inverse permutation through the core `tensor:permute` index instance. Both
  operator contracts remain dependencies of the generated plan. Negative axes
  are normalized and malformed permutations are rejected.
- Tiles lowers rank-2 and higher matrix products using the existing Tensor core
  index instance, including singleton and unequal-rank batch broadcasting. Batch
  coordinates are decoded from grid Y and passed through the core access maps;
  no alternative broadcasting convention is introduced. Ascending f32 FMA
  accumulation remains unchanged. Empty output batches perform no operand reads;
  an empty contraction stores the positive-zero initial accumulator.
- Matrix pullbacks compose transpose, matmul and sum-to through those same
  primitives. A shared rank-2 RHS uses the CPU gradient's flattened batch/row
  GEMM, preserving its single FMA chain rather than rounding independent batch
  products and adding them afterwards. Rank-1 vector products remain unsupported
  by Tiles. Launches exceeding the existing CUDA grid-Y limit are rejected.

Each failure was reproduced manually before adding plan assertions. Run:

```powershell
node scripts/test.js test/plans/feat/modules/ddpm_primitives.plan.toml
```

The plan checks scalar gradients with runtime/macro construction on NVM/FVM/JIT,
embedded-NUL file round trips, and GPU-independent CUDA source generation for
mean/layout gradients with changing shapes. The layout case uses a non-self-inverse
permutation and negative axes. Batch products additionally cover changed batch
sizes, rank-3/4 inputs, broadcasting, shared projections and non-tile-aligned
dimensions. CUDA execution checks an independent integer-valued product, empty
cases, CPU/GPU forward and gradient agreement and directional finite differences.
To execute these on CUDA hardware:

```powershell
camel --arg run_gpu=true test/cases/modules/tiles/mean_grad.cml std::macro std::fvm
camel --arg run_gpu=true test/cases/modules/tiles/layout_grad.cml std::macro std::fvm
camel --arg run_gpu=true test/cases/modules/tiles/batch_matmul.cml std::macro std::fvm
```

The experiment's own tests additionally compare its GPU gradients and Camel Adam
implementation with the CPU autodiff and `nn.adam` implementations. GPU capability
checks use explicit Tiles `run`/`run_device`; they cannot silently fall back to CPU.

## Explicit BF16 matrix products

A matrix product with all three lexical grants `reassociate`, `change_precision`,
and `finite_math` selects `cublas_bf16`. Partial or absent grants keep the existing
ascending f32 FMA implementation. The optional cuBLAS library is loaded only on
execution, requires Ampere or newer, and uses COMPUTE_32F_FAST_16BF: FP32 input
storage, BF16 operand conversion, FP32 accumulation/output, no atomics and no
reduced-precision reduction. Shared rank-2 projections flatten rows; equal batches
use strided GEMM; other broadcast patterns use the core batch mapping.

The private handle is bound to the execution stream and retained until completion.
This is not global half-storage AMP: graph boundaries and optimizer state stay
FP32. GPU availability or cuBLAS failures are errors, never CPU fallback. Manual
CUDA verification distinguishes BF16 rounding from strict FP32; full latent-model
checks compare forward/backward gradients with CPU FP32 references.
