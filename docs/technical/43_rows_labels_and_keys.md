# Rows, class labels and keys

H20-2 and H20-3. This note records what the new operators do, the representation each one chose,
the reverse rule and the export mapping, the keyed random algorithm, and the points left open. The
authoritative semantic statements are in [design principles](../design-principles.cn.md) 5.4 and
13.5; the review that ordered the work is
[H13-H14](../handover/reviews/H13-H14.md).

## 1. `take`: rows along the first dimension

`tensor::take(t, indices)` is the function form of `t[idx]` (design principles 5.4) until the H21
indexing syntax lands. It takes rows along the first dimension:

- `indices` is an `int[]` or a rank-1 integer tensor (`int64`, or `int32` from the array
  bridge). A floating or rank-2 index argument is rejected when the call is known: inference
  throws a `ShapeError`, reported as `SemanticDiag::ArgumentsRejected` at the call; when the
  argument's type is only known at run time the same check reports `TensorDimensionMismatch`
  ("expects integer indices").
- The result shape is `[len(indices)] + shape(t)[1:]`, with `t`'s element type. `take` of a
  rank-1 tensor returns a rank-1 tensor; a scalar tensor is refused (`rank >= 1`).
- Repeated indices are kept and appear in the given order.
- The result is a **copy**, not a view: an arbitrary row order is not an affine view of the
  buffer, and pretending otherwise would make later in-place rules (10.5) unsound. `slice` stays
  the view-producing operator.
- `indices = []` is an empty result with the right trailing dimensions; an untyped empty literal
  has no element type to infer from yet, so it is accepted as the empty integer list.

Bounds are checked where they are known:

| when | what happens |
|---|---|
| constant index array, static first extent | `SemanticDiag::ArgumentsRejected` at the call, naming the index and the extent (inference throws `ShapeError`) |
| runtime index | located `RuntimeDiag::IndexOutOfRange` at the `take` call: "Index -1 is out of range for the first dimension of the input tensor of length 4." |
| negative | the same fault; there is no Python-style wrap-around (3.9, 5.4) |

### Reverse

The VJP of `take` is an internal `tensor:take_grad(t, indices, dy)` (not exported): it creates a
zero tensor shaped like `t` and **adds** each result row into row `indices[i]`. Repeated indices
therefore accumulate (`take` twice from row 0 gives gradient 2 on that row), which is the correct
transpose of a gather. The dtype follows `t` (float, int or bool; bool accumulates with `or`).
`take_grad` has no user-facing name and no ONNX lowering; export sees only `take`.

### ONNX

`tensor:take` lowers to `Gather(axis = 0)`. The indices operand becomes an i64 initializer when it
is a constant `int[]` (an array has no operand name of its own) or an i64 input/initializer when it
is a tensor. Constant indices are checked against a statically known first extent at export time
and an out-of-range index is an `ExportError` before any node is emitted. `nn:embedding` already
used the same `Gather` mapping; the two now share it.

### `std::cpp`

Not available yet: `std::cpp` refuses tensors entirely (design/cpp-standalone, phase 5), so any
program whose signature mentions `Tensor` is refused with a located `TranslationRefused` naming
the parameter type. This is a dependency on the standalone tensor bridge, not a gap in `take`;
the refusal is covered by the existing `trans.cpp` tests, not by the new plan.

## 2. `one_hot` and the two cross-entropy forms

`tensor::one_hot(labels, classes)` accepts the same label forms as `take`'s indices and returns a
2-D numeric tensor `[len(labels), classes]`, one 1.0 per row at its label and 0.0 elsewhere.

- The element type is **f32**. H15 (literal context typing) has not landed, so there is no
  per-element type annotation to choose e.g. f64; f32 is the tensor stack's default numeric type
  and what `nn` losses expect.
- `classes < 0` is reported at compile time as `SemanticDiag::ArgumentsRejected` when constant
  (inference throws a `ShapeError`) and as a located `InvalidArgument` when it is only known at
  run time; a label outside `[0, classes)` is a located `IndexOutOfRange` at the call, naming the
  class dimension and its length ("Index 3 is out of range for the class dimension of one_hot of
  length 3.").

`nn::softmax_cross_entropy(logits, target)` and `nn::softmax_cross_entropy_grad(logits, target, dy)`
are **overloaded on the target's type**, as two operators with the same exported name and
different signatures — not a runtime branch:

| target | selected definition | target meaning |
|---|---|---|
| `Tensor` with a floating element type, or an array of numbers | `nn:softmax_cross_entropy` | one-hot rows `[batch, classes]`, each row summing to 1 |
| `Tensor` known to have an integer element type, or `int[]` | `nn:softmax_cross_entropy_labels` | one class index per logits row; an `int64` or `int32` tensor is accepted |

Overload resolution is the ordinary `OperatorGroup` mechanism: the one-hot definition's inference
returns "not mine" when the target dtype is known to be integer, and the labels definition returns
"not mine" unless it is. The labels form checks rank 2, one label per row (`TensorDimensionMismatch`)
and the label range (located `IndexOutOfRange`, "the class dimension of softmax_cross_entropy").

The two forms compute the same arithmetic in the same order (row maximum, `exp` sum, `log` sum,
then `loss -= logit[label] - logZ`), so for the same logits and the equivalent target the value and
the gradient are bitwise equal; the test asserts `grad_diff=0.0` with
`sum(abs(a - b))`. The
gradient of the labels form is `dy * (softmax(logits) - one_hot(labels))`, again with the same
loop order as the one-hot kernel.

`softmax_cross_entropy_grad` gets the same two definitions, so `with_grad` of either form
works without a new API. A target whose element type is **not fixed** (a bare `Tensor` parameter)
keeps the one-hot form, which is the pre-existing spelling; passing integer data there is a type
error the compiler cannot see, and is listed under the open questions.

## 3. Keyed random

`random::key(seed)`, `random::split(key)` and `random::permutation(key, n)` are pure functions:
they are registered with `pure = true`, declare no resources, and never read or advance the
process generator. Calling them twice with the same arguments gives the same value; the draws
around them are unchanged (the test seeds, draws, calls all three, draws again, and re-seeds to
repeat the draws).

### The `Key` value

A key is 128 bits, represented as the struct

```
type Key = { hi: int, lo: int }      // the value is (hi << 64) | lo, halves as unsigned bits
```

Both fields are signed int64. This is the representation until nominal types (H5) exist; it
prints deterministically as `{ hi: 1350887118657991639, lo: -7139630017956984517 }`, compares by
content, and can be stored in trees/arrays like any struct. `random` exports the type, so
`random.Key` names it. The interpreter and the generated C++ struct use the same field order, so
the printed form and `==` agree across schedulers and `std::cpp`.

### Algorithm (normative)

Philox4x32-10, one implementation in `include/camel/utils/philox.h` included by both the random
module's kernels and the programs `std::cpp` writes:

- **Block function**: eight 32-bit words (`c0..c3`, `k0..k1`), ten rounds of
  `p0 = M0*c0`, `p1 = M1*c2`, `c0 = hi(p1) ^ c1 ^ k0`, `c1 = lo(p1)`,
  `c2 = hi(p0) ^ c3 ^ k1`, `c3 = lo(p0)`, then `k0 += W0`, `k1 += W1`, with
  `M0 = 0xD2511F53`, `M1 = 0xCD9E8D57`, `W0 = 0x9E3779B9`, `W1 = 0xBB67AE85`.
  `mulHigh` computes the high half of a 64×64 product in 32-bit pieces, so the code builds with
  MSVC (no `__int128`).
- **`key(seed)`**: the block with Philox key = `seed` and counter `0`, packed low-first:
  `lo = o0:o1`, `hi = o2:o3`.
- **`split(k)`**: two children, domains `0x9E3779B97F4A7C15` and `0xD1B54A32D192ED03`, each the
  block with Philox key = `k.lo` and counter `(k.hi, domain)`, packed as above.
- **`permutation(k, n)`**: start from `[0, n)`, then for `i` from `n-1` down to 1 draw
  `j ∈ [0, i]` and swap. Each draw is the 64-bit word `o0:o1` of the block with Philox key =
  `k.lo` and counter `(k.hi, 0x8000000000000000 + draw index)`; the draw index advances across
  every block, including rejected ones. `j` comes from Lemire's multiply-and-reject method:
  `hi = mulHigh(r, i+1)`, `lo = r * (i+1)`, reject when `lo < (2^64 % (i+1))`.
- `n = 0` is `[]` and `n = 1` is `[0]`, with no draws; `n < 0` is a located `InvalidArgument`
  ("the permutation length is -1; it must be non-negative").
- `permutation` allocates and returns a new `int[]`; it is uniform over the `n!` permutations
  because every swap chooses uniformly from the remaining prefix.

### Determinism and `std::cpp`

All four schedulers, the interpreter and translated C++ call the same header, so the keys, the
split children and the permutations are bit-identical. The translated program uses
`rt::randomKey<Key>(seed)`, `rt::randomSplitKey<Tuple, Key>(key)` and
`rt::randomPermutationKey<Array, Key>(key, n, site)` from `camel/standalone/random.h`; there is no
generator and no state, so the expressions may be shared or reordered by the translator, and the
slow `keyed_random.std_cpp_agrees` test compares their output with `std::fvm`.

## 4. Verification

Commands and counts from this worktree (`out/latest`, Windows, clang 20):

| command | result |
|---|---|
| `node scripts/test.js test/plans/feat/modules/random.plan.toml --tier all --jobs 2` | 11/11 pass (four schedulers, generator independence, negative length, `std::cpp` agreement) |
| `node scripts/test.js test/plans/feat/modules/tensor.plan.toml --jobs 6` | 50/50 pass, including the five new `take`/`one_hot` tests (the runtime and fault tests run on all four schedulers) |
| `node scripts/test.js test/plans/feat/modules/nn.plan.toml --jobs 4` | 64/67 pass; the 3 failures are `mnist_py*`, which need the Python module's DLL (`python.cmo` loads but `python313.dll` is absent in this worktree), unrelated to H20; the two labels tests pass on all four schedulers (the three `mnist_py` cases were deleted on 2026-10-23: an early example that needed a network download for its data set and skipped silently when it could not get one) |
| `node scripts/test.js test/plans/feat/modules/onnx.plan.toml --jobs 4` | 102 pass, 0 fail, 19 skipped (no `onnx`/`onnxruntime`/`torch` Python packages) — `export_take` passes and checks one `Gather` + i64 initializer |
| `node scripts/test.js test/plans/feat/modules/stdlib_registry.plan.toml --jobs 2` | 4/4 pass (purity, signatures, generated reference) |

The ONNX runtime checks of the exported model are skipped in this environment, as the plan
preconditions report; the structural check (node type, initializer, graph IO) runs without the
Python packages.

## 5. Open questions

1. **A bare `Tensor` target** cannot be classified at compile time, so `softmax_cross_entropy`
   keeps the one-hot form for it. Once H15 literal/dtype refinements or a labels-typed view exists,
   the overloads could be selected by the element type even when the caller only says `Tensor`.
   Passing integer labels through such a parameter is a type error the current compiler cannot
   report; it is not made a runtime branch because that would cost a dtype test per call.
2. **`Key` as a struct** is a stopgap. H5 (nominal types) should make it opaque so that
   `hi`/`lo` are not names user code can rely on, while keeping the same printed form and bit
   layout for compatibility with existing keys.
3. **`std::cpp` and tensors**: `take`, `one_hot` and the cross-entropy forms are refused until the
   standalone tensor bridge (phase 5) exists; only the keyed random operators are translated
   today. The report records this rather than adding a tensor representation to `std::cpp` in this
   handover.
4. **`one_hot` element type**: fixed to f32 because H15 has not landed. If H15 defines a context
   type for numeric array literals, `one_hot` should follow it; the labels-cross-entropy math
   already works in the logits' dtype.
