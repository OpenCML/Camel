# Numerical contract audit

This records implementation evidence against the language's numerical contract.
Neither the CPU implementation nor the CUDA implementation is normative by
itself. The language author's ruling is recorded in design principles 2.13 on
`fsh-dev` (latest ruling verified at `d7d192de`). Basic arithmetic follows correctly rounded,
ordered operations without contraction; transcendental functions require a
declared ULP error bound. Five independent explicit permissions govern relaxation.

## Reproduced subnormal difference

On 2026-10-15, the isolated `88zjwei` evaluation checkout, with Tiles and tensor
registry sources from commit `18148014`, was rebuilt using `npm run build`.
RTX 3090 device 7 passed the existing 82 relevant tests. The additional manual
program below nevertheless produced a CPU/GPU difference on all three host
schedulers: `std::nvm`, `std::fvm`, and `std::jit`.

```camel
module tiles_exp_contract_audit
import { Tensor, reshape, exp, sum } from tensor
import { run } from tiles

func exponent(x: Tensor<f32>): Tensor<f32> { return exp(x) }

func main(): int sync {
    let x = reshape([-90.0], [1])
    let cpu = exponent(x)
    let gpu = run<exponent>(x)
    println(sum(cpu))
    println(sum(gpu))
    println(sum(gpu - cpu))
    return 0
}
```

Observed stdout for each scheduler:

```text
0.0
8.194008692231508e-40
8.194008692231508e-40
```

The CPU `modules/tensor/kernels/vmath.h` explicitly returns zero for inputs
below its `kMin` threshold. Its header documents subnormal flushing and an
approximate polynomial implementation. Tiles emits `expf` in
`modules/tiles/src/codegen.cpp`; the tested CUDA implementation preserves this
subnormal result. Turning off implicit multiply/add contraction does not address
this difference.

## Other evidence boundaries

- Tiles matrix multiplication explicitly emits `fmaf` under Q7B;
  disabling implicit FMA contraction does not disable an explicit fused
  operation. The CPU blocked
  GEMM has panel accumulators and optional backend selection. Cross-backend
  bitwise equivalence is not established by the current tests.
- Tiles full sum/mean currently use ordered double accumulation. Matching this
  loop alone is not proof that every upstream operation produces identical bits.
- The benchmark correctness gate compares optimized and reference CUDA schedules,
  not CPU and CUDA implementations. Its f32 comparison uses an absolute and
  relative tolerance. Such an absolute tolerance can admit zero versus a tiny
  nonzero result.
- Its f64 comparison uses numeric equality, not raw-bit equality; it does not
  distinguish signed zeros or NaN payloads. Calling this gate bitwise validation
  would be incorrect.

## Matrix multiplication ruling and correction

The regression `test/cases/modules/tiles/matmul_rounding.cml` computes a two-term
dot product: `-1 + (1 + 2^-23) * (1 - 2^-23)`. Separate f32 rounding produces
positive zero; fused accumulation produces `-2^-46`, observed as
`-1.4210854715202004e-14`. The earlier separate-rounding implementation in
`f2749f17` is superseded by the author's Q7B ruling: matmul itself specifies an
increasing-k FMA chain from +0. Tiles therefore emits `fmaf` with one accumulator
across all K tiles, skipping padded K entries. A 34-term regression crosses two
tile boundaries and distinguishes the continuous chain (1) from independent
panel accumulators (2). This does not grant contraction to ordinary multiply/add
nodes; their NVRTC policy remains `--fmad=false`.

## Remaining contract work

The native module-lease test includes a runtime-input GPU bit probe for the
explicit NVRTC policy: smallest-subnormal multiplication and addition, division
of 1 by 10, square root of 2, and the distinction between ordinary multiply/add
and explicit FMA. It compares raw binary32 output words to exact expected values;
these targeted cases do not establish exhaustive correct-rounding or ULP bounds.

Q5 defines NaN propagation and signed-zero ordering for extrema, but does not yet
specify how different NaN payloads/signs map to result bits. That detail is being
referred to the author before claiming bitwise commutativity or implementing a
canonical-NaN policy. Classification-only NaN tests cannot settle that question.

Complete the reference contract for each composite operation, including input and accumulator
types, rounding boundaries, subnormal handling, exceptional values, and permitted
approximation. Keep caller-granted permissions for reassociation, FMA contraction,
approximate functions, and reduced precision distinct. Establish which guarantees
apply to strict execution and what an explicit relaxation permits.

Add contract-specific edge tests and comparison modes as those details become available. Keep
this diagnostic separate from a golden test that would prematurely enshrine
either currently observed result as the desired language semantics.
