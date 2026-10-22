# Naive in, expert out: one Transformer training step

The first half of the flagship example: the naive program, its three PyTorch counterparts, the
measurement harness, and the accuracy framework.  The rules pipeline that turns the naive source
into the expert form (`rules::accuracy`, `rules::fusion`, `explain`) is the other half and is not
here.

The program is written once, for one shape: batch 8, sequence 16, d_model 64, 4 heads of width
16, FFN 128, vocabulary 32, two pre-LN blocks, 68,288 parameters.  The sweep measures the same
program at d_model 64, 256 and 1024, with batch and sequence scaled as described under [the
sweep](#the-sweep).  The loss is a per-position softmax cross-entropy; the labels are the largest
of the first 32 features of each position.  Seeds are fixed and no wall clock enters the model.

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

# The single-size table: the three PyTorch counterparts and the float64 reference.
python examples/models/naive-in-expert-out/measure.py

# The sweep the claim rests on: three widths, one thread and one per physical core.
python examples/models/naive-in-expert-out/measure.py \
    --d-model 64,256,1024 --threads 1,16 --repeats 3 \
    --output results/2026-10-23-sweep-linux.json

# Why "all cores" is 16 threads on this host and not its 32 hardware threads.
python examples/models/naive-in-expert-out/thread_scale.py \
    --output results/2026-10-23-thread-scaling-linux.json

# The accuracy measurements (bf16/fp32/fp64; Camel's f32 side when --camel is given).
python examples/models/naive-in-expert-out/accuracy.py \
    --camel out/latest/bin/camel
```

`measure.py` needs a built Camel install and a Python with torch; it finds both from the checkout
and `sys.executable`.  A single cell writes `results.json` next to the script (ignored by git) and
prints the table; `--output` names a committed file instead.  The Camel program reads no
environment variable: `bench`, `init_dir` and `final_dir` are `main` parameters, and the first
run above passes all three explicitly.  `measure.py` passes them itself (`--arg bench=true --arg
init_dir=... --arg final_dir=...`).

The thread budget is the command line's `-t`: every pool of a run takes its size from that one
number (the graph scheduler and each module's worker pool, the tensor kernels' included), and
`CAMEL_NUM_THREADS` is no longer read.  `measure.py` passes `-t N` to Camel and
`torch.set_num_threads(N)` plus the OMP/MKL variables to PyTorch, so each framework runs N
threads by its own documented mechanism.  `--threads all` is `os.cpu_count()`, which is 32 on
the host below.

## The sweep

The table this directory used to print measured one point: d_model 64, 128 tokens per step, one
thread, 2.09 ms/step for Camel against 4.76 for eager PyTorch.  A point cannot support the claim
it was attached to, because it cannot say whether the win is the tensor runtime or the fixed cost
every operator pays when a step is a few milliseconds long.  The sweep is the table the claim
needs: the same program and the same four implementations at three widths (`d_model` 64, 256,
1024), each at one thread and at one thread per physical core.

**What the widths do.**  Starting from the program's own shapes, a cell with `d_model = d` has
`s = sqrt(d / 64)` and takes `batch = 8 * s`, `sequence = 16 * s`, `d_ff = 2 * d`, `d_head = 16`,
`vocabulary = 32` and two blocks:

| d_model | batch × sequence | tokens/step | heads | d_ff | parameters |
|---|---|---|---|---|---|
| 64 | 8 × 16 | 128 | 4 | 128 | 68,288 |
| 256 | 16 × 32 | 512 | 16 | 512 | 1,059,584 |
| 1024 | 32 × 64 | 2,048 | 64 | 2,048 | 16,821,248 |

`batch` and `sequence` grow by the same factor as each other, and the token count grows with the
width, so the step's arithmetic grows as the cube of the width (×64 per 4×).  That is the honest
choice for this question, and it is stated rather than assumed: the number of operators in a step
is fixed by the program, so the fixed per-operator cost stays roughly constant per step while
the arithmetic grows.  To watch that cost stop dominating, the arithmetic has to grow with the
width *and* with the data; growing only the width would have measured a narrower claim, and
growing both axes by the full width ratio (×4 each, ×64 tokens per step) would make the largest
step 65,536× the smallest and spend hours per cell to answer the same question — the crossover,
if it is in this range, is reached long before that.  `measure.py`'s `shape_for` is this rule;
`scaled_source` generates the Camel program from `train_step.cml` by substituting exactly the
extents the cell names, counts every substitution, records the generated file's SHA-256, and the
program's own `naive config` line is checked against the cell before a timing is accepted.

**How a cell is measured.**  Each implementation runs 16 steps; the first 3 are warm-up and the
step time is the median of the other 13, measured inside the process around one forward,
backward and Adam update.  The four are measured in three interleaved rounds with a rotating
order (round *r* starts with implementation *r*); ms/step is the median of the three round
medians, and the spread is their minimum–maximum.  The float64 reference runs once per cell, for
the accuracy comparison, so its row carries whatever load that one run saw: the 3233 ms at d=256
with 16 threads is one such run, against 116 ms for the same program in the one-thread cell.
Two entries are only ordered when the gap between them is wider than both spreads; where their
round ranges overlap, the table's numbers do not order them and the verdict says so.

<!-- sweep-table:begin -->
**ms/step, one thread** (median of 3 interleaved rounds; parentheses: the round range)

| implementation | d=64 | d=256 | d=1024 |
|---|---|---|---|
| Camel (`std::jit`) | 2.085 (2.072–2.089) | 60.980 (60.167–67.704) | 3186.178 (3066.772–3268.403) |
| PyTorch naive | 4.569 (4.474–4.570) | 57.113 (56.472–62.645) | 2947.278 (2918.314–4617.991) |
| PyTorch expert | 3.803 (3.777–3.830) | 58.879 (54.927–59.230) | 2397.578 (2328.843–2415.207) |
| `torch.compile(naive)` | 2.938 (2.929–2.955) | 49.779 (49.350–50.593) | 2393.691 (2383.373–3522.431) |
| float64 reference | 5.561 (one run) | 116.069 (one run) | 6780.466 (one run) |

**ms/step, 16 threads** (median of 3 interleaved rounds; parentheses: the round range)

| implementation | d=64 | d=256 | d=1024 |
|---|---|---|---|
| Camel (`std::jit`) | 4.466 (3.714–4.616) | 42.194 (41.467–48.164) | 1897.177 (708.758–2134.288) |
| PyTorch naive | 8.087 (6.582–8.138) | 35.831 (33.301–72.997) | 473.269 (453.214–10953.000) |
| PyTorch expert | 7.669 (5.150–7.740) | 32.433 (29.952–34.261) | 398.472 (362.432–2628.657) |
| `torch.compile(naive)` | 4.943 (4.827–5.270) | 25.840 (24.102–3289.390) | 693.676 (446.420–15543.034) |
| float64 reference | 11.166 (one run) | 3232.954 (one run) | 1209.214 (one run) |

<!-- sweep-table:end -->

**The crossover.** With one thread Camel is the sole fastest implementation at d=64 (2.085 ms,
with `torch.compile(naive)` at 2.938) and it is not at d=256: `torch.compile(naive)` takes 49.8
against Camel's 61.0, a gap of 11 ms wider than both round ranges, while Camel, PyTorch naive and
the expert form sit inside each other's ranges. At d=1024 the medians keep that order
(`torch.compile` 2394, expert 2398, naive 2947, Camel 3186), but one `torch.compile` round reached
3522 and its range swallows Camel's, so by range the table orders only the expert form ahead of
Camel. With 16 threads the crossover is at the same width: Camel is the sole fastest at d=64
(4.47, `torch.compile` 4.94), and at d=256 its 42.2 ms is the highest of the four medians (25.8,
32.4, 35.8), though one round of that cell fell into a load spike and all four ranges overlap. So
the crossover is **d_model 256** at both thread counts: resolvably behind `torch.compile(naive)` at
one thread, behind by median but not separable by range at 16. There is no width in 64–1024, at
either thread count, where Camel is the sole fastest entry beyond d=64.

**The ties, and what printing them costs.** Four of the six cells print a tie group, and three tie
everything (d=256 at 16 threads, d=1024 at both). The reason is visible in the ranges: one round of
the d=256 16-thread cell took 3289 ms in `torch.compile` against 24–26 ms in the other two — the
host's load doubled mid-round — and a single spiked round widens a range until it overlaps every
other one. Printing ties as ties costs the headline twice. It refuses the sentence a median-only
table would allow ("PyTorch is 1.6× faster at 16 threads"), and it also refuses the opposite one
("Camel is still ahead at 256"): at d=256 and above Camel is not resolvably the fastest, and this
run does not bound how far behind it is. The factor between Camel and PyTorch naive at d=1024 with
16 threads is 4× by median and 1.6× by fastest round, with the load moving from 12 to 109 during
the cell; neither number measures the frameworks. What the run does support is the shape: Camel's
2.2× advantage over PyTorch naive at d=64 does not survive the first widening.

**Conditions.** Host `tangshan88`: AMD EPYC 7343, 16 physical cores and 32 hardware threads, 503
GiB RAM, Ubuntu 22.04 (kernel 6.8.0-138), Python 3.10.12, NumPy 2.2.6, PyTorch 2.14.1+cu130 (a
CUDA build; every variant ran on the CPU), Camel v0.2.24. The binary is the committed source:
fingerprint `808a7e445efc`, built 21:07:38 UTC. The build that produced the numbers was stamped
`2adf18c04` with seven dirty paths, because the compiler change it contains was committed as
`539ad3808` while the sweep ran; rebuilding that commit here gives the same fingerprint, so the
measured and the committed sources are one build. The
harness is the `measure.py` committed with this table; it is Python, and nothing in the measured
binary changed while the numbers were taken. The host is **shared**: other builds and tests ran
throughout, and the 1-minute load average over the run ranged from 12 to 109 on 32 hardware
threads; each cell's start, end and per-round samples are in the JSON, and the two d=1024 cells
saw the worst of it.

`16 threads` is one thread per physical core, not all 32 hardware threads. Asking for 32 on a
16-core SMT part measures the host rather than the frameworks: in
[`results/2026-10-23-thread-scaling-linux.json`](results/2026-10-23-thread-scaling-linux.json) a
2048×1024 matmul takes 49.4 ms at 16 threads and 47.4 ms at 32 (no gain at all), while a 64×64
matmul takes 0.0145 ms at one thread, 32.3 ms at 16 and 48.8 ms at 32 — the small operators this
model is made of pay the whole wake-up bill for every thread they are given. That run saw load 72;
its absolute values are as shared-host as the sweep's, and what reproduces is the absence of a
gain from the 32nd thread. So "all cores" is 16 here. Absolute values are not portable in any case:
the same d=64 single-thread Camel row measured 2.09 here and 2.55 in an earlier probe of this
host, and every row here is a shared-host number.

**d=1024 is a timing cell, not a convergence cell.** Every fp32 implementation leaves the finite
numbers at step 8 there: Camel and PyTorch print the same loss to five digits up to step 7
(3.531943823 and 3.531949043), and the float64 reference — same init, same learning rate —
converges to 0.25. The naive source's header says large logits overflow `exp`; that is not what
happens here (the logits stay O(1)), the fp32 trajectory itself diverges. Three of the four
implementations are therefore timed partly on non-finite values (steps 8–15 of 16). The operator
sequence does not change at the boundary and the per-step time does not either (round 2 of the
d=1024 single-thread Camel cell: 3050–3086 ms, flat across it), so the ms/step values stand; the
loss and parameter comparison against float64 does not exist for those rows, the JSON records
`diverged_at_step: 8` instead of a number, and nothing here measures how fast a *stable* fp32 step
at that size would be. The performance claim is about the arithmetic, and the arithmetic is the
same whether its values are numbers or not.

**What the sweep does not cover.**  Memory at d_model 1024 is not covered as a subject: with the
runtime's default 2 GiB block space the 2,048-token cell does not run at all (the allocation of a
33.5 MB tensor is refused with 1,911 MB in use), so `measure.py` passes `--block-limit 8192` to
every cell to keep them comparable. Peak RSS is recorded per cell — at d=1024 it is 2.8 GiB for
Camel and PyTorch naive, 2.5 for the expert form, 2.8 for `torch.compile` and 4.3 for the float64
reference — but nothing charts the allocator, finds where a cell stops fitting, or measures the
cost of the 2 GiB limit as a limit. The effect of the rules pipeline is not covered because it is
not usable end to end yet: `rules::accuracy` and `rules::fusion` exist as libraries with their own
tests, but the naive source is not rewritten into the expert form, so the "Camel + rules" row the
ruling asks for cannot be measured. The GPU is not covered; that is H49.

**The earlier numbers.** 2.062 ms/step was reported for an earlier revision and 2.314 ms/step
on a busy host.  Neither report left its machine, command or revision in the tree—this
repository contains neither number in a file or a commit message—so they are context, not a
baseline.  The single-size table's 2.091 (2.029–2.195), in
[`results/2026-10-23-linux.json`](results/2026-10-23-linux.json), is consistent with both, and
2.062 vs 2.091 is below that measurement's spread: it is not evidence of a regression.

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