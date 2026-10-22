# Naive in, expert out: one Transformer training step

The first half of the flagship example: the naive program, its three PyTorch counterparts, the
measurement harness, and the accuracy framework.  The rules pipeline that turns the naive source
into the expert form (`rules::accuracy`, `rules::fusion`, `explain`) is the other half and is not
here.

The model is small and fixed: batch 8, sequence 16, d_model 64, 4 heads of width 16, FFN 128,
vocabulary 32, two pre-LN blocks, 68,288 parameters.  The loss is a per-position softmax
cross-entropy; the labels are the largest of the first 32 features of each position.  Seeds are
fixed and no wall clock enters the model.

## Run

```sh
# The naive Camel step, on any of the three CPU back ends.  The program takes its inputs on
# the command line; an empty directory means "write nothing".
camel -t 1 examples/models/naive-in-expert-out/train_step.cml \
    --arg bench=false --arg init_dir= --arg final_dir=
camel -t 1 examples/models/naive-in-expert-out/train_step.cml std::fvm \
    --arg bench=false --arg init_dir= --arg final_dir=
camel -t 1 examples/models/naive-in-expert-out/train_step.cml std::jit \
    --arg bench=false --arg init_dir= --arg final_dir=

# The three PyTorch counterparts and the float64 reference, plus the table.
python examples/models/naive-in-expert-out/measure.py

# The accuracy measurements (bf16/fp32/fp64; Camel's f32 side when --camel is given).
python examples/models/naive-in-expert-out/accuracy.py \
    --camel out/latest/bin/camel
```

`measure.py` needs a built Camel install and a Python with torch; it finds both from the checkout
and `sys.executable`.  It writes `results.json` next to the script (ignored by git) and prints
the table.  The Camel program reads no environment variable: `bench`, `init_dir` and
`final_dir` are `main` parameters, and the first run above passes all three explicitly.
`measure.py` passes them itself (`--arg bench=true --arg init_dir=... --arg final_dir=...`).

## What the naive program does

`train_step.cml` is written the textbook way, with no numerical care:

- three separate Q, K, V projections (`h @ wq`, `h @ wk`, `h @ wv`);
- `p = exp(scores) / sum(exp(scores), -1, true)`, with no maximum subtracted;
- `p = exp(logits) / sum(exp(logits), -1, true)` then `log(p)` for the cross-entropy, with no
  `log_softmax`;
- RMSNorm, GELU (the tanh approximation), Adam, and a `with_grad` backward pass;
- the two blocks are one `Block[]` value walked by `loop h = x for b in m.blocks`: an array's
  tangent is the array of its elements' tangents, so the loop differentiates, and the optimizer
  and `save_params` walk the array element by element.

The scale is `1 / sqrt(d_head)`, not a literal `4.0`: `attention_scale` is computed once from the
width the model actually uses.  The parameter count is a named function over the model's shapes
(`param_count`), not an expression written inside the `println` interpolation.

The labels are a one-hot product; a label is discrete, so its derivative is zero by definition
(design principles 9.9). `one_hot` registers the empty rule and the labels need no `stop_gradient`.

## The PyTorch counterparts

`torch_impl.py --variant naive|expert|compiled|reference`:

| variant | what differs from the Camel source |
|---|---|
| `naive` | nothing: separate Q/K/V, `exp/sum(exp)`, `log(softmax)` |
| `expert` | `F.scaled_dot_product_attention`, `F.log_softmax`, one fused QKV projection |
| `compiled` | `torch.compile` of the naive loss (compiled forward and backward; Adam stays eager and is the same explicit formula in every variant) |
| `reference` | the same graph in float64; not one of the four measurements |

The harness first runs the Camel program, which writes its initial parameters and data as
`.npy`; every PyTorch variant loads exactly those files.  That is why the numbers are comparable
at all across frameworks.  The Adam update is the same explicit formula in all four, computed in
float64 and stored in float32, matching `nn.adam`; the optimizer is not one of the differences.

## Tolerance

`measure.py` requires the final loss to be within `1e-5` absolute and every final parameter
within `2e-4` absolute of the float64 reference.  Equality cannot be exact: the same f32
operation is reduced in a different order by Camel's kernels and by ATen/oneDNN, SDPA computes
the attention block differently from the explicit softmax, the fused QKV matmul is one reduction
instead of three, `torch.compile` fuses elementwise chains, and the float64 reference is a
different width altogether.  The observed gaps are one to two orders of magnitude below the
bounds (see the table `measure.py` prints).

## Accuracy

`accuracy.py` scans the input range for the first value where the naive form stops being finite,
for bf16, f32 and f64, and reports the expert form on the same input.  It also measures the fp64
error of softmax, log_softmax, cross-entropy and the matmul reduction, and the bf16 reduction
that rounds a `1` away.  `accuracy.cml` repeats the f32 measurements in Camel.

## Gaps found (Camel side)

- **H238 (priority): tensors still keep one storage type.** The public tensor element types are
  effectively f32, i64 and bool; f64 is normalised to f32 by `normalizeTensorDType` on creation
  and cast, and i32 to i64.  This paragraph is provisional: when **H238** lands (branches
  `handover/h259-dtype`, which refuses `Tensor<f64>`/`Tensor<i32>` instead of substituting, and
  `handover/h260-tensor-storage`, which deletes the normalisation table), delete it and add an
  f64 column to the table if f64 then works.  Until then bf16 overflow and the fp64 error of the
  step cannot be measured in Camel; its f32 thresholds match PyTorch's f32 thresholds exactly.
- **`f32[b, t, d]` does not reach the tensor operators.**  `exp`, `@`, `sum` and the rest take
  `Tensor`, not the core array type; `let x: f32[2, 3] = randn(...)` is a `DeclaredTypeMismatch`.
  The example writes shaped `Tensor<f32, [8, 16, 64]>` (and named aliases `TokenStates`,
  `HeadStates`, ...), so every signature and every field shows its extents.  Dimension names
  inside `Tensor<f32, [b, t, d]>` do not bind (the checker warns `DimensionNameUsedOnce`), so the
  extents are written as literals and named by the aliases and the comment at the top.
- **No dtype conversion written as a type.**  `f32(oh)` has no tensor overload, and
  `oh as f32[...]` is a `DynamicCastForbidden`; the one-hot product uses numeric promotion
  (f32 * int64 gives f32) instead of the old `cast(oh, 'float32')` string.  A typed tensor cast
  is still a gap.
- **No fused QKV/attention operator and no `rules::accuracy`/`rules::fusion` pipeline**, so the
  expert form is not produced from the naive source.  `softmax`, `log_softmax` and the fused
  cross-entropy exist as operators and are used by the PyTorch expert and the accuracy probes.
- **`-t` does not reach the tensor worker pool.**  The option caps the graph scheduler; the
  tensor kernels still read `CAMEL_NUM_THREADS`.  `measure.py` sets that for all four
  implementations (as it sets OMP/MKL for PyTorch) so the timing is one thread; the program
  itself reads no environment variable.  Wiring `-t` through to the kernels is a runtime gap.
- **`explain` (the rule-rewrite chain) is not available yet**, so this directory cannot show it.
