# C6 — variational autoencoder

A minimal VAE: a 2-D point goes through a 16-wide hidden layer, two heads produce the mean and
the log-variance, a reparameterized sample goes through the decoder, and the loss is
reconstruction plus the closed-form KL.  It is here to show two things:

1. **Reparameterization**: `z = mu + exp(0.5 * logvar) * eps`; sampling is ordinary tensor
   arithmetic on `eps`, so the gradient flows through it;
2. **Two heads sharing an encoder**: `mu` and `logvar` are two linear outputs of the same hidden
   layer, so the parameter tree has four tensors and the gradient tree has the same shape.

The model has 198 parameters and the data is synthetic 2-D points, so the whole
encode-sample-decode-KL path runs in seconds and can be lined up against PyTorch.

## Layout

```
showcase/c6-vae/
├── train.cml                 the Camel program
├── baseline/train_torch.py   the PyTorch counterpart
├── make_data.py              four Gaussian clusters, initial weights, fixed epsilons
├── compare.py                runs both, compares, measures, writes RESULTS.md
├── RESULTS.md                measured results of the standard profile (generated)
├── README.cn.md
└── README.md
```

## Data and randomness

`make_data.py` uses a fixed seed to draw four 2-D Gaussian clusters (centres `(-2,-1)`,
`(1.5,2)`, `(-1.5,2)`, `(2,-2)`), 1024 training and 256 validation rows, plus 40000 rows of
standard-normal `eps` that both sides slice per step.

**Why the epsilons are in the data**: the language currently has only the effectful
`random::randn` that advances a global state, with no keyed random numbers.  Sampling inside the
program would give the two runs different noise and the PyTorch comparison would be meaningless.
Keyed random numbers add `random::key(seed)` / `split` / `permutation` (Philox, pure); after it lands the loop
will split a key per step instead of reading the file.  The README and the report record this
dependency.

## Running

```powershell
python showcase/c6-vae/make_data.py --out showcase/c6-vae/artifacts/small
$env:CAMEL_HOME = "out\latest"
$env:PATH = "$env:CAMEL_HOME\bin;$env:PATH"
camel showcase/c6-vae/train.cml --arg data=showcase/c6-vae/artifacts/small `
    --arg steps=200 --arg batch=32 --arg lr=0.005 --arg log_every=20 --arg threads=1 `
    std::macro std::nvm
python showcase/c6-vae/compare.py --profile small
python showcase/c6-vae/compare.py --profile standard   # writes RESULTS.md
```

## Results (standard, 1000 steps, one thread)

| metric | Camel | PyTorch | difference |
|---|---|---|---|
| initial validation loss | 7.212991182 | 7.212991089 | 1.3e-08 |
| worst logged loss (step 800) | 2.647558995 | 2.647559404 | 1.5e-07 |
| final validation loss | 1.996342750 | 1.996342555 | 9.8e-08 |
| final reconstruction MSE | 0.213992766 | 0.213992611 | 7.2e-07 |
| final KL | 1.782349985 | 1.782349944 | 4.1e-08 |

Training time 0.034 s vs 1.629 s, peak memory 18.7 MiB vs 274.8 MiB, compile 0.046 s.  The full
tables are in `RESULTS.md`.

**On performance**: the model has 198 parameters, so PyTorch's per-step time is dominated by
Python, autograd and optimizer overhead while Camel's loop runs in C++.  The 0.021 ratio says
"framework overhead dominates on a tiny model"; it is **not** "the Camel kernels are 40x
faster".  The C2 MLP (784→128→10) is the compute-dominated comparison, and there Camel is
17–21% slower.  The memory ratio has the same caveat: the 274.8 MiB is mostly the PyTorch
runtime.

## Tolerances

- Initial loss: `1e-05` relative (only the order of the matrix products and bias adds differs).
- Every logged loss, the final loss and the reconstruction MSE: `1e-02` relative.  The measured
  differences are below 1e-6 because the model is small and both sides do the same Adam bias
  correction.
- Not bit-identical: design principles 7 promises bit-for-bit equality only for the same
  algorithm on the same back end.

`compare.py` exits non-zero when a bound is exceeded.

## Side by side

| thing | Camel | PyTorch |
|---|---|---|
| encoder | `relu(x @ ew1 + eb1)` then two heads | two `nn.Linear`s and a hidden layer |
| sampling | `mu + exp(0.5 * logvar) * eps` | the same expression |
| KL | `-0.5 * mean(sum(1 + logvar - mu² - exp(logvar), 1))` | the same expression |
| update | `adam(m, gradient, opt, lr)`, optimizer state in the loop | `optimizer.step()` |
| loop | `loop m, opt, step ... do sync { ... }` | `for step in range(...)` |

### Differentiation spelling

Design principles 9.9: `with <m: Vae>` is the final
spelling, and `with_grad(loss)<m>(x, eps)` differentiates the first `with` parameter, returning
a gradient of the same type as `m`; the other parameters are data.  The example binds
`with_grad(loss)` outside the loop.

## Problems found

- **No keyed random numbers**: worked around with a pre-generated epsilon file; keyed random numbers should
  replace it.  This is not a pattern to copy, only the only way today to guarantee identical
  noise on both sides.
- **No checkpoint**: atomic replacement of `save_tree` is unlanded, and C2 already hit the
  "directory exists" error.  C6 does not need a checkpoint, so it does not repeat the
  workaround; saving VAE parameters waits for the atomic replacement to land.
- Sampling uses `exp(0.5 * logvar)` rather than a separate standard-deviation head, one tensor
  fewer; a spelling choice with no semantic effect.

## Tests

`test/plans/perf/showcase_c6.plan.toml` (`tier = "milestone"`) runs `--profile small`: 1024 rows,
200 steps, batch 32, about six seconds for the whole plan (mostly the two PyTorch imports).  It
fails when a bound is exceeded.

## Not committed

`artifacts/`, `*.npy` and `__pycache__/` are ignored; `RESULTS.md` is committed.
