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

The thread budget is the command line's `-t`: every pool of a run takes its size from that one
number (the graph scheduler and each module's worker pool, the tensor kernels' included), and
`CAMEL_NUM_THREADS` is no longer read.  `measure.py` passes `-t 1` to Camel and
`torch.set_num_threads(1)` plus the OMP/MKL variables to PyTorch, so each framework runs one
thread by its own documented mechanism.

## Results

Measured 2026-10-22 19:27 UTC (2026-10-23 03:27 Asia/Shanghai).  The table is exactly what
`measure.py` printed; the raw file, with every per-step time and the load it saw, is
[`results/2026-10-23-linux.json`](results/2026-10-23-linux.json).

| implementation | ms/step | spread (3 rounds) | peak RSS MiB | final loss | \|dloss\| vs fp64 | max \|dparam\| vs fp64 |
|---|---|---|---|---|---|---|
| Camel (`std::jit`) | **2.091** | 2.029–2.195 | 40.5 | 1.568042986 | 1.8e-08 | 7.0e-06 |
| PyTorch naive | 4.762 | 4.595–4.849 | 643.7 | 1.568042994 | 1.1e-08 | 7.5e-06 |
| PyTorch expert | 3.919 | 3.750–4.013 | 643.7 | 1.568043113 | 1.1e-07 | 1.0e-05 |
| `torch.compile(naive)` | 3.005 | 2.985–3.287 | 924.1 | 1.568042994 | 1.1e-08 | 1.4e-05 |
| float64 reference | 5.802 | — | 648.4 | 1.568043004 | 0 | 0 |

**Conditions.** Host `tangshan88`: AMD EPYC 7343 16-Core, 32 hardware threads, 503 GiB RAM,
Ubuntu 22.04 (kernel 6.8.0-138).  Python 3.10.12, NumPy 2.2.6, PyTorch 2.14.1+cu130 (a CUDA build; every variant ran on the
CPU), Camel v0.2.24 built from commit `ab9cb4579` (clean build: no dirty paths, fingerprint
`0dc0fec4c9b1` over 1,152 source files).  The host is **shared**: other builds and tests ran
throughout, and the 1-minute load average was 31.7 when the run started and 34.1 when it
ended, on 32 hardware threads.  The harness that produced the file is `38fbcbacc`'s
`measure.py` (at measurement time it was the same content, not yet committed); it is Python,
and nothing in the measured binary changed.

**Command.**  The whole table is one command:

```sh
python examples/models/naive-in-expert-out/measure.py \
    --camel out/latest/bin/camel --backend jit --repeats 3 \
    --output results/2026-10-23-linux.json
```

It runs, with the thread budget passed explicitly to each framework:

```sh
out/latest/bin/camel -t 1 .../train_step.cml std::jit \
    --arg bench=true --arg init_dir=<scratch>/init --arg final_dir=<scratch>/final/camel
python .../torch_impl.py --variant naive|expert|compiled|reference \
    --init <scratch>/init --steps 16 --warmup 3 --threads 1 \
    --save-final <scratch>/final/<variant>.npz
```

Each implementation runs 16 steps; the first 3 are warm-up and the step time is the median of
the other 13, measured inside the process around one forward, backward and Adam update.  The
four are measured in three interleaved rounds with a rotating order; ms/step is the median of
the three round medians and the spread is their minimum–maximum.  Peak RSS is the maximum
over the rounds of `/usr/bin/time -v`'s maximum resident set for the whole process,
interpreter and allocator included, so it compares footprints, not model sizes.

**What the spreads say.** Camel 2.029–2.195 (spread 0.166 ms), PyTorch naive 4.595–4.849
(0.255), expert 3.750–4.013 (0.262), compiled 2.985–3.287 (0.301).  The adjacent gaps are
0.914 ms (Camel → compiled), 0.914 (compiled → expert) and 0.843 (expert → naive), each larger
than the largest spread (0.301), so the order is real in this window.  A difference below
~0.3 ms is not, and the absolute values are not portable: the same Camel row measured in a run
that began at load 42 and ended at 68 was 3.081 ms (spread 2.165–4.555), and an early
single-round probe without a recorded load gave 2.552 ms.  This is why the table has a spread
column instead of a ranking alone.

**What it does and does not show.** The 2.091 ms row is the *naive* source: separate Q/K/V, no
maximum subtraction, `log(softmax)`, no kernel written for this model.  It is faster than the
same naive algorithm in eager PyTorch (4.762), than PyTorch's hand-written expert form (3.919)
and than Inductor's compiled naive (3.005).  The win is the tensor runtime and the scheduler,
not a hand-tuned kernel, and not the rules: `rules::accuracy`/`rules::fusion` do not exist yet,
so the naive source was not rewritten into the expert form.  The comparison also cuts the
other way: Inductor's compiled naive beats the hand-written expert form here, so "expert" is
not synonymous with fastest.

**The earlier numbers.** 2.062 ms/step was reported for an earlier revision and 2.314 ms/step
on a busy host.  Neither report left its machine, command or revision in the tree—this
repository contains neither number in a file or a commit message—so they are context, not a
baseline.  This table's 2.091 (2.029–2.195) is consistent with both, and 2.062 vs 2.091 is
below this measurement's spread: it is not evidence of a regression.

## What the naive program does

`train_step.cml` is written the textbook way, with no numerical care:

- three separate Q, K, V projections (`h @ wq`, `h @ wk`, `h @ wv`);
- `p = exp(scores) / sum(exp(scores), -1, true)`, with no maximum subtracted;
- `p = exp(logits) / sum(exp(logits), -1, true)` then `log(p)` for the cross-entropy, with no
  `log_softmax`;
- RMSNorm, GELU (the tanh approximation), Adam, and a `with_grad` backward pass.

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
- **The two blocks are now one `Block[]`, and the forward pass is the plain
  `loop h = x for b in m.blocks`.**  An array's tangent is the array of its elements' tangents
  (design principles 9.9), so a `for` over an array that depends on a differentiated parameter
  differentiates: the loop's reverse pass scatters each round's gradient into the array at that
  round's index, and the optimizer and `save_params` walk the array element by element.  An array
  has no tangent only when its **element type** has none (string, a function, `Map`), and that
  refusal happens at the `loop`, naming the array type, the element type and where the array is
  written.  The checkpoint file names are unchanged (`blocks.0.wq.npy`, `blocks.1.wq.npy`):
  array elements are named by index, so an existing checkpoint still loads.
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
- **`explain` (the rule-rewrite chain) is available now** (`run`/`explain` in `stdlib/rewrite.cml`), but this directory does not show it yet.