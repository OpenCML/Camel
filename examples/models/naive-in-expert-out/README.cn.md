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
# The naive Camel step, on any of the three CPU back ends.
camel examples/models/naive-in-expert-out/train_step.cml
camel examples/models/naive-in-expert-out/train_step.cml std::fvm
camel examples/models/naive-in-expert-out/train_step.cml std::jit

# The three PyTorch counterparts and the float64 reference, plus the table.
python examples/models/naive-in-expert-out/measure.py

# The accuracy measurements (bf16/fp32/fp64; Camel's f32 side when --camel is given).
python examples/models/naive-in-expert-out/accuracy.py \
    --camel out/latest/bin/camel
```

`measure.py` needs a built Camel install and a Python with torch; it finds both from the checkout
and `sys.executable`.  It writes `results.json` next to the script (ignored by git) and prints
the table.  The Camel program uses only the environment variables the harness sets:
`NAIVE_INIT_DIR`, `NAIVE_FINAL_DIR`, and `NAIVE_BENCH=1`.

## What the naive program does

`train_step.cml` is written the textbook way, with no numerical care:

- three separate Q, K, V projections (`h @ wq`, `h @ wk`, `h @ wv`);
- `p = exp(scores) / sum(exp(scores), -1, true)`, with no maximum subtracted;
- `p = exp(logits) / sum(exp(logits), -1, true)` then `log(p)` for the cross-entropy, with no
  `log_softmax`;
- RMSNorm, GELU (the tanh approximation), Adam, and a `with_grad` backward pass.

The labels are a one-hot product; a label is discrete, so its derivative is zero by definition
(design principles 9.9). `one_hot` takes no gradient and the labels need no `stop_gradient`.

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

- The public `Tensor` element types are effectively f32, i64 and bool: no bf16/f16, and f64 is
  normalised to f32 by `normalizeTensorDType` on creation and cast.  bf16 overflow and the fp64
  error of the step cannot be measured in Camel today; its f32 thresholds match PyTorch's f32
  thresholds exactly.
- No fused QKV/attention operator and no `rules::accuracy`/`rules::fusion` pipeline, so the
  expert form is not produced from the naive source.  `softmax`, `log_softmax` and the fused
  cross-entropy exist as operators and are used by the PyTorch expert and the accuracy probes.
- `explain` (the rule-rewrite chain) is not available yet, so this directory cannot show it.