# Stdlib drafts not yet integrated: blocker matrix

Method. For each draft `docs/handover/stdlib/<m>.cml`, a scratch copy of the installed tree (lane `silent`, merged with fsh-dev at the head of `lane/stdlib-text`) had the draft (and its `<m>.signatures.cml`; for `std` the `prelude.signatures.cml` as `signatures/globals.cml`) copied over the module, and `camel check` was run on it. The compiler stops at the first error of a module, so the declaration an error points at was removed and the check repeated, until the module was clean: every distinct failure is listed. A failure that names a declaration removed earlier is marked "cascade" and is not counted as a cause of its own. Then three `@example` lines per module were checked against what survived. Nothing was committed from the scratch copies. No draft was repaired.

Causes (letters are used in the table at the end):

- **A** `fail` / `never` missing: `fail<'Kind'>(message)` is only in the prelude draft (`std`); no installed prelude has it.
- **B** elementwise conversion `T(x)` with `T` a type variable (`Unresolved reference: 'T'`): in progress (conversion of arrays; the scalar `T(...)` over a type variable is part of it).
- **C** with-parameters do not take part in overload selection: in progress. Also hit as "overloads that differ only in the `when` guard are a Redeclaration" (a separate point, see C2).
- **D** `case array` / a `case _ when <guard> =>` arm in `match type`: in progress (`case array`); the guarded arm is a syntax error today.
- **E** `->` propagation (`!->`, `?->`, `!!` chains): in progress; shows only in the examples (`import_module(...) !-> attr<...>`).
- **F** missing native kernel (`NativeDeclarationNotLoaded`): the draft declares `#op('m:op')` that no module implements.
- **G** syntax order of the declaration tail: the drafts write `when ... pure` and `when ... uses r`; the parser refuses both.
- **H** dimension-pack and shape-name features: a leading pack `T[..s, i]`; a dimension name only in the result (`o`, `c`); not accepted today.
- **I** `with` default of type `T` written as a float literal (`eps: T = 1e-5`).
- **J** opaque handle types (`Graph`, `Node`, `Op`, `Region`, `Rule`, `Proof`, `Txn<F>`, `PyObject`, ...) not declared anywhere the draft can see.
- **K** a type variable as the argument of a module type (`Txn<F>`).
- **L** cross-module dependency not satisfied by the installed neighbour (a name the draft imports or uses from a module that is still the old version: `shape`, `numel`, `dim`, `reshape`, `unsqueeze`, `softmax`, `sigmoid`, `graphs_of`, ...).
- **M** `uses rng`: a resource name that is neither a parameter nor a declared resource.
- **N** draft bugs (the draft is wrong as written).
- **O** `Redeclaration`: two exports of one name that differ only in `when` (C2).

## array (42 exports)

Module check: not clean, 38 failures, 24 of them root.
- **F** 14 kernels: `array:shape`, `numel`, `dim`, `to_bytes`, `argsort` (3 forms), `elements`, `sum_axis`, `mean_axis`, `min_axis`, `max_axis`, `argmax_axis`, `argmin_axis`. The other 12 failures (mean, min, max, argmax, ... ) are cascades of `*_axis` and `elements`.
- **A** `chunks`, `some_elements`: `fail` unresolved.
- **D** `sum` L156: `case _ when is_float::<T>() => ...` is `MissingToken: Expected '=>' before 'when'`.
- Examples: `[1, 2, 3]->reverse` ok; `[3, 9, 2]->max` fails (`max` is in the cascade); `zip_with<...>` ok.

## statistics (4 exports)

- **B** `variance`, `stdev`: `T(variance_of(float(xs)))`.
- **A** `quantile`, `variance_of`: `fail`.
- `median` is a cascade of `quantile`. No kernel is missing.
- Examples: all three fail (the names are removed by the failures above).

## math (79 exports)

- **F** 55 operators: for each function `math:<f>_kernel` and `math:<f>_d` (sqrt, exp, expm1, log, log2, log10, log1p, sin ... tanh, floor, ceil, round, trunc, pow, hypot, atan2, min, max, ...), and the complex `re`/`im`. The scalar kernels exist under their old names; the draft's names are new.
- **B** 27 declarations: `return T(sqrt_kernel(float(x)))` and the like (sqrt .. trunc, pow, hypot, atan2, min, max).
- **O** `exp::<T>(z: T) when is_complex::<T>()` is a Redeclaration of the `exp` above it (and `abs`, twice, as a cascade). Needs overloads by `when` (C2).
- **A** `clamp`, `gcd`: `fail`.
- **N** `is_inf`, `is_finite` use `inf`, which is not a name in scope (not in `math` and not in the prelude draft; the draft means `1.0/0.0` or an export that is missing).
- Examples: all three fail on the module-level kernel error (`math:im`).

## random (17 exports)

- **F** `random:philox`, `random:uniform_array`, `random:normal_array` (kernels and their `rand:` aliases; the `#impl` of `uniform`/`normal` is a cascade: `ImplKernelUnknown`).
- **M** `seed` and `next_key`: `uses rng` with no resource `rng` declared.
- **B** `uniform_at`: `T(float(...) * ...)`.
- **A** `fold`, `randint`: `fail`.
- Examples: `key(42)` ok; `permutation` and `randn` fail (cascades).

## tensor (8 exports)

- **G** `save_npy`: `when ... uses fs` (`Unexpected 'uses'`).
- **B** `zeros`/`ones`: `full(shape, T(0))`.
- **L** `eye`, `one_hot`, `stack`: `reshape`, `unsqueeze` not found (the neighbour `array` is still the old module; the draft expects the new `array` to provide the reshaping functions as pure Camel).
- **F** `tensor:full`.
- Examples: all three fail (cascades; `save_npy` also G).

## nn (15 exports)

- **G** 7 kernels (`relu_kernel`, `sigmoid_kernel`, `gelu_kernel`, `softmax_kernel`, `log_softmax_kernel`, `layer_norm_kernel`, `batch_norm_train_kernel`): `when ... pure` (`Unexpected 'pure'`). These cascade into 5 `ImplKernelUnknown`.
- **H** `linear`, `layer_norm`: `T[..s, i]` leading pack (`DimensionAfterAnyRank`); `conv2d`, `channel_mean`: `o` and `c` bound by no parameter (`DimensionNameNotBound`).
- **I** `batch_norm`, `batch_norm_train`: `eps: T = 1e-5`, `momentum: T = 0.1`.
- **L** `once`, `log_softmax`: `numel`; `batch_norm`: `shape`; `softmax_cross_entropy`: `dim` (not exported by the old `array`).
- **F** `nn:conv2d`, `max_pool2d`, `avg_pool2d`, `softmax_cross_entropy_kernel`, `softmax_cross_entropy_labels_kernel`.
- Examples: all three fail (the first is G on `relu`).

## optim (10 exports)

- **O** `zero_leaf`, `sgd_leaf`, `adam_leaf`: each declared once per leaf kind with different `when` guards (`untrained::<T>()` and its negation): Redeclaration (C2). 7 cascades.
- **B** `of`, `sgd_leaf`, `adam_leaf`: `T(0.0)`, `T(float(p) - ...)`, `p - T(rate) * g`.
- **L** `zero_leaf`: `shape`.
- Examples: `3->constant<rate: 0.1>` ok; `adam_state` and `adamw<...>` fail (cascades).

## graph (46 exports)

- **J** every declaration but one: `Graph`, `Node`, `Op`, `TypeRef`, `Region`, `Tag`, `Txn<F>`, `NodeKind`, `RegionKind`, `Effects` are named in the signatures but nothing declares them (the draft's header says the handles are named by the module; the installed `signatures/graph.cml` the README deletes is the only place the old ones were). 45 failures are `UnresolvedTypeReference`.
- **G** `add_constant`: `when ... uses t`.
- No kernel could be checked past J (`NativeDeclarationNotLoaded` is not reached). Examples: all fail.

## opt (3 exports)

- **K** 7 helpers: `Txn<F>` with `F` a type variable (`InvalidTypeArgument`). Needs a decision: either the module type admits a type variable, or the helpers take `Txn` without the argument.
- **L** `cse`, `dead_values`, `fold`: `graphs_of` (graph is J).
- **N** `constant::<T>(a)` inside `same_value`: the call is `NoMatchingFunction` (one argument, the function wants the with-argument); `same_value` and `same_constant` are used before they are declared in a form the module does not export (`Unresolved reference: 'same_value'`, `'same_constant'`).
- Examples: all three fail (cascade of the above).

## rewrite (19 exports)

- **J** `Rule`, `Proof` not declared (11 failures; `Node`, `Op` come from `graph`, which is J).
- **F** `rewrite:check_text`.
- Examples: all fail (`rewrite` itself is not exported once the declarations are removed).

## rules (51 exports)

- **L** two root failures: `softmax` (L331) and `sigmoid` (L363) are not found: they come from `nn`, which is not integrated. The other 88 failures are the error repeated at the rule-set level (`array_log_softmax_of_softmax` unresolved), one per rule, because a rule that fails removes the table.
- Examples: all three fail (same).

## stream (12 exports)

- **F** `stream:tee` (3 failures, one declaration).
- Examples: the `unfold` loop ok; `1->iterate<...>->take<3>->collect` fails with `LiteralNotRepresentable: '[]' cannot be 'int[]'` (the empty accumulator of `collect`: a draft bug in the example or the body, N); the `tee` example fails on F (and `let (a, b) = ...` as an expression, N).

## std (the prelude, 53 exports)

- Module check clean: 0 failures. With `prelude.signatures.cml` as `signatures/globals.cml`, the draft `std.cml` and the prelude check as is (`fail`/`never` are declared there as `#op(':op/fail')`; nobody installs that operator yet, so A holds for every other module until the prelude is integrated). The module has no `@example` lines to run.

## onnx (2 exports)

- **F** `onnx:supported_operators`. Example fails (cascade).

## python (7 exports)

- **J** `PyObject` is not declared (6 failures).
- **F** `python:exec`, `python:run_file`.
- **E** the example `import_module('math') !-> attr<'pi'> !-> to_string !! ''` does not parse (`!->` is not in the lexer yet).
- Examples: all three fail.

## ckpt

No `ckpt.cml` in `docs/handover/stdlib/`: only `ckpt.notes.md` and `ckpt.signatures.cml`. Nothing to check; the draft of the module is missing (N, task for the draft author).

## Causes x modules

Cell is the number of root failures (declarations that fail for that cause); `.` is none. In-progress rows are the four core features.

| cause | array | statistics | math | random | tensor | nn | optim | graph | opt | rewrite | rules | stream | std | onnx | python | ckpt |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| A `fail`/`never` | 2 | 2 | 2 | 2 | . | . | . | . | . | . | . | . | ok | . | . | . |
| B elementwise `T(x)` (in progress) | . | 2 | 27 | 1 | 2 | . | 4 | . | . | . | . | . | . | . | . | . |
| C with-overloading (in progress) | . | . | . | . | . | . | . | . | . | . | . | . | . | . | . | . |
| C2/O overloads that differ by `when` | . | . | 1 | . | . | . | 3 | . | . | . | . | . | . | . | . | . |
| D `case array` / guarded arm (in progress) | 1 | . | . | . | . | . | . | . | . | . | . | . | . | . | . | . |
| E `->` propagation (in progress) | . | . | . | . | . | . | . | . | . | . | . | . | . | . | 1 (example) | . |
| F missing native kernel | 14 | . | 55 | 6 | 1 | 5 | . | . | . | 1 | . | 1 | . | 1 | 2 | . |
| G `when ... pure` / `when ... uses` | . | . | . | . | 1 | 7 | . | 1 | . | . | . | . | . | . | . | . |
| H dimension pack / unbound dimension name | . | . | . | . | . | 4 | . | . | . | . | . | . | . | . | . | . |
| I `with` default `T = float literal` | . | . | . | . | . | 2 | . | . | . | . | . | . | . | . | . | . |
| J opaque handle types undeclared | . | . | . | . | . | . | . | 45 | . | 11 | . | . | . | . | 6 | . |
| K type variable in a module type | . | . | . | . | . | . | . | . | 7 | . | . | . | . | . | . | . |
| L neighbour module not yet new | . | . | . | . | 3 | 4 | 1 | . | 3 | . | 2 | . | . | . | . | . |
| M `uses rng` unknown resource | . | . | . | 2 | . | . | . | . | . | . | . | . | . | . | . | . |
| N draft bug | . | . | 2 | . | . | . | . | . | 3 | . | . | 2 | . | . | . | 1 (module missing) |

Readings.
- The four in-progress features cover every failure in `statistics` and `optim`, and the elementwise conversion alone is 36 declarations. They do not unblock `math`, `graph`, `rewrite`, `rules`, `opt`, `python`: those wait for kernels (F), handle types (J) or the integration order (L).
- F is the largest cost: 85 kernels across 9 modules, 55 of them the `math` scalar kernels. J is the one cause that needs a decision (where the handle types are declared) before `graph` can be tried at all; K is a second decision (`Txn<F>`).
- G and H are not on the in-progress list and block `nn` at the first line of the kernels: either the parser accepts `when ... pure` / `when ... uses r` or the drafts move the clause (decision: which side), and the leading dimension pack and the result-only dimension names (`o`, `c`) need the type function the notes already say is later.
- Order that unblocks the most with the least: prelude (A), then the four core features, then `array` kernels, then `math` kernels; `tensor`, `nn`, `optim`, `rules` follow `array`, `math` and each other (L).
