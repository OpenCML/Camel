# progen: random programs with a reference output, run across pipelines

`progen` draws Camel programs from a seed, computes in Python what each program must print, and
runs the program under a matrix of rewrite passes and schedulers. A run that prints something
else is a silent wrong result; a run that dies or fails without a source position is reported
too. Failing pairs are shrunk automatically.

It complements `test/cases/linear/byref/var_pipelines.py` (three hand-built shapes) and
`benchmarks/pipeline/` (existing programs, compared with their own zero-pass run). Here the
programs are random over a wide set of constructs and the oracle is independent of every
backend, so the zero-pass run is checked as well.

The tool lives in `test/tools/` and not in `benchmarks/pipeline/`: that directory is the
harness of one experiment (a fixed corpus, records, frozen classifier rules with recorded
deviations), while this is a test tool whose default batch runs in the test suite. It imports
from that harness what applies to both (see "Reuse" below).

Findings are in [`test/cases/opt/progen/`](../../cases/opt/progen/README.md).

## Usage

All commands run from the repository root; the compiler is `out/latest` unless `CAMEL_EXE` /
`CAMEL_HOME` are set.

```
python test/tools/progen/progen.py show --seed 7                 # the program and its reference output
python test/tools/progen/progen.py features                      # the feature switches
python test/tools/progen/progen.py selfcheck --seeds 1-1000 --jobs 6 --out DIR
python test/tools/progen/progen.py diff --seeds 1-2000 --matrix full --jobs 6 --out DIR
python test/tools/progen/triage.py DIR                           # bad cells grouped by cause
python test/tools/progen/progen.py reduce --from DIR --seed 32 --cell 'devirt+inline+dce@nvm' --out OUT --name NAME
python test/tools/progen/progen.py check                         # the batch of the test plan
python test/tools/progen/progen.py findings                      # re-run the archived findings
python test/tools/progen/progen.py fixed                         # the findings recorded as fixed
```

- `--features a,b,c` generates with only those constructs; `--features -macro,-env` removes
  some from the full set. The feature set is part of the random stream: the same seed with
  another feature set is another program.
- `--matrix full` is 25 rewrite sequences x 4 schedulers plus the run without passes (101
  cells); `cross` is the 25 sequences on the node VM and five of them on the other three
  schedulers (41 cells); `quick` is 8 x 4 + 1; `frozen` is P1-P12 and R01-R60 of `benchmarks/pipeline/pipelines.py`
  without the pipelines that name a module pass; `--backends nvm,fvm` and `--cells id,id`
  narrow a matrix.
- `diff` writes `results.jsonl` (one record per program: the outcome of every cell, details of
  the bad ones, whether a bad cell repeated on two more runs), `summary.txt`, and for every
  program with a bad cell `failures/progen_<seed>.cml` and `.json`.
- `reduce` needs `--from DIR` when the failure depends on where the source file is (some
  crashes do); `--repeats N` is for a failure that does not happen on every run. The reduced
  program is written with its expected output, the actual output and how many of 10 runs of
  the written file failed.

## What a generated program contains

Each switch is a feature (`progen.py features`); all but `modules` are on by default and they
nest freely. `modules` is left out of the default set because the feature set is part of every
program's random stream and the recorded seeds must keep naming the same programs; turn it on
with `--features all,modules`.

| Feature | Constructs |
|---|---|
| (always) | `int` arithmetic `+ - * / %` with negative operands (division truncates, the remainder takes the sign of the dividend), comparisons, `&&`, `\|\|`, `!`, `let`, `println`, `format` with `{}` |
| `float` | `+ - * /` and unary minus on values that are exact (see "Limits of the reference"), comparisons, `float(int)`, `int(float)`, `{:.Nf}` placeholders, default float printing |
| `string` | literals, `+`, `len`, `string(int)`, `string(bool)`, `format`, `==`, `!=`, `<`, `<=`, `>`, `>=` (byte order), `join` |
| `array` | literals (of scalars, tuples, structs), index (with and without a length guard), `len`, `concat`, `slice`, `contains` |
| `tuple`, `struct` | literals, `.N` and field access, as parameters, results, array elements and variables |
| `var` | `var` of int, float, string, bool, `int[]`, tuple; `=`, `+=`, `-=`, `*=`, `/=`, `%=` (and `+=` on a string) in sync blocks; reads before and after writes |
| `varparam` | `sync func` with one or two `var` parameters, writes through them, a variable passed on to another function |
| `capture` | lambdas and nested functions that read captured values and variables (seeing later writes) or write them; a function returning a lambda that owns a variable (a counter) |
| `if`, `match` | as expressions with expression or block arms; as statements with `sync` arms that print and write; `match` on an int, string or bool name with literal cases and `_` |
| `func`, `rec` | functions over all the types; recursion bounded by a depth argument (0 to 9): tail, non-tail, tree (two self calls), mutual, and a `sync func` recursion that writes a `var` parameter before and after the call and prints |
| `closure`, `hof` | lambdas, named functions as values, functions that return lambdas, user functions with function parameters (pure, and effectful ones called from sync bodies) |
| `builtin` | `map`, `filter`, `reduce`, `zip`, `range` (pure element functions); `foreach` and `reduce` with element functions that print and write captured variables |
| `effects` | `println` in functions whose body is a sync block, in branch and match arms, in `foreach` / `reduce` element functions |
| `wait` | `wait` before assignments and calls that write (inside sync blocks, where it must not change anything) |
| `env` | one to three values read with `get_env`, which no pass can know |
| `macro` | pure functions declared `macro` (ordinary functions without `std::macro`, evaluated ahead of time with it) |
| `comprehension` | `[e for x in xs if c]` with a pure element expression and condition (the condition is left out in two of five), over arrays of every element type, nested in any expression |
| `nullable` | `T?`: functions that return `T?` (`if c then e else null`) and take one (`x ?? d`); `let o: T? = ...`; a plain value, `null` or a `T?` written where a `T?` is expected (argument, typed `let`, `return`); `a ?? d` and `a ?? b ?? d`, with a nullable right side too; a nullable value printed by itself |
| `partial` | functions with one or two context parameters (`with <k: T>`), called in place (`f<w>(x)`), bound (`let g = f<w>`), and handed to `map` / `filter` / user functions as `f<w>` |
| `ifnoelse` | `if c then e` as a statement of a sync block with one expression as its arm: a `println`, a call with effects, or a pure value of any type that is dropped |
| `modules` (not on by default) | two to four top-level `let`s of scalar types (each a pure expression over literals and the ones made before it), written in a shuffled order and used by functions and by `main`; and the program split into three modules: `<name>_vals` exports the values and the struct types, `<name>_funcs` exports a random set of functions that call only each other and imports what it uses from `<name>_vals`, the entry module imports both by name and keeps the rest. The reference is the same as for the program in one file: the values are evaluated once, before `main` |

The spelling of a program varies without touching the random stream (a seed still names the
same program): `+`, `-` and `*` are written with both spaces, with none, or with one (`a - b`,
`a-b`, `a -b`, `a- b`), and an index into a nested tuple as `t.0.1` or `(t.0).1`. The choice is
a checksum of the operands' text. The lexer once took a sign into the number after it, so that
`let b = a-1` bound `b` to `a` and dropped the -1 silently; with only `a - b` generated, no
batch could see that.

A program ends up at about 115 source lines: one to four pure functions, one to three
functions with effects, and a `main` of 6 to 14 statements that prints most of what it
computed. `--size N` (on `show`, `selfcheck`, `diff`, `reduce`) multiplies the number of
functions and of statements of `main` by N: size 4 gives programs of about 450 lines, size 8
of about 900. A seed names a different program at each size; every recorded seed and finding
refers to size 1.

## Why the reference is valid

Camel evaluates by dependency, so an eager interpreter is only a model of it for programs
whose observable output the design principles (`docs/design-principles.cn.md`) fix to one
value. The generator stays inside that set by construction, and `model.effect_violations`
checks it on every generated and every reduced program:

1. **Every effect is in an effect position.** An effect (`println`, an assignment, `foreach`,
   a call of a function that prints or writes, `reduce` with such an element function) is the
   expression of a statement of a `sync` block, and all enclosing blocks up to the function or
   lambda body are `sync` blocks (principles 2.2). `if` and `match` pass the position on only to
   arms written as `sync { ... }`, and a statement `if c then e` without `else` to the one
   expression `e` that is its arm (2.10: the arm is evaluated as any arm is; only its value is
   dropped). Nowhere else can an effect appear, so "an effect outside a
   sync block whose result is not used does not run" (2.1, misreadings 1 and 3) never decides
   an output. A function that prints has a sync body of its own (2.4).
2. **One effect per statement.** An effectful call is a whole statement (`let r = f(...)`,
   `f(...)`) with pure arguments, so no order between two effects of one statement is assumed.
3. **Element functions of `map` and `filter` are pure** (2.7: their elements have no order).
   `foreach` and `reduce` are ordered and may print and write.
4. **Everything outside sync blocks is pure**, so whether and when it runs cannot be seen.
5. **`wait` only appears in sync blocks**, where it changes nothing (2.6). The ordering that
   `wait` gives outside sync blocks is not generated.
6. **No fault can happen**: no division by zero, no index out of range, recursion depth at
   most 9 (far below 256), no macro evaluation deeper than the limit by construction of the
   depth arguments. A program whose reference evaluation meets one is discarded.
7. **Pipelines are not assumed to commute** (principles section 4, misreading 9). Nothing is
   compared between two pipelines. Each run is compared with the reference, and the claim
   checked is only "a standard pass must not make a determined program print something else".
   A pipeline may decline: a located diagnostic of a pass before anything ran is the outcome
   `REFUSED` and is not a finding.

What the hand probes established and the reference therefore models: a read of a `var` is a
snapshot of its value (`let k = c` does not follow later writes) while a lambda that reads a
captured `var` sees later writes; a `return` inside a block arm ends that arm, not the function;
assignments, `println` and calls written as statements of a sync block run even when nothing
uses them.

## Self-check

`selfcheck` runs each program once without passes and compares with the reference. It tests
the generator and the model above, not the optimizer. A disagreement is first a suspected
generator or model error; it is recorded as a compiler finding only after it is reduced and
checked against the design principles. The numbers of the last runs are in the findings README.

## Outcomes

`runner.py` classifies a cell as `OK`, `REFUSED` (legal), `MISMATCH` (silent wrong result),
`CRASH`, `HANG`, `RUN_ERROR` (a run-stage diagnostic although the program is fault-free),
`ERROR_NO_DIAG`, `REFUSED_UNLOCATED` or `FRONTEND`. All but the first two are bad cells.
After four crashed or hung cells the other cells of that program are `NOT_RUN` (on a system
with a crash handler every crash takes many seconds); they count neither as passing nor as bad.
Every bad cell is run two more times and recorded as stable or not.

## Reuse of `benchmarks/pipeline`

`runner.py` imports from it: how camel is invoked and how its JSON diagnostics array is split
from the program's own output (`run.split_diagnostics`), what a located diagnostic is
(`classify.located`, `classify.errors_of`), and the pipeline sets P0-P12 / R01-R60
(`pipelines.FIXED`, `pipelines.random_pipelines`) for `--matrix frozen`. Its record format,
corpus handling and rule sets are not used: they compare a program with its own zero-pass run
and carry the recorded deviations of that experiment (clock printers, ONNX cells, effect
traces), none of which applies to programs that come with their own expected output.

## Reduction

`reduce.py` keeps a candidate only if it is still in the determined subset, the reference
evaluates it (the expected output is recomputed for every candidate), the run without passes
prints exactly that, and the failing pipeline still fails the same way. Steps: remove
functions; remove statements (chunks, then single); splice an `if` statement's arm in its
place; replace an expression by an operand of the same type or the smallest literal of its
type; drop `macro`; drop passes and try the node VM as scheduler.

Two cautions. A mismatch is reduced as "still a mismatch", so when a program shows two defects
the result can be the other one; reduce again from another seed of the group if the result is
not the symptom you were after. And a candidate with a fault that is visible without running
(a literal index outside a literal array) is rejected, because the design principles make that
a compile-time error.

## The batch in the test suite

`test/plans/feat/opt/progen.plan.toml` runs `progen.py check`: the first seeds whose every
cell of the `cross` matrix passes today (about 15 s). A seed with a known bad cell is left out
and listed in `CHECK_EXCLUDED` with the finding it shows. When a finding is fixed, move its
seed back; when the generator changes the programs of existing seeds, raise
`generate.VERSION` and choose the batch again from a fresh `diff` run. No seed is left out at
present.

The same plan runs `progen.py fixed`: every finding with a `fixed` key in `findings.json`, under
the rewrite sequence it was found with and under `std::inline`, `std::opt`,
`std::inline std::opt::dce`, `std::opt::simplify` and `std::specialize`, on the four schedulers. When a finding is fixed, give it that key.

## What is not covered

- Tensors and every module (`tensor`, `nn`, `autodiff`, `onnx`, `fs`, `json`, `re`, ...); the
  module passes `tensor::fuse` and `onnx::export`; the C++ translation.
- Effect order under parallelism: no effect is generated where the order is open (effects
  outside sync blocks, element functions of `map` / `filter` that print, `para` operators).
  `std::prl` is in the matrix, but only on programs whose output is determined.
- `wait` outside sync blocks (ordering by the use of a variable, principles 2.6), and writes to
  one variable from several places without `wait` (principles section 6, undecided).
- Effects that run on demand: nothing checks that an unused effect outside a sync block does
  *not* run.
- Module-level variables (`var`), imports of whole modules and qualified names (`m::f`),
  mutual imports, namespaces, decorators. (Module-level `let`s and imports by name of user
  modules: feature `modules`.)
- Faults and their diagnostics (division by zero, index out of range, recursion limits, macro
  evaluation failure by depth), `!!`, `?->`, `throw`, `T!`, `match` on a `T?`, arrays of `T?`.
- Generic functions (as callees or as values), partial application of a function value that
  still has context parameters (`g<w>` with `g` a parameter), `var` context parameters, `==` on
  arrays, tuples and structs, a `match` case with several patterns.
- Deep programs: recursion is at most 9 deep and arrays have at most 64 elements. At the
  default size a program also has at most 34 graphs that own a constant object: that was the
  most the compiler could build (finding F24, fixed since), and the filter is kept at size 1
  only so that recorded seeds keep naming the same programs. Larger programs (`--size`) are
  not filtered, but the default batch does not use them, so inlining budgets and graph
  capacity are stressed only by batches run with `--size`.
- Constructs that did not work when probed and are left out so that the reference stays
  reliable: `string(float)` (question J3), and `==` on arrays, tuples and structs (the type
  checker has no such overload). Ordering comparisons of strings, `+=` on a string, `%=` and
  `string(bool)` were left out for findings F27, F28, F29 and F26 and are generated since
  those were fixed (`generate.VERSION` 2); an ordering comparison is no longer parenthesized
  for F30. A statement-form `if` without `else` written with a block arm ends that arm in
  `null` (it used to be `return null`, which the warning ExitInUnusedBranch now reports);
  since the ruling of 2026-10-12 the arm may be one expression of any type, which the feature
  `ifnoelse` writes (`generate.VERSION` 3, with `comprehension`, `nullable` and `partial`).
- Integers near the 64-bit range (programs are discarded when a value leaves +-2^62), bit
  operators, `^`, `i32` / `f32`, `as` / `is`, slices with a step.

## Limits of the reference

- **Floats are exact or absent.** A float operation is accepted only when its result is exact
  in binary, has at most four binary places and is below 2^20 in magnitude; a negative zero is
  rejected. So fused or reordered machine arithmetic cannot change a result, and rounding in
  the default print format is unambiguous; but rounding behaviour itself is not tested.
- **Rejection biases the programs.** About one generated program in two is discarded (a
  rejected value, or too many constant-owning graphs) and replaced by the next attempt of the
  same seed. Programs with long arithmetic chains, large values or many string literals are
  therefore under-represented.
- **The model was fitted to the node VM by hand probes.** Where the language reference and
  the implementation differ, the construct was left out rather than modelled either way. A
  construct that every backend and the model get wrong in the same way would not be noticed.
- **One statement, one effect** means interleavings inside an expression are not explored.
- The seed stream depends on `generate.VERSION` and on the feature set; findings are archived
  as reduced programs with their expected output, not as seeds.
