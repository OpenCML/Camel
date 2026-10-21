# C2: training an MLP on MNIST

The first example of the showcase batch (H13). The same task is written twice: once in Camel
(`train.cml`) and once in the idiomatic PyTorch a practitioner would write
(`baseline/train_torch.py`). Both read the same data and the same starting weights, train the
same 784 → 128 → 10 MLP (ReLU, Adam, softmax cross-entropy), evaluate on the same validation
set, and are compared against one set of tolerances. Every number is produced by
`compare.py`; see [RESULTS.md](RESULTS.md). The Chinese README is
[README.cn.md](README.cn.md).

## Layout

```
showcase/c2-mlp-mnist/
  train.cml                the Camel implementation (parameter tree, with_grad, take sampler, loop-collected curve, save_tree)
  baseline/train_torch.py  the PyTorch counterpart (nn.Linear, F.cross_entropy, torch.optim.Adam, state_dict)
  make_data.py             writes the data and the starting weights (MNIST or a deterministic stand-in)
  compare.py               runs both sides, compares, measures, writes RESULTS.md; exits non-zero on failure
  RESULTS.md               the latest `--profile standard` run (generated, do not edit)
  README.cn.md             Chinese README
  README.md                this file
  artifacts/               generated data and checkpoints (gitignored, not committed)
```

## Data

`make_data.py` writes the data and the starting weights both sides share. By default they
land in `showcase/c2-mlp-mnist/artifacts/<profile>/` (gitignored; `--data <dir>` puts them
elsewhere), and `compare.py` regenerates them before each run when they are missing or too
small. To write them without running the comparison,
`python showcase/c2-mlp-mnist/make_data.py --out <dir>`.

- `train_images.npy` / `val_images.npy`: float32 `(N, 784)`, pixels in `[0, 1]`;
- `train_labels.npy` / `val_labels.npy`: int64 class indices, the form both
  `F.cross_entropy` and `nn.softmax_cross_entropy` take;
- `train_order.npy`: one epoch permutation after another, flat. The batch sampler of both
  implementations walks this order, so a step sees the same rows on both sides;
- `init/w1.npy`, `init/b1.npy`, `init/w2.npy`, `init/b2.npy`: He-normal weights and zero
  biases;
- `dataset.id`: the identity of the data (the first 16 hex digits of the SHA-256 of the
  training images and labels plus the sizes); the Camel program writes it into the
  checkpoint, and `save_tree`/`load_tree` check it, so a checkpoint cannot be loaded against
  a different data set. The batch order is not part of it, which keeps a checkpoint valid
  when the number of epochs changes;
- `manifest.json`: source, sizes, seed and the SHA-256 of every file.

`train_order.npy` must hold at least `ceil(steps / (train // batch))` epochs; `compare.py`
passes that number to `make_data.py`, whose default is 10. Running `train.cml` by hand with
more steps than the order covers fails at the `slice` that reads the batch.

**MNIST is never downloaded.** When the repository cache holds `mnist.npz`, the real data is
used (`--mnist <path>`, or `--source auto` to look at `.cache/mnist/mnist.npz` under the
repository root); when it does not, the script generates a **deterministic synthetic
stand-in** and records `synthetic` as the source in `manifest.json`. Each class is a fixed
set of bright rectangles on a 28×28 grid (a crude stroke), every sample is shifted by a few
pixels and gets Gaussian noise, and the result is shuffled with a fixed seed. It exists so
that the machinery the example demonstrates (data, training, evaluation, checkpoint,
comparison) works without the dataset; C2 is about the language and the example, not about
MNIST. The small CI configuration always uses the stand-in, so its numbers are identical on
every machine.

## Running it

The comparison (recommended: it prepares the data, runs both sides and writes the tables).
It needs `numpy`, `torch` and `psutil` in the `python` on `PATH`, and the Camel binary on
`PATH` as well.

```powershell
# small: synthetic data, 200 steps, about 5 seconds including the PyTorch import
python showcase\c2-mlp-mnist\compare.py --profile small

# the reported configuration: MNIST when the cache has it, the stand-in otherwise
python showcase\c2-mlp-mnist\compare.py --profile standard `
    --mnist D:\Projects\Camel\.cache\mnist\mnist.npz
```

`--profile`: `small` (1000/200, 200 steps, batch 50), `standard` (10000/2000, 1000 steps,
batch 100), `full` (60000/10000, 3000 steps, batch 128). `--steps`, `--batch`, `--lr`,
`--log-every`, `--threads`, `--seed` and `--source` override the profile.

The Camel program alone (first set `$env:CAMEL_HOME = "out\latest"` and put
`out\latest\bin` on `PATH`):

```powershell
camel showcase\c2-mlp-mnist\train.cml `
    --arg data=showcase/c2-mlp-mnist/artifacts/small --arg steps=200 --arg batch=50 `
    --arg lr=0.001 --arg log_every=20 --arg threads=1 `
    std::macro std::nvm
```

**No cleanup is needed before a second run**: `save_tree` replaces the checkpoint directory
as a whole (see "Problems" below).

The PyTorch baseline alone:

```powershell
python showcase\c2-mlp-mnist\baseline\train_torch.py `
    --data showcase/c2-mlp-mnist/artifacts/small --steps 200 --batch 50 --lr 0.001 --log-every 20 --threads 1
```

Both programs print the same machine-readable lines (`c2 config`, `c2 init`,
`c2 step=<i> loss=<v>`, `c2 trained`, `c2 checkpoint`); `compare.py` parses only those.

## Results and tolerances

`RESULTS.md` contains three tables generated by the script:

1. **Agreement**: initial validation loss, the training loss at every logged step (the worst
   scaled difference), final validation loss, final validation accuracy, and each side's
   own checkpoint round-trip drift; every row shows the difference, the tolerance and
   PASS/FAIL, and `compare.py` exits non-zero when one bound is exceeded. The small test
   plan asserts the same things, so the example cannot rot silently.
2. **Performance**: training time (the training loop only; data loading, evaluation and
   checkpointing are excluded), throughput (`steps × batch / time`), the peak working set of
   the whole process including children (psutil), the Camel compile time (the median of three
   `std::macro std::stats std::null` runs), and the whole-process wall time including
   startup.
3. **Code size**: SLOC per `// == section ==` / `# == section ==` section the two files share
   (blank lines, full-line comments, Python docstrings and C block comments are not counted;
   a code line with a trailing comment counts once).

**Why these tolerances (design principles section 7)**: bit-for-bit equality is promised only
for the canonical algorithm on one back end. These two programs compute the same f32
formulas, but the matrix kernels accumulate in a different order and Adam's bias correction
is written differently, and the difference compounds over the optimization steps, so this is
a **bounded-error** comparison: `1e-5` relative for the initial loss (the same weights and
data, one forward pass), `1e-2` for every logged training loss, `1e-2` relative for the final
validation loss, and `1e-2` absolute (one percentage point) for the final validation
accuracy. A logged training loss can be close to zero, where a purely relative bound measures
rounding noise rather than a divergence, so the training-loss row uses the difference
`|a - b| / max(1, |a|, |b|)` (relative with a one-unit floor) and reports that scaled value.
Under the standard configuration the worst step is step 900, with a scaled difference of
`1.46e-3` against the `1e-2` bound; see `RESULTS.md`. The `small` configuration (synthetic
stand-in) differs at the `2.4e-4` level.

## Side by side

- **The parameters are a type.** Camel's `type Mlp = { w1, b1, w2, b2 }` is at once the type
  of the starting weights, of the gradient and of the optimizer state;
  `with_grad(loss)<m>(x, y)` returns a gradient tree of the same shape and
  `adam(m, g, state, lr)` consumes it, so no line in the program names a leaf. PyTorch needs
  three things — an `nn.Module`, an optimizer object and a `state_dict` — to express the same
  facts.
- **Training is an expression.** The whole run is one `loop` whose state (model, optimizer
  state, step and the loss curve) is written out explicitly; no step function, logging
  statement and state update have to be split across functions. The body is pure — the curve
  is collected into a `float[]`, not printed from inside the loop — so the timed region is
  exactly the training work and the loop needs no `sync` block. PyTorch uses the idiomatic
  `for` loop with `zero_grad`/`backward`/`step` and prints inside it.
- **Effects are explicit.** The collected curve is printed afterwards by `report`, a
  `sync` function whose `foreach` runs in step order. In PyTorch `print` is sequential for
  free; in Camel the one place that writes is marked.
- **The data pipeline.** Both sides read the same `.npy` files and walk the same
  `train_order.npy`, one permutation per epoch. Camel gathers a minibatch with
  `take(train_x, slice(order, 0, offset, offset + batch))` and its labels the same way;
  PyTorch indexes with the same order. `take` produces a copy (an arbitrary row order is not
  a view), so the Camel side pays for the gather, while PyTorch's advanced indexing does too.
  Neither side shuffles during training: the order is part of the data, so a step sees the
  same rows on both sides.
- **Labels are the native form.** The data carries one `int64` class label per row, the form
  `F.cross_entropy` takes, and Camel's `softmax_cross_entropy` takes the same form. A tensor
  read by `load_npy` has no static element type, so the Camel code says
  `cast(labels, 'int64')` to select the integer-label overload (see "Problems").
- **Where Camel is shorter**: the checkpoint (`save_tree` on a tree vs `torch.save` on a
  `state_dict`), evaluation (one `argmax`/`sum` vs `no_grad` + `.item()`), and initialization
  (four `load_npy` calls vs a layer-by-layer `copy_`). PyTorch is shorter in `model` and
  `loss`, because `nn.Linear` hides the bias and the shapes; the overall counts are in
  `RESULTS.md`.
- **The cost**: in the recorded standard run Camel's throughput is 0.83× PyTorch's and its
  peak memory 0.24× (74.7 vs 315.8 MiB, one thread; see `RESULTS.md`). Three same-day runs
  measured 0.83×, 1.30× and 0.81× — the middle one had PyTorch slowed by other load — so treat
  the wall-clock ratio as load-dependent and the agreement numbers as the stable ones. Camel's
  compile time is about 0.03 s; PyTorch is eager and has no such step. Performance is not what
  this example sets out to prove, but the numbers are reported as measured.

## Problems

The genuinely awkward parts of writing this example, with the question each raises in the
"Open questions" section of `docs/handover/reports/H13.md`.

1. **Subscript and slice syntax is not implemented.** Design principles 5.4 and H21 describe
   `t[i]`, `t[a:b]` and `t[idx]`; the language reference (`06_expressions.md`) still says the
   slice spelling is reserved and reports `NoMatchingFunction`. The natural showcase line
   `train_x[order[offset : offset + batch]]` therefore has to be written as
   `take(train_x, slice(order, 0, offset, offset + batch))`. It works, but a reader has to
   decode the function-call form of an operation the design presents as indexing.
   **Question**: H21's indexing is in the queue; is the function-call form acceptable for the
   showcase until it lands, and should the example switch to `t[...]` when it does?
2. **A tensor read from a file has no static element type, so label-taking operators cannot be
   selected.** `nn.softmax_cross_entropy` is overloaded on the target's type: a floating
   tensor means one-hot rows, an integer tensor means class indices. `load_npy` returns a bare
   `Tensor`, so the integer overload is not selected and the call fails at run time with
   "expects rank-2 target". The example writes `cast(labels, 'int64')` at every use. The
   technical note (`docs/technical/43_rows_labels_and_keys.md`, section 5) records this as an
   open question; the cast is a language-level workaround, not a property of the task.
   **Question**: should `load_npy` (or a loader variant) return a tensor whose element type is
   known, or should overload resolution see the run-time dtype for this pair?
3. **The batch order cannot be shared through keyed random.** `random::permutation(key(seed),
   n)` exists and is pure, but a PyTorch counterpart cannot reproduce the keyed permutation,
   so if each side shuffled independently the two runs would train on different batches and a
   per-step comparison would be meaningless. The example therefore puts one permutation per
   epoch into `train_order.npy`, and both sides walk it; Camel uses `take`, not
   `permutation`. This keeps the comparison exact, but the sampler, not the training program,
   owns the shuffle.
   **Question**: for showcases with a non-Camel counterpart, is a data-side sampler the
   intended shape, or should such examples compare only aggregate metrics so each side can
   shuffle with its own generator?
4. **Collecting a loss curve works but reads heavily.** `loop ... losses = [] as float[]` with
   `losses->append<value>` is the decided form (design principles 3.10) and it works; printing
   the logged steps afterwards needs `range(...)->filter<...>->foreach<sync ...>`, about six
   lines and a `sync` closure for what a `for` loop with `print` does in Python. No blocking
   question; recorded because it is the kind of ceremony the showcase is meant to expose.
5. **`take` copies rows.** The shuffled-order sampler needs a gather; a contiguous batch could
   be a `slice` view. The copy is a per-step cost, not the source of the peak: **correction
   (H40b)** this example used to say the standard run's memory was higher than with contiguous
   slices; H40 found that the 262 MiB peak came from collection pacing (the order in which the
   block-bytes base was taken after deferred releases) and not from `take`, and that `take` and
   `slice` peak the same (H40 section 3). The standard run now peaks at 74.7 MiB
   (`RESULTS.md`). This is the documented semantics of `take` (5.4), not a defect; it is
   reported with the other numbers.

## Tests

The first review's questions were answered in H20/H21 and the example now uses the answers:
`save_tree` replaces a directory as a whole (no Python-side cleanup), `take` selects rows,
`softmax_cross_entropy` takes integer labels, and `one_hot` is no longer needed. H21 item 3
was withdrawn (see H25): `with <m: Mlp>` and `with_grad(loss)<m>(x, y)` are the final
spelling. The example binds `with_grad(loss)` outside the loop and uses
`let (value, gradient) = loss_with_grad<m>(x, y)` inside.

The small configuration is registered in `test/plans/perf/showcase_c2.plan.toml`
(`tier = "slow"`): it runs 200 steps on the synthetic stand-in, runs both sides, checks the
tolerances above, and takes a few seconds (3.3 s in the recorded run). H10's fast/slow split
has landed: the default `npm run test` skips the slow tier and `npm run test:all` (or
`--tier all`) runs it.

## Not committed

`artifacts/` (data and checkpoints), `*.npy` and `*.pt` are in
`showcase/c2-mlp-mnist/.gitignore`; the repository root already ignores `tmp/`. `RESULTS.md`
is a few dozen lines of text and is committed so the numbers can be reviewed.
