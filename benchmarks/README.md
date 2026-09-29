# Camel Tensor Benchmarks

Reference harness for comparing Camel against PyTorch, TensorFlow and ONNX Runtime
on four small inference models (design record:
`docs/technical/25_tensor_stack_and_onnx_export.md`, sections 1 and 5).

This file is the **contract** the Camel implementations (`<model>/model.cml`) and
the Camel ONNX exporter must follow. The harness contributes no kernels; it only
drives implementations, checks agreement, and records measurements.

## Layout

```
benchmarks/
  run.py                    benchmark driver (CSV output; --trials N for repeated trials)
  report.py                 paper tables (Markdown, LaTeX) and SVG figures from summary CSVs
  requirements.txt          reference-side Python dependencies
  common/
    models.py               model registry: seeds, shapes, weight names (source of truth)
    weights.py              deterministic weight/input generation + loader
    reference_numpy.py      NumPy float64 reference forward for every model
    check.py                numerical agreement check of all configs
    configs.py              configuration registry (display names, report groups), skip rules, ONNX export, ORT runner
    stats.py                median, bootstrap CI, ratio CI, per-trial summaries (`--self-test`)
    worker.py               measures one (model, config) in a fresh process
    export.py               exports one model to ONNX in a fresh process
    camel.py                plug-in driver for native Camel configs
    torch_utils.py / tf_utils.py / runner.py / memory.py   shared helpers
    camel_bench.cml         shared Camel driver: weight loading, warmup/timed loops, output
  mlp/ lenet/ gru/ transformer/
    model_torch.py          PyTorch implementation
    model_tf.py             TensorFlow implementation
    model.cml               Camel implementation (imports common/camel_bench.cml)
  artifacts/<model>/        generated; git-ignored
    weights/<name>.npy  input.npy  manifest.json
    torch.onnx  tf.onnx     exported by the harness
    camel.onnx              exported by Camel (consumed by camel_onnx_ort)
  results/                  CSV outputs (only smoke.csv is committed)
```

## Global conventions

- All tensors are `float32`. Inference only, no dropout, no masks.
- **Linear**: weight `W` has shape `[in, out]`, bias `b` has shape `[out]`,
  `y = x @ W + b`. (PyTorch modules load `W.T` into `nn.Linear.weight`.)
- **Conv**: weight `[out_c, in_c, kh, kw]` (PyTorch layout), bias `[out_c]`,
  stride 1, cross-correlation (no kernel flip). Activations are **NCHW**.
  TensorFlow transposes to NHWC internally and back to NCHW before flattening.
- **Flatten** of an NCHW tensor is row-major over `(C, H, W)`.
- **MaxPool(2,2)**: kernel 2, stride 2, no padding.

## Models

### 1. `mlp` (seed 1001)

`x[64,784] -> Linear(784,256) -> ReLU -> Linear(256,128) -> ReLU -> Linear(128,10)`,
output logits `[64,10]`.

| weight | shape |
|---|---|
| `fc1_w`, `fc1_b` | `[784,256]`, `[256]` |
| `fc2_w`, `fc2_b` | `[256,128]`, `[128]` |
| `fc3_w`, `fc3_b` | `[128,10]`, `[10]` |

### 2. `lenet` (seed 1002)

`x[64,1,28,28] -> Conv(1->6, k5, stride 1, pad 2) -> ReLU -> MaxPool(2,2)`
`-> Conv(6->16, k5, pad 0) -> ReLU -> MaxPool(2,2) -> Flatten [64,400]`
`-> Linear(400,120) -> ReLU -> Linear(120,84) -> ReLU -> Linear(84,10)`, output `[64,10]`.

Intermediate shapes: `[64,6,28,28] -> [64,6,14,14] -> [64,16,10,10] -> [64,16,5,5] -> [64,400]`.

| weight | shape |
|---|---|
| `conv1_w`, `conv1_b` | `[6,1,5,5]`, `[6]` |
| `conv2_w`, `conv2_b` | `[16,6,5,5]`, `[16]` |
| `fc1_w`, `fc1_b` | `[400,120]`, `[120]` |
| `fc2_w`, `fc2_b` | `[120,84]`, `[84]` |
| `fc3_w`, `fc3_b` | `[84,10]`, `[10]` |

### 3. `gru` (seed 1003)

`x[32,16,64]` (batch, time, input), hidden `H=128`, single layer, `h0 = 0`,
written as an explicit loop over the 16 time steps with the PyTorch GRU equations:

```
r = sigmoid(x_t @ w_ir + b_ir + h @ w_hr + b_hr)
z = sigmoid(x_t @ w_iz + b_iz + h @ w_hz + b_hz)
n = tanh   (x_t @ w_in + b_in + r * (h @ w_hn + b_hn))
h = (1 - z) * n + z * h
```

Output: `Linear(128,10)` on the final `h`, shape `[32,10]`.

| weight | shape |
|---|---|
| `w_ir`, `w_iz`, `w_in` | `[64,128]` each |
| `w_hr`, `w_hz`, `w_hn` | `[128,128]` each |
| `b_ir`, `b_iz`, `b_in`, `b_hr`, `b_hz`, `b_hn` | `[128]` each |
| `fc_w`, `fc_b` | `[128,10]`, `[10]` |

`torch_nn_gru` is a **library-kernel reference**: the same weights loaded into
`torch.nn.GRU` (gates stacked in `r, z, n` order, transposed). It is not a
separate model, and `check.py` verifies that it agrees with the explicit loop.

### 4. `transformer` (seed 1004)

`x[8,64,128]` (batch, seq, d_model), one pre-LN encoder block, 4 heads
(`d_head = 32`), FFN hidden 512:

```
h = x + MHA(LN1(x))
y = h + FFN(LN2(h))
LN(x)  = (x - mean) / sqrt(var + 1e-5) * gamma + beta      over the last axis, biased variance
MHA(a) = concat_heads(softmax(Q_i K_i^T / sqrt(32)) V_i) @ o_w + o_b
         Q = a @ q_w + q_b, K = a @ k_w + k_b, V = a @ v_w + v_b
         head i owns feature columns [32*i, 32*(i+1)); softmax over the last (key) axis; no mask
FFN(f) = GELU(f @ ffn1_w + ffn1_b) @ ffn2_w + ffn2_b
GELU(x)= 0.5 * x * (1 + tanh(sqrt(2/pi) * (x + 0.044715 * x^3)))
```

Output `y` has shape `[8,64,128]`.

| weight | shape |
|---|---|
| `ln1_gamma`, `ln1_beta`, `ln2_gamma`, `ln2_beta` | `[128]` each |
| `q_w`, `k_w`, `v_w`, `o_w` | `[128,128]` each |
| `q_b`, `k_b`, `v_b`, `o_b` | `[128]` each |
| `ffn1_w`, `ffn1_b` | `[128,512]`, `[512]` |
| `ffn2_w`, `ffn2_b` | `[512,128]`, `[128]` |

## Weights and inputs

`python common/weights.py [--models ...] [--force]` writes
`artifacts/<model>/weights/<name>.npy`, `artifacts/<model>/input.npy`, and
`artifacts/<model>/manifest.json` (names, shapes, dtype, seed, file paths).

- RNG: `numpy.random.default_rng(seed)` with the per-model seed above.
- Tensors are drawn in the order of the weight tables above (the order in
  `common/models.py`), each as `rng.standard_normal(shape)` then scaled:
  weights `* 1/sqrt(fan_in)` (linear: `in`; conv: `in_c*kh*kw`),
  biases `* 0.05`, LayerNorm gamma `1 + 0.1*z`, beta `0.1*z`; cast to float32.
- The input is drawn last, standard normal, shape as listed per model.

## Configurations

| config | what runs |
|---|---|
| `torch_eager` | PyTorch eager under `torch.inference_mode()` |
| `torch_compile` | `torch.compile` (inductor, default mode) |
| `torch_onnx_ort` | `torch.onnx.export` opset 17 (TorchScript exporter, static shapes) -> ONNX Runtime |
| `torch_nn_gru` | gru only: `torch.nn.GRU` library-kernel reference |
| `tf_eager` | TensorFlow eager |
| `tf_function` | `tf.function` with a fixed input signature |
| `tf_xla` | `tf.function(jit_compile=True)` |
| `tf_onnx_ort` | `tf2onnx.convert.from_function` opset 17 -> ONNX Runtime |
| `camel_native` | `camel benchmarks/<model>/model.cml` (default `std::nvm`) |
| `camel_fvm`, `camel_jit` | same with the `std::fvm` / `std::jit` pass (opt-in; not in the default list) |
| `camel_onnx_ort` | `artifacts/<model>/camel.onnx` -> ONNX Runtime |

ONNX Runtime always uses `CPUExecutionProvider`, `ORT_ENABLE_ALL` graph
optimization, sequential execution. Camel configs are **skipped with a message**
until their prerequisites exist (`out/latest/bin/camel` plus `model.cml`, or
`camel.onnx`). `CAMEL_BENCH_BIN` overrides the Camel binary path.

### Camel native protocol

`run.py` launches `camel <abs path of model.cml> [passes]` with working directory
`benchmarks/<model>/`, `CAMEL_HOME` defaulting to `out/latest`, the thread
variables below, and:

| variable | meaning |
|---|---|
| `CAMEL_BENCH_WEIGHTS` | absolute path of `artifacts/<model>/weights/` |
| `CAMEL_BENCH_INPUT` | absolute path of `artifacts/<model>/input.npy` |
| `CAMEL_BENCH_OUTPUT` | where the model should save its output as `.npy` (enables the agreement check) |
| `CAMEL_BENCH_WARMUP` / `CAMEL_BENCH_REPS` | untimed warmup iterations / timed iterations |
| `CAMEL_BENCH_THREADS` | thread budget |

`model.cml` prints lines of the form `CAMEL_BENCH key=value ...` on stdout:

- `CAMEL_BENCH latency_ms=<float>`: one line per timed repetition (preferred),
  or one summary line that may also carry `p10_ms=` and `p90_ms=`.
- Optional: `CAMEL_BENCH compile_ms=<float> warmup_ms=<float>`.

Other stdout is ignored. Peak RSS is the Camel process's high-water mark.

### Camel ONNX protocol

The Camel exporter writes `artifacts/<model>/camel.onnx` with a single float32
input with the model's input shape, and a single output. Input and output names
are free. Weights are embedded as initializers.

The harness produces it by running the same `model.cml` with
`CAMEL_BENCH_EXPORT=<path>`: `camel_bench.run_benchmark` then calls
`onnx.export_model` on the forward function (specialized to `input.npy`'s dtype
and shape) instead of timing it, and prints `CAMEL_BENCH export_ms=<float>`,
the time spent inside the exporter. `common/export.py --exporter camel` wraps
this, so `run.py` records `export_s` for Camel as it does for torch and tf.

## Running

```bash
python3 -m venv benchmarks/.venv
benchmarks/.venv/bin/pip install -r benchmarks/requirements.txt
cd benchmarks
.venv/bin/python common/weights.py                 # generate weights and inputs
.venv/bin/python common/check.py                   # agreement check (exports ONNX if missing)
.venv/bin/python run.py --models mlp,lenet,gru,transformer --threads 4 \
    --warmup 10 --reps 50 --out results/run.csv
```

On Windows, use `.venv\Scripts\python.exe`. `run.py --help` lists all options
(`--configs`, `--interop`, `--timeout`, `--no-export`). The default config list
is every config above except `camel_fvm` and `camel_jit`.

## Repeated trials

A single run is one sample per config, so slow drift in machine load (other
processes, thermal state) can bias whichever config happened to run during a
busy period. For publishable numbers use `--trials N` (N > 1):

```bash
cd benchmarks
.venv/bin/python run.py --models mlp,lenet,gru,transformer --threads 4 \
    --warmup 10 --reps 50 --trials 10 --seed 0 --baseline torch_eager \
    --out results/paper.csv
```

- The export stage runs **once per model**, as in a single run.
- Then N rounds per model: each round measures every applicable config once
  (same fresh-process worker / Camel driver, same `--warmup` / `--reps`), in a
  round-robin order that is **shuffled per round** with `random.Random(--seed)`.
- A failed or skipped trial never aborts the others; it is recorded and counted.
- `--trials 1` (the default) keeps the single-run behavior and CSV format unchanged.

Outputs:

- `results/paper.csv`: the raw rows, the single-run columns plus `trial` (1..N)
  after `config`.
- `results/paper_summary.csv`: one row per (model, config), computed by
  `common/stats.py`:

| column | definition |
|---|---|
| `status` | `ok` (all trials ok), `partial` (some trials failed/skipped), `failed`, `skipped` |
| `trials`, `failures` | number of ok trials; number of failed trials |
| `median_ms` | median of the per-trial `latency_median_ms` |
| `ci_low_ms`, `ci_high_ms` | 95% percentile-bootstrap CI of that median (resampling trial medians, `--resamples` default 2000, seeded) |
| `min_ms`, `max_ms` | smallest / largest trial median |
| `peak_rss_mb` | mean peak RSS over ok trials |
| `export_s` | export time of the config's ONNX producer (measured once) |
| `agree` | `yes` if every ok trial agreed with the reference, `NO` if any disagreed |
| `baseline`, `speedup` | `--baseline` config; baseline `median_ms` / this `median_ms` (> 1 = faster) |
| `speedup_ci_low`, `speedup_ci_high` | bootstrap 95% CI of the speedup, resampling rounds jointly (paired) when both configs succeeded in the same rounds, independently otherwise |
| `notes` | failure count (`k/N trials not ok`) and the distinct failure reasons |

With few trials the bootstrap CI cannot extend beyond the observed range of
trial medians (with 3 trials it is essentially min..max); use 10 or more trials
for reported intervals. `python common/stats.py --self-test` checks the estimators.

## Report

`report.py` turns one or more summary CSVs into paper-ready tables and figures.
Later inputs override earlier ones for the same (model, config), so a partial
re-run can be layered over a full run. A plain single-run CSV is also accepted
(one trial per config, no CI).

```bash
cd benchmarks
.venv/bin/python report.py results/paper_summary.csv --out results/report
.venv/bin/python report.py results/paper_summary.csv results/rerun_gru_summary.csv   # layered
```

Outputs in `--out` (default `results/report/`):

| file | content |
|---|---|
| `latency.md` | rows = configs grouped by framework, columns = models, cell = `median [CI low–high]` ms, best per model in bold, `†` = some trials failed |
| `latency.tex` | the same table as a LaTeX `booktabs` table (`\usepackage{booktabs}`) |
| `memory.md` | mean peak RSS (MB), lowest per model in bold |
| `export.md` | ONNX export time (s) for the torch, tf and Camel exporters (`—` when the export was skipped, e.g. `--no-export`) |
| `latency.svg` | grouped bars per model, log-scale ms, 95% CI whiskers |
| `speedup.svg` | speedup vs the summary's baseline, log scale, bars grow from the 1x line, CI whiskers |
| `latency.png`, `speedup.png` | only if matplotlib is installed (it is not in `requirements.txt`) |

The SVGs are written directly (no plotting library). Display names and groups
(PyTorch, TensorFlow, ONNX Runtime, Camel) come from `Config.display` /
`Config.group_label` in `common/configs.py`; each group has one hue from a
colorblind-safe categorical palette and its configs are shades of that hue, keyed
by registry order so a config keeps its color across reports.

## Thread pinning

Every subprocess gets `OMP_NUM_THREADS`, `MKL_NUM_THREADS`,
`OPENBLAS_NUM_THREADS`, `TF_NUM_INTRAOP_THREADS` = `--threads` and
`TF_NUM_INTEROP_THREADS` = `--interop` (default 1). In process:
`torch.set_num_threads` / `set_num_interop_threads`, TF
`intra_op` / `inter_op` parallelism, ORT `intra_op_num_threads` / `inter_op_num_threads`.

## Metrics (CSV columns)

Each (model, config) is measured in a **fresh process**; ONNX export runs in its
own fresh process first. All times are wall clock (`time.perf_counter`).

| column | definition |
|---|---|
| `status` | `ok`, `skipped` (prerequisite missing, reason in `notes`), or `failed` |
| `export_s` | ONNX export time (framework import excluded); ORT configs only |
| `import_s` | importing the framework for this config |
| `load_s` | reading weight/input `.npy` files |
| `compile_s` | first-call overhead: build the runner + first call (tracing, `torch.compile`, XLA, ORT session creation) |
| `warmup_s` | total time of the `--warmup` calls after the first call |
| `latency_median_ms`, `latency_p10_ms`, `latency_p90_ms`, `latency_mean_ms` | over `--reps` individually timed calls; each call takes a prepared framework-native input and returns a NumPy array (output materialization included, input conversion excluded) |
| `throughput_sps` | `batch / median latency` (samples per second) |
| `peak_rss_mb` | peak resident set size of the measuring process (Camel: of the Camel process) |
| `max_abs_err`, `agree` | first-call output vs the NumPy reference, `allclose(rtol=1e-4, atol=1e-5)` |
| `notes` | skip/failure reason, or extra info (`process_wall_s` for Camel) |

## Numerical agreement

`common/check.py` runs every available configuration once and compares it with
`common/reference_numpy.py` (float64 computation, float32 result) using
`rtol=1e-4, atol=1e-5`, printing the max absolute error. It exits non-zero on any
disagreement. It hosts PyTorch and TensorFlow in one process and therefore
imports `torch._dynamo` before TensorFlow (the reverse order segfaults inside
triton with torch 2.14 / TF 2.21); `run.py` is unaffected because it isolates
every config.

## Caveats

- The PyPI `torch` wheel on Linux is the CUDA build; it runs on CPU here, but
  loading its CUDA libraries likely inflates the PyTorch configs' peak RSS.
- TensorFlow enables oneDNN custom ops by default; results still agree within
  tolerance.
