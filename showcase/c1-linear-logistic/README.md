# C1 — linear and logistic regression

This example is the smallest complete training run: tensors, `with_grad`, a `do` loop and
SGD over a parameter tree.  One program solves both problems and only swaps the loss:

- linear regression: `y = 2*x1 - 1.5*x2 + 0.5` with a little noise;
- logistic regression: two Gaussian clusters classified by `sigmoid(x @ w + b)`.

C2 is the "parameter tree + optimizer state + checkpoint" example; C1 is deliberately smaller.
The model has two tensors, `w` and `b`, the optimizer is `nn.sgd`, and the reader can see the
whole "gradient -> update -> next step" line without the machinery.

## Layout

```
showcase/c1-linear-logistic/
├── train.cml                 the Camel program (both problems share one training loop)
├── baseline/train_torch.py   the PyTorch counterpart (nn.Linear + SGD)
├── make_data.py              synthetic data and zero initial weights
├── compare.py                runs both, compares, measures, writes RESULTS.md
├── RESULTS.md                measured results of the standard profile (generated)
├── README.cn.md
└── README.md
```

## Data

`make_data.py` uses NumPy only and a fixed seed:

- `linear/`: 512 training and 128 validation rows, `train_x/val_x` shaped `(N, 2) f32`,
  `train_y/val_y` shaped `(N, 1) f32`;
- `logistic/`: the same shapes, labels 0/1; the two clusters are centred at `(-1, -1)` and
  `(1.2, 1.2)`;
- both models start from `w = 0`, `b = 0`, so the initial loss is fully determined by the data;
  both sides read the same `.npy` files.

Nothing is downloaded, and the `.npy` files are not committed (`artifacts/` is ignored).

## Running

```powershell
python showcase/c1-linear-logistic/make_data.py --out showcase/c1-linear-logistic/artifacts/small
$env:CAMEL_HOME = "out\latest"
$env:PATH = "$env:CAMEL_HOME\bin;$env:PATH"
camel showcase/c1-linear-logistic/train.cml --arg model=linear `
    --arg data=showcase/c1-linear-logistic/artifacts/small/linear `
    --arg steps=200 --arg batch=32 --arg lr=0.1 --arg log_every=20 --arg threads=1 `
    std::macro std::nvm

python showcase/c1-linear-logistic/baseline/train_torch.py `
    --data showcase/c1-linear-logistic/artifacts/small/linear --model linear `
    --steps 200 --batch 32 --lr 0.1 --log_every 20 --threads 1
```

`compare.py` chains both steps and also runs `logistic`:

```powershell
python showcase/c1-linear-logistic/compare.py --profile small
python showcase/c1-linear-logistic/compare.py --profile standard   # writes RESULTS.md
```

## Results (standard, 1000 steps, one thread)

| problem | initial loss diff | worst logged step | final loss diff | final metric |
|---|---|---|---|---|
| linear | 5.8e-08 | 1.1e-06 | 3.4e-07 | MAE 0.043818 vs 0.043818 |
| logistic | 0 | 1.2e-02 (bound 2e-02) | 6.5e-04 | accuracy 0.9922 vs 0.9922 |

Training time: linear 0.0054 s vs 0.119 s, logistic 0.0082 s vs 0.128 s; peak memory 8.0/11.0 MiB
vs 273.2/273.9 MiB; compile 0.034 s.  The full tables (throughput, code lines) are in
`RESULTS.md`.

**Do not extrapolate the throughput ratio.**  The model has three parameters and the data is
512×2, so PyTorch's time is dominated by per-step framework and autograd overhead while
Camel's is dominated by the interpreter loop.  These numbers say "framework overhead dominates
on a tiny model", not "the Camel kernels are faster".  The C2 MLP (784→128→10) is where compute
dominates, and there Camel is 17–21% slower.

## Why these tolerances

- The initial loss differs only in the order of the dot product and the bias add: `1e-05`
  relative.
- Each logged loss is amplified by the optimizer: logistic regression at `lr = 0.05` turns a
  per-step difference of order 1e-7 into 1e-2 within tens of steps, so logged steps are bounded
  at `2e-02` relative (measured worst: 1.2e-02).
- The final loss uses `1e-02` relative and the final metric (MAE / accuracy) `1e-02` absolute.
- The two runs are not bit-identical, and cannot be: design principles 7 promises bit-for-bit
  equality only for the same algorithm on the same back end.

`compare.py` exits non-zero when a bound is exceeded, so a failing plan entry means a real
numeric disagreement.

## Side by side

| thing | Camel | PyTorch |
|---|---|---|
| model | `type Model = { w, b: Tensor }` | `nn.Linear(2, 1)` |
| loss | hand-written formula reduced with `mean` | `F.mse_loss` / `F.binary_cross_entropy_with_logits` |
| gradient | `with_grad(loss)<m>(x, y, kind)` | `value.backward()` |
| update | `sgd(m, gradient, lr)`, returns a tree of the same type | `optimizer.step()` |
| loop | `loop m = initial, step = 0 while ... do sync { ... }` | `for step in range(steps)` |
| printing | backtick interpolation `` `c1 step=${step} loss=${value:.9f}` `` | f-string |

### Differentiation spelling

Design principles 9.9 (H21 item 3 was withdrawn; see H25): weights are `with` parameters, and
`with_grad(loss)<m>(x, y)` differentiates the first `with` parameter, returning a gradient of
the same type as `m`; the remaining `with` parameters are constants and ordinary parameters are
data.  This example binds `with_grad(loss)` outside the loop and calls it with the weights
inside.

## Problems found

- Nothing new blocked C1.  It only needs tensors, `grad`, `sgd` and the `do` loop, all present;
  the gaps recorded for C2 (`save_tree`, `take`, cross-entropy targets) do not apply here.
- `sgd` accepts a parameter tree directly, like `adam`; no per-leaf loop is needed.  This is
  the most comfortable part of the example.
- The logistic loss is written in the stable form `relu(z) - z*y + log(1 + exp(-|z|))`; the
  `exp(log(...))` path differs from PyTorch's `log1p` path by more than a fused kernel would,
  which is one reason the logged-step bound is twice C2's.  This is a numeric-path choice, not
  a language problem.

## Tests

`test/plans/perf/showcase_c1.plan.toml` (`tier = "milestone"`) runs `--profile small`: 512 rows,
200 steps, batch 32, about five seconds including both PyTorch imports.  It fails when a
numeric bound is exceeded.

## Not committed

`artifacts/`, `*.npy` and `__pycache__/` are ignored; `RESULTS.md` is committed because it is
the generated, checkable record of the measurement.
