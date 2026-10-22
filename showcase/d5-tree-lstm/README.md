# D5 — Tree-LSTM sentiment classification

A four-leaf syntactic tree: leaves are words, internal nodes combine their two children with a
Tree-LSTM cell, and the root hidden state goes through a linear layer to "positive/negative".
The example is here for **recursion over an `enum` tree**: `forward` calls itself, the tree is an
`enum` value, and `with_grad` still returns the gradient of the whole parameter tree.

The label is not a plain sum of the four word polarities: it is the **XOR of the left and the
right subtree polarity**.  The same four words give different labels depending on how they are
grouped, so a bag-of-words model cannot get it right; the model must actually compose the tree.

## Layout

```
showcase/d5-tree-lstm/
├── train.cml                 the Camel program (recursive Tree-LSTM)
├── baseline/train_torch.py   the PyTorch counterpart (the same recursion)
├── make_data.py              synthetic trees, embedding initialization, one-hot labels
├── compare.py                runs both, compares, measures, writes RESULTS.md
├── RESULTS.md                measured results of the standard profile (generated)
├── README.cn.md
└── README.md
```

## Data

- Every sentence has exactly four leaves and the vocabulary is 8; word ids below 4 are negative,
  ids 4 and above are positive.
- The label is `(left subtree polarity > 0) XOR (right subtree polarity > 0)`.
- 256 training and 64 validation sentences, fixed seed.
- Each leaf is stored as a one-hot `[4, 8]` row, and both sides compute `onehot @ emb`.
  `take`/row indexing does not exist yet (H20-2), so the data stores one-hots and the lookup is a
  matrix product.

The model has 762 parameters and an 8-wide hidden state.

## Running

```powershell
python showcase/d5-tree-lstm/make_data.py --out showcase/d5-tree-lstm/artifacts/small
$env:CAMEL_HOME = "out\latest"
$env:PATH = "$env:CAMEL_HOME\bin;$env:PATH"
camel showcase/d5-tree-lstm/train.cml --arg data=showcase/d5-tree-lstm/artifacts/small `
    --arg steps=200 --arg lr=0.01 --arg log_every=20 --arg threads=1 std::macro std::nvm
python showcase/d5-tree-lstm/compare.py --profile small
python showcase/d5-tree-lstm/compare.py --profile standard   # writes RESULTS.md
```

## Results (standard, 1000 steps, one sentence per step)

| metric | Camel | PyTorch | difference |
|---|---|---|---|
| initial validation accuracy | 0.609375 | 0.609375 | 0 |
| worst logged loss (step 800) | 0.000076483 | 0.000076529 | 6.0e-04 relative |
| final validation accuracy | 1.000000 | 1.000000 | 0 |

Training time 0.264 s vs 2.759 s; peak memory 19.5 MiB vs 274.6 MiB; compile 0.032 s.  The full
tables are in `RESULTS.md`.

**On performance**: each step is one four-leaf tree with 762 parameters.  PyTorch's time is
dominated by Python recursion and per-step autograd overhead, while Camel's recursion runs in
the C++ graph.  The 0.096 ratio is about framework overhead on a tiny, deeply recursive model,
not kernel speed.  The C2 MLP is the compute-dominated comparison.

## Tolerances

- The initial accuracy is a discrete count and must agree exactly in practice (measured: 0
  difference); it is still bounded at `1e-02` absolute.
- Logged losses: `1e-02` relative (measured worst: 6e-4).
- Final accuracy: `1e-02` absolute (both 1.0).
- Not bit-identical: design principles 7.

## Side by side

| thing | Camel | PyTorch |
|---|---|---|
| tree | `type Tree = enum { Leaf(Tensor), Node(Tree, Tree) }` | nested tuples, leaves are `Tensor`s |
| recursion | `forward<p>(left)` / `forward<p>(right)` | `self.forward(left)` / `right` |
| composition | the five gates written out | the same formulas |
| loss | `softmax_cross_entropy(logits, onehot)` | `F.cross_entropy(logits, ids)` |
| loop | `loop p, opt, step ... do sync { ... }` | `for step in range(...)` |

### Why one sentence per step

Trees have variable shape.  Batching would either pad to a fixed shape or use row indexing /
`take` to assemble several trees; the latter is H20-2/H21-1.  This example processes one tree per
step to make "recursion + autodiff" clear; throughput is not the goal.  Once `t[idx]` from
H21-1 lands, a batch can be assembled as an array of trees and the loop shape does not change.

### Differentiation spelling

Design principles 9.9 (H21 item 3 was withdrawn; see H25): `with <p: Params>` is the final
spelling, and `with_grad(loss)<p>(tree, target)` differentiates the whole parameter tree,
returning a gradient tree of the same type; the tree input is data.  The example binds
`with_grad(loss)` outside the loop.

## Problems found

- **Recursion works**: a recursive call on an `enum` tree inside a `with`-parameter function,
  differentiated with respect to the parameters, works.  That is the point of D5, and nothing
  blocked it.
- **No `take` / row indexing**: the lookup is a one-hot matrix product instead of selecting a
  row of the embedding table; H20-2's `take` will allow storing word ids directly.  See the data
  section.
- **No batching for variable trees**: see above; H21-1 enables it.  The current run is one
  sentence per step, and the throughput is reported as measured.
- The internal node's input is the sum of the children's hidden states (child-sum), with no
  extra parameter for internal nodes; a standard Tree-LSTM simplification that does not affect
  the "recursion + gates" demonstration.

## Tests

`test/plans/perf/showcase_d5.plan.toml` (`tier = "slow"`) runs `--profile small`: 256 sentences,
200 steps, about six seconds for the whole plan (mostly the two PyTorch imports).  It fails when
a bound is exceeded.

## Not committed

`artifacts/`, `*.npy` and `__pycache__/` are ignored; `RESULTS.md` is committed.
