# Camel Examples

[中文简体](README.cn.md) | English

Short programs, one task each. Every file starts with a comment that says what it shows and
gives the command that runs it. The commands are written for the repository root, with `camel`
on the `PATH`.

```sh
camel examples/text/word_frequency.cml
```

A command line names the program and then, optionally, the passes applied to its graph. With no
pass the program runs on the default back end; `camel x.cml std::prl` runs the same program with
the parallel scheduler. The output of every example is the same on `std::nvm`, `std::fvm`,
`std::jit` and `std::prl`.

To learn the language step by step, read the [tutorial](../docs/tutorial/tutorial.en.md); for the
details of a construct, see the [language reference](../docs/language/en/README.md). The library
functions the examples call (`str`, `array`, `math`, `statistics` and the others) are listed with
signatures and examples in the [standard library reference](../docs/stdlib-reference.md).

## Examples by task

### Text: `text/`

| File | What it shows | Run |
|------|------|------|
| [`word_frequency.cml`](text/word_frequency.cml) | The words of a text with `split`, `trim` and `to_lower`, counted in a `Map<string, int>`; the most frequent with `sort_by` and `take` | `camel examples/text/word_frequency.cml` |
| [`table_report.cml`](text/table_report.cml) | An aligned table: a column as wide as its longest entry (`max`, `repeat`), headings with `to_upper`, the total with `sum` | `camel examples/text/table_report.cml` |
| [`csv_parse.cml`](text/csv_parse.cml) | CSV lines cut into fields with `split` and `trim`, numbers read with `parse_int`; a bad line is an error value reported with its line number | `camel examples/text/csv_parse.cml` |
| [`log_filter.cml`](text/log_filter.cml) | A log file read and split into lines and fields; lines selected by a substring (`find`) or a pattern (`re.test`) and counted per level | `camel examples/text/log_filter.cml` |

### Data: `data/`

| File | What it shows | Run |
|------|------|------|
| [`records.cml`](data/records.cml) | An array of struct records: filter, derived records, `sort_by` and `sum`, totals grouped in a `Map` | `camel examples/data/records.cml` |
| [`json_roundtrip.cml`](data/json_roundtrip.cml) | JSON encoding and decoding into a typed value; malformed text gives an error value | `camel examples/data/json_roundtrip.cml` |
| [`validate_input.cml`](data/validate_input.cml) | Functions that may fail return `T!`; `?->`, `!!` and `match ... case error e` | `camel examples/data/validate_input.cml` |
| [`nullable_lookup.cml`](data/nullable_lookup.cml) | Lookups that may find nothing return `T?` (`find` and `index_of` of `array`, a `Map` lookup); `??` and `match ... case null` | `camel examples/data/nullable_lookup.cml` |
| [`dedup_sets.cml`](data/dedup_sets.cml) | Removing duplicates with `unique` or a `Set<T>`; union, intersection, difference | `camel examples/data/dedup_sets.cml` |

### Files: `files/`

| File | What it shows | Run |
|------|------|------|
| [`read_write.cml`](files/read_write.cml) | Writing a text file, reading it back and splitting it into lines; a missing file handled through the `string!` of `fs.try_read_text` | `camel examples/files/read_write.cml` |
| [`json_config.cml`](files/json_config.cml) | Settings in a JSON file read into a struct, with defaults when the file is missing or has the wrong shape | `camel examples/files/json_config.cml` |

### Sorting and searching: `algorithms/`

| File | What it shows | Run |
|------|------|------|
| [`merge_sort.cml`](algorithms/merge_sort.cml) | Merge sort written with recursion, checked against `algo.sorted` and `array.sort_by`, the library functions a program would call | `camel examples/algorithms/merge_sort.cml` |
| [`binary_search.cml`](algorithms/binary_search.cml) | Binary search by tail recursion; a missing value gives `null` (`int?`). For an array in any order there is `array.index_of` | `camel examples/algorithms/binary_search.cml` |

### Functions: `functions/`

| File | What it shows | Run |
|------|------|------|
| [`closures.cml`](functions/closures.cml) | Functions as values: closures, composition, `with` parameters, a counter with its own state | `camel examples/functions/closures.cml` |
| [`generic_helpers.cml`](functions/generic_helpers.cml) | Generic helper functions with `typeas`, built on the generic functions of `array` | `camel examples/functions/generic_helpers.cml` |

### Command-line tools: `cli/`

| File | What it shows | Run |
|------|------|------|
| [`temperature.cml`](cli/temperature.cml) | The parameters of `main` are the program's inputs and come from `--arg name=value`; the unit is normalised with `trim` and `to_upper` | `camel examples/cli/temperature.cml --arg value=21.5 --arg unit=C --arg steps=3` |
| [`exit_status.cml`](cli/exit_status.cml) | The `int` returned by `main` is the exit code; `exit(n)` ends the program at once | `camel examples/cli/exit_status.cml --arg used=91 --arg limit=80`<br>`camel examples/cli/exit_status.cml --arg used=40 --arg limit=80` |

### Parallel execution: `parallel/`

| File | What it shows | Run |
|------|------|------|
| [`parallel_map.cml`](parallel/parallel_map.cml) | One `map` run sequentially or in parallel, chosen by the passes on the command line | `camel examples/parallel/parallel_map.cml`<br>`camel examples/parallel/parallel_map.cml std::prl` |

### Numeric simulation: `numeric/`

| File | What it shows | Run |
|------|------|------|
| [`monte_carlo_pi.cml`](numeric/monte_carlo_pi.cml) | Monte Carlo estimates of pi with fixed seeds, summarised with `statistics`, `min`/`max` and `math` | `camel examples/numeric/monte_carlo_pi.cml` |
| [`random_walk.cml`](numeric/random_walk.cml) | Seeded random walks with the `random` module. The steps are a tail recursion: calls that depend on state have no order inside a `map` | `camel examples/numeric/random_walk.cml` |
| [`ode_euler.cml`](numeric/ode_euler.cml) | Euler integration of a cooling equation as a tail recursion, compared with the exact solution | `camel examples/numeric/ode_euler.cml` |

### Tensors and training: `ml/`

| File | What it shows | Run |
|------|------|------|
| [`train_linear.cml`](ml/train_linear.cml) | A minimal training loop: `autodiff.grad` for the gradient, `nn.sgd` for the update | `camel examples/ml/train_linear.cml` |
| [`export_onnx.cml`](ml/export_onnx.cml) | A small two-layer model exported to an ONNX file with `onnx.export_model` | `camel examples/ml/export_onnx.cml`<br>`camel examples/ml/export_onnx.cml std::macro std::opt onnx::export` |

The programs under `files/` and `ml/`, and `text/log_filter.cml`, write their files to
`tmp/examples/`, relative to the directory the command is run from; they create the directory themselves, and it is not tracked
by git.

## Other directories

- [`perf/`](perf/): `fibs.cml` times a recursive Fibonacci function with `perf.perfms` (`camel examples/perf/fibs.cml std::fvm`); its output is a measurement and differs from run to run.
- [`tty/`](tty/): `snake.cml` is an interactive terminal game for manual exploration (`camel examples/tty/snake.cml`); see [`tty/README.md`](tty/README.md).

## Checking the examples

The test plan `test/plans/feat/examples.plan.toml` runs every program in the tables on the four
back ends and compares its output with the recorded text under
`test/plans/feat/golden/examples/`:

```sh
node scripts/test.js test/plans/feat/examples.plan.toml
```

The plan is part of the default set run by `npm run test`. After an intended change of an
example's output, record it again:

```sh
node test/tools/run-tests.mjs --update-golden test/plans/feat/examples.plan.toml
```
