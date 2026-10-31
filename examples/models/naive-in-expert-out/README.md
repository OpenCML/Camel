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
**ms/step, one thread, before the library step** (median of 3 interleaved rounds; parentheses: the round range)

| implementation | d=64 | d=256 | d=1024 |
|---|---|---|---|
| Camel (`std::jit`) | 2.085 (2.072–2.089) | 60.980 (60.167–67.704) | 3186.178 (3066.772–3268.403) |
| PyTorch naive | 4.569 (4.474–4.570) | 57.113 (56.472–62.645) | 2947.278 (2918.314–4617.991) |
| PyTorch expert | 3.803 (3.777–3.830) | 58.879 (54.927–59.230) | 2397.578 (2328.843–2415.207) |
| `torch.compile(naive)` | 2.938 (2.929–2.955) | 49.779 (49.350–50.593) | 2393.691 (2383.373–3522.431) |
| float64 reference | 5.561 (one run) | 116.069 (one run) | 6780.466 (one run) |

**ms/step, 16 threads, before the library step** (median of 3 interleaved rounds; parentheses: the round range)

| implementation | d=64 | d=256 | d=1024 |
|---|---|---|---|
| Camel (`std::jit`) | 4.466 (3.714–4.616) | 42.194 (41.467–48.164) | 1897.177 (708.758–2134.288) |
| PyTorch naive | 8.087 (6.582–8.138) | 35.831 (33.301–72.997) | 473.269 (453.214–10953.000) |
| PyTorch expert | 7.669 (5.150–7.740) | 32.433 (29.952–34.261) | 398.472 (362.432–2628.657) |
| `torch.compile(naive)` | 4.943 (4.827–5.270) | 25.840 (24.102–3289.390) | 693.676 (446.420–15543.034) |
| float64 reference | 11.166 (one run) | 3232.954 (one run) | 1209.214 (one run) |

<!-- sweep-table:end -->

**After the shape-dependent library step (measured at `9aa8075dc87b`; final build after the
gradient-code cleanup is `7e57f010a6b3`).** Camel now runs
`tensor::fuse std::jit`. The pass records `tensor.kernel` on `matmul` and `linear`; the VJP
carries that choice to `matmul_grad_lhs`/`matmul_grad_rhs`, and the library path itself keeps the
canonical kernel below 2^30 multiply-adds per call. So the large projection gradients use the
library while the small batched attention products stay canonical. The re-run at one thread, same
host and same harness:

| ms/step, one thread | d=64 | d=256 | d=1024 |
|---|---|---|---|
| Camel before (canonical) | 2.085 | 60.980 | 3186.178 |
| Camel after the library-only step | 2.614 | 66.263 | 3599.913 |
| Camel after the shape step | 2.100 | 66.133 | 2570.427 |
| `torch.compile(naive)` before | 2.938 | 49.779 | 2393.691 |
| `torch.compile(naive)` after | 2.931 | 50.030 | 2772.492 |

The interleaved canonical/library pair for the same three cells (three rounds each, one thread,
same process runner alternating the two programs; medians):

| ms/step, one thread | canonical | library | delta |
|---|---|---|---|
| d=64 | 2.121 | 2.133 | +0.6% (noise) |
| d=256 | 63.37 | 63.45 | +0.1% (noise) |
| d=1024 | 3236 | 2544 | -21% |

The library pays only at d=1024, where the projection gradients are large enough; at d=64 and
d=256 every product falls below the threshold and the two rows are the same kernel. The paired
profile and the per-category rows are in
[`results/2026-10-23-d1024-profile-shape.json`](results/2026-10-23-d1024-profile-shape.json).
d=256 remains
behind: 66.1 against 50.0 (32%), with non-overlapping round ranges. At d=1024 the medians put
Camel ahead of `torch.compile` this run (2570 against 2772), but the compiled range is
2424–5466, so the numbers do not order the two. The d=1024 library-only cell was 3600 ms at load
31; the shape step is 2570 ms at load 14–35, and the paired row shows the difference is the path,
not only the host. The 16-thread cells are in
[`results/2026-10-23-sweep-linux-shape.json`](results/2026-10-23-sweep-linux-shape.json); there
Camel is 1146.6 against compiled 369.4 at d=1024, because the library pool is capped at four
threads and the canonical kernels take the rest of the run.

**The same three cells in CPU time, with the system CBLAS the default.** The rows above are wall
times, and the host they were taken on reports load averages in the hundreds, so the paragraph's own
warning applies to them. This scan re-measures the same workloads — `measure.py`'s shapes, generated
source and config check — with the process's CPU seconds (user + system) as the currency: each step
is the slope of CPU seconds against step count over two run lengths, so start-up and preemption
cancel. Run 2026-10-23 11:44–12:52 +08:00, build `102dbc5d1` group (fingerprint
`a39d2d4c570a`, `CAMEL_TENSOR_BLAS=ON`, OpenBLAS 0.3.20), load average 97–128 on 32 hardware
threads. `camel-library` is the default path (`tensor::fuse`); `camel-canonical` is the same program
with the pass removed. Both figures of every cell are given in
[`results/2026-10-23-cpu-scale-scan.json`](results/2026-10-23-cpu-scale-scan.json):

| CPU ms/step, one thread | library (default) | canonical | library ÷ canonical |
| --- | ---: | ---: | ---: |
| d=64 | 2.701 (2.254, 2.701) | 2.612 (2.612, 2.589) | 1.03 |
| d=256 | 62.292 (58.750, 62.292) | 61.615 (61.198, 61.615) | 1.01 |
| d=1024 | 2 656.9 (2 656.9, 2 576.9) | 2 780.3 (2 780.3, 2 703.1) | 0.956 |

The library pays only at d=1024 — 4.4% — and the reason is the 7.3 threshold, not a measurement of
the library at the small sizes: every product at d=64 and d=256 is below `kLibraryMinProducts`
(2^30 multiply-adds a call), so `sgemmLibrary` runs the canonical kernel there and the two columns
are the same kernel. At one thread the recorded paired row for this host was 3236 → 2544 (−21%); the
column above is −4.4%, so the library's advantage at d=1024 has narrowed to about a fifth of what it
was when it was introduced. That comparison carries the post-sweep code changes in both columns, so
it measures the default against the default, not the library against the canonical kernel; what it
does bound is that a 21% library effect is no longer reproducible on this host. The 16-thread cells
(2 656 → 3 819 CPU ms/step at d=1024 for the same step that costs 27.0 ms at d=64) are in the same
file and are **not** usable: the worker threads spin under the load, so 16-thread CPU seconds count
preemption as work, and the two kernels come out within 2% of each other there while the one-thread
pair separates them by 4.4%. A 16-thread CPU figure needs an idle host and a run of its own.

**The d=64 CPU-time gap is the host, not a regression.** The paragraph above leaves 2.612–2.701
CPU ms/step next to the 2.085 wall ms of the sweep table, and the question that leaves is whether
the current build grew. It is decided in retired instructions — `perf stat -e instructions:u`, the
slope against step count over 64 and 512 steps so start-up, JIT warm-up and the parameter save
cancel — because that counter measures the same work whatever the neighbours do. Both builds ran the
same generated program in one window at load 87–112, with `std::jit` and no `tensor::fuse`, so both
are on the canonical kernels: the recorded one (fingerprint `808a7e445efc`, rebuilt from
`539ad3808`, the tree the sweep table was measured on) and the current one (`0ad92b961`, fingerprint
`2ad1ad1d1e0e`).

| slope, one thread | recorded build | current build | ratio |
| --- | ---: | ---: | ---: |
| d=64 instructions/step | 23 446 507 | 20 430 096 | 0.871 |
| d=256 instructions/step | 718 900 802 | 620 906 080 | 0.864 |
| d=64 CPU ms/step (task-clock) | 2.477 | 2.363 | 0.954 |
| d=256 CPU ms/step (task-clock) | 65.46 | 63.02 | 0.963 |

The current build retires **12.9% fewer** instructions a step at d=64 and 13.6% fewer at d=256: the
opposite direction from a regression, and thousands of times the counter's own scatter (three
repeats of one slope agree to 0.005% at d=64 and 0.0001% at d=256). The 25% is the window: the
recorded d=64 cell ran at load 15.6–20.3, and the same tree measures 2.477 CPU ms/step here at load
87–112. Adding 96 spinners (load 87–112 → 136–138) left the instruction slope at 1.000000 of its
ambient value, while task-clock went from 2.363 to 2.321, inside the 1–4% scatter of its own repeats
(11% across the recorded build's three). A control on the same tree, built with and without the
system CBLAS, moves the count by 0.002%, so the option is not a confound. **No recorded number was
changed.** Both build pairs, the load pair and the control are in
[`results/2026-10-24-d64-instruction-counts.json`](results/2026-10-24-d64-instruction-counts.json).

**The d=1024 gap against `torch.compile`, and why it no longer divides.** On the same CPU basis,
`torch.compile(naive)` costs 2 645.0 ms a step (CPU seconds 80.71 at 24 steps and 165.35 at 56, its
`torchtrace.py` profile under the same two-length slope), against the library path's 2 656.9 — 0.4%
apart. The recorded profile had Camel at 3 511 wall ms against 2 664, an 847 ms gap; that was a wall
measurement of an earlier build and a different currency, so the narrowing is not attributed here to
either change alone, but it is real in both currencies this scan can check (the wall medians of the
same runs, which are in the result file, are 5 009–6 143 ms for Camel's long runs and 5 418–6 276 ms
for torch's, inside each other). The two matmul rows profiled inside those two runs, per step,
however, are 4 395 ms against 3 036 ms for `tensor:matmul` and 3 273/3 297 against 2 001/1 988 for
the two gradient rows, and the derived attributions of the 12 ms gap do not reconstruct it (the
difference of the two decompositions is hundreds of milliseconds). The recorded
`matmul_share_of_gap: 0.992` was taken when the step gap was 847 ms; at a 12 ms gap the attribution
has no resolution, so this scan does not confirm 99% and does not replace it with another share.
What it establishes is the size of the thing that used to be 99%: the gap itself is now inside the
run-to-run spread. The per-run sanity check that the profile rows sum to the profiled round's own
mean came out at 1.22 (library) and 1.21 (canonical), so the absolute row values carry that much
inflation too.

**Where the d=256 gap is not: allocation.** `std::profile` now separates the allocator from the
operator that called it: every operator row carries the time spent inside the Camel value
allocators, the allocator calls, the data blocks among them and their payload bytes, and the step
block gives the same per round. Measured 2026-10-23, one thread, the same generated step: at d=256
one round allocates 362 block payloads (1,499 allocator calls, 137.9 MiB) and spends 0.68 ms in
the allocators — 0.40% of the step and 0.37% of the operator time (362 includes the four blocks the
`gelu_grad` kernel has since stopped allocating; the budget file below counts 358). The three matmul
rows spend
0.075% of their time there and allocate exactly one block per call, the output, so they allocate
no packing buffer either. At d=1024 it is 1.34 ms of a 6.27 s step (0.02%). The host carried load
60 on 32 threads and the absolute times are inflated (171 ms here against the 66 ms of the table
above); the shares are ratios from the same run and do not depend on that. A 34% gap on 66.1 ms
is about 22 ms, roughly 33 times the allocator time of a whole step, so the d=256 gap is kernel
time, not allocation. What this still cannot split is a `memcpy` inside a kernel, a BLAS
library's own `malloc`-ed packing buffer, and the page faults of first touching a newly reserved
segment; the file is
[`results/2026-10-23-d256-d1024-alloc-profile.json`](results/2026-10-23-d256-d1024-alloc-profile.json)
and the instrument is described in
[`docs/technical/54_profiler_allocations.md`](../../../docs/technical/54_profiler_allocations.md).

**Where one d=256 step goes.** The paragraph above says the d=256 gap is not the allocator. This one
says which rows it is, with each row measured rather than attributed by subtraction:
[`test/cases/perf/step_budget.py`](../../../test/cases/perf/step_budget.py), `measure` mode, results
in [`results/2026-10-23-d256-step-budget.json`](results/2026-10-23-d256-step-budget.json).

The host makes the naive method fail first. `std::profile` times operators with `steady_clock`, and
the same 60 ms step reports 75 to 148 ms of wall time in a round depending on who else is building
here, so a row's wall milliseconds measure the neighbours too. The budget therefore takes the step's
CPU time as the slope of a run's CPU seconds against its step count (32 and 128 steps, three runs
each, median — start-up cancels), and gives each row its share of one profiled run of the same
program times that CPU step. One row does not come from the profiler: `test/tools/gemmprobe`
measures every projection shape of the step on its own, one thread, in CPU time, which is also the
one independent check on the normalisation — its sum lands 2.9% from the profiler's three matmul
rows. One step, 60.2 ms of CPU time:

| row | ms | share |
| --- | ---: | ---: |
| matmul, projections (39 calls, per-shape probe CPU time) | 33.52 | 55.7% |
| matmul, attention products (3072 micro products of 32x32x16) | 4.61 | 7.7% |
| elementwise, backward (multiply 3.58, add 1.63, neg 1.23, divide 1.18, gelu_grad 0.83) | 8.45 | 14.0% |
| elementwise, forward (add 1.01, multiply 0.94, exp 0.72, gelu 0.62, divide 0.56, log 0.02, sqrt 0.01) | 3.88 | 6.4% |
| reductions (sum_to 3.20, mean_axis 0.38, sum 0.29, sum_axis 0.24) | 4.11 | 6.8% |
| layout (expand_axis 1.29, permute 1.08, permute_inverse 0.70, transpose 0.30) | 3.39 | 5.6% |
| optimizer (nn:adam) | 1.87 | 3.1% |
| gradient assembly (scatter_add_arr 1.13, zeros_arr 0.10) | 1.23 | 2.0% |
| allocation (inside every row above; reported once here) | 0.23 | 0.4% |
| residual | -1.07 | -1.8% |

The JSON has the 36 operator rows it measured, their shares, the per-shape probe rates and the three
phase profiles; the rows sum to 99.9% of the profiled round mean and the residual is -1.8% of the
step. The largest term in it is nameable: that same 2.9% between the probe's by-shape matmul and
the profiler's three matmul rows once both are carried onto the CPU step, and the allocator counted
once as a row of its own (0.4%) beside the operator calls that made it. Dispatch has no row of its
own: the same graph at 1x1 tokens runs a round of 6 to 17 ms depending on the host, and its operator
rows account for all of it in both cases (6.0 of 6.0, 17.2 of 16.9), so the interpreter's time
between operator calls is inside that noise rather than a fifth of the step. The step's CPU time is
60.2 ms here against 64.9 in the file this one replaces, and the rows are read against the step in
their own file: the difference is the host, not a change in the step.

**What is not fixable.** The matmul row is two thirds of the step and it is at parity: at the
projection shapes the canonical kernel runs at 0.94-1.01 of OpenBLAS's rate in the same process, on
the same core, at the same thread count (`vs_blas` in the JSON, recorded per shape). The attention
products (4.61 ms) are 12 batched calls a step — four forward, eight backward — each 256 micro
products of 32x32x16 at 22 GFLOP/s; the cost is per micro product (1.45 us for 16K MACs), not per
call, so this is a small-GEMM question, not a dispatch one, and batching the calls further is
already done. OpenBLAS gets 28 GFLOP/s at the same shape, so the shape, not the kernel's author, is
what binds.

**The one change: `tensor:divide` ran a switch per element.** In
[`modules/tensor/kernels/elementwise.cpp`](../../../modules/tensor/kernels/elementwise.cpp)
`binaryTyped` gave `+`, `-` and `*` their own row loop and sent every other operator through one
`default:` that handed the runtime `op` to the inner loop, so `x / y` ran a switch and a scalar
division per element while the comment above the function promised the operator was hoisted out.
Each operator now gets its own loop with the operator a compile-time constant. Against the same
torch call, in the same window, one thread, [512, 256] (`measure --torch`; the torch side is the same
operator on the same shape and thread count): 215 us before, 63 us after, with torch at
51 and 46 us in those two windows — from 4.2x slower than torch to 1.2-1.4x. Inside the step the
divide row went from 4.22 to 2.11 ms (its ratio to the matmul row, which is the in-run anchor,
halved), 3.3% of the step. The step's own CPU time (64.1 ms before, 64.9 ms after, and one run of
the after code at 59.6 ms) does not resolve a 2 ms change on this host; the row and the per-call
measurement do.

**The other change: `tensor:gelu_grad` wrote three tensors a call.** The gradient of `gelu` is
`dy * gelu'(x)`, and the kernel computed it the way the operator chain that defines it writes it:
the polynomial `x + 0.044715 x^3` into one temporary, its `tanh` into a second through a `unary`
pass, then the slope and the product. At the step's own width — two calls of 262,144 elements a
step — that was three allocator blocks of 1,048,576 bytes a call, and four traversals of the
inputs and the two temporaries. It is one loop now that reads `x` and `dy` once and writes the
output once. The exact part of this is a count, and it is in the file above: one round of the step
allocates 362 block payloads before and 358 after, in the full and in the forward+backward build,
and 85 in both forward-only builds, which run no `gelu_grad` — two calls times the two temporaries
that are gone. `step.elementwise` asserts it as a property, not a duration: every element-wise
operator of the step allocates exactly one block per call, its output, and it fails on the kernel
this one replaces (3 blocks and 3,145,728 bytes a call against 1 and 1,048,576).

**How much time that is, this host cannot say.** Three attempts, all on the same machine: the
`gelu_grad` row of the same program's profile read 1,081 us a call before and 1,005 after, with the
row's own repeat agreeing to 0.2% and 2.6%, but the rows the change did not touch repeated 25% and
40% apart across the two sessions, so the pair is a few percent at best; the whole
`grad(loss)<x>()` call from the slope of CPU seconds against round count — the one instrument here
that the load does not enter — read 937 us a round before and 977 after against its own repeat
spread of 2 to 12%, and it carries the unchanged forward pass and the graph, which is why it cannot
see a few percent of one row; and the two JSONs' normalised rows (1.22 and 0.83 ms) are not a
before/after pair at all, because their CPU steps differ by 7% and every row in each file is read
against its own. What the change removes is exact — two blocks of 1,048,576 bytes and about 5 MB of
traffic a call, 4 blocks and 10 MB a step — and what it buys in time is a tenth of a millisecond at
most, under 0.2% of the step, below what this host resolves.

**The change moves no bits.** The fused gradient's storage bits, over 1,155 to 262,144 elements, are
the three-pass chain's shape for shape, in the default unit and under `--tensor-isa generic`:
the plan pins them (`gelu_grad.bits`, `gelu_grad.bits.generic_unit`). That is why the loop is
compiled per instruction set (`gradient_loops.inl` included by `gradient_generic.cpp` and
`gradient_avx2.cpp`, the shape `update_*` already has) rather than written once in
`gradient.cpp`: the tangent is `vmath::tanh`, the function the unary kernel inlines, which the
unary units compile with contraction on, while `gradient.cpp` carries the project's
`-ffp-contract=off` and the baseline target, so the same expression cannot contract there — a
single portable unit would move the tangent's bits and run it four wide. The two units are not identical to each other on the large shape: the
tangent's contraction differs on 0.062% of elements, by at most 1.2e-7 over 20M inputs from the
range the harness feeds, which is inside `vmath.h`'s declared error for `tanh` and is the
declaration the unary units already carry. Hence two pinned digest sets and not one.

**What could not be measured.** Absolute wall times: this file's table was taken at load average
63-76 on 32 hardware threads, and the profiled round mean of the same step moved between 78 and
148 ms from one run to the next. The attention
product's cost on the step's own data: the probe runs the shape in isolation, its operands warm, so
its 22 GFLOP/s is an upper bound for the step's interleaved calls. The `memcpy` inside a kernel, and
the page faults of first touching a reserved segment, are inside the rows but below the instrument.
The allocator count has no counterpart in torch (its allocator is not instrumented), so no
like-for-like is claimed for it.

**The crossover.** In the before table above, with one thread Camel is the sole fastest implementation at d=64 (2.085 ms,
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
CUDA build; every variant ran on the CPU), Camel v0.2.24. The numbers were measured on a binary
with fingerprint `808a7e445efc`, built 21:07:38 UTC from `2adf18c04` plus this directory's
`is_numeric` change (that change was committed as `539ad3808` while the sweep ran, and rebuilding
that tree gave the same fingerprint). The tree has since merged develop (`bf6fe3a54`), and the
tree as committed here rebuilds to fingerprint `8d3a616b7faa`: these are measurements of the
earlier build, and no row was re-measured after the merge. The after table above is the later
build `b2e4abd0e50d` (the library step); it is not a re-measurement of this table. The
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
reference — but nothing finds where a cell stops fitting, or measures the cost of the 2 GiB limit
as a limit; the allocator column of `std::profile` (above) measures what a step allocates, not
what a cell holds. The effect of the rules pipeline is not covered because it is
not usable end to end yet: `rules::accuracy` and `rules::fusion` exist as libraries with their own
tests, but the naive source is not rewritten into the expert form, so the "Camel + rules" row the
rule asks for cannot be measured. The GPU is not covered.

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

- **Priority gap: tensors still keep one storage type.** The public tensor element types are
  effectively f32, i64 and bool; f64 is normalised to f32 by `normalizeTensorDType` on creation
  and cast, and i32 to i64.  This paragraph is provisional: when tensor element types are separated, delete it and add an
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
  The example writes shaped `f32[8, 16, 64]` (and named aliases `TokenStates`,
  `HeadStates`, ...), so every signature and every field shows its extents.  Dimension names
  inside `f32[b, t, d]` do not bind (the checker warns `DimensionNameUsedOnce`), so the
  extents are written as literals and named by the aliases and the comment at the top.
- **No dtype conversion written as a type.**  `f32(oh)` has no tensor overload, and
  `oh as f32[...]` is a `DynamicCastForbidden`; the one-hot product uses numeric promotion
  (f32 * int64 gives f32) instead of the old `f32(oh)` string.  A typed tensor cast
  is still a gap.
- **No fused QKV/attention operator and no `rules::accuracy`/`rules::fusion` pipeline**, so the
  expert form is not produced from the naive source.  `softmax`, `log_softmax` and the fused
  cross-entropy exist as operators and are used by the PyTorch expert and the accuracy probes.
- **`explain` (the rule-rewrite chain) is available now** (`run`/`explain` in `stdlib/rewrite.cml`), but this directory does not show it yet.