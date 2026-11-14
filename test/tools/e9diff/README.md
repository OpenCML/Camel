# e9diff: the E9 differential, in three tiers

The campaign of the CC paper's E9: random programs from `test/tools/progen`, every one of them run
under a matrix of rewrite pipelines, back ends and collectors, and every run compared with the
output the generator's independent Python evaluator computed. A run that prints something else is
a wrong value; a run that declines without a source position is a refusal; a run that dies, hangs,
fails without a structured diagnostic or produces C++ the compiler rejects is a crash. Those are
the three numbers a merge is judged by, and `baseline.json` here holds them for the "only down,
never up" ratchet.

It lives in `test/tools/` with its own `baseline.json` and no plan of its own for the two slower
tiers: a full run is 25 minutes on this host, and a gate that starts one by itself is a gate people
learn to skip.

## The three tiers

Measured on the 32-core host, `--jobs 4`, one program at a time in each of four threads, with the
1-minute load between 10 and 14 (that is: not a quiet machine, so treat these as upper bounds).

| Tier | What runs | Cost | When |
|---|---|---|---|
| **fast** | `test/cases/opt/e9/` with `test/plans/feat/opt/e9.plan.toml`: the reduced programs of the campaign, each with no pass and through `std::opt` on the four back ends (8 cells per program) | 1 s for three cases (24 cells) | every merge (it is an ordinary plan, `tier = "fast"`) |
| **slow** | `run --matrix slow`: 50 seeds, the 11 pipelines on `std::nvm` (generational) and `std::prl` (mark-sweep), 22 cells per program | 20 s; 748 cells, because 16 of the 50 seeds are generator rejects | on a branch, when a reading is wanted and half an hour is not |
| **full** | `run --matrix full`: 500 seeds, all 99 cells (11 pipelines x 4 back ends x 2 collectors + `std::cpp`) | 25 min (1,507 s); 31,680 cells, 320 programs, 180 generator rejects | before a release, after a change to an optimization pass or a back end, or overnight on an idle machine. **The ratchet is this one, and nothing starts it automatically.** |

The slow tier gives each of its two back ends one collector and covers both that way (nvm
generational, prl mark-sweep): half of the collector matrix for a twenty-second run. Somebody who
wants the other half can run `--matrix full --n 50`.

## Usage

```
python test/tools/e9diff/e9diff.py run --matrix slow --out DIR            # 50 seeds, 22 cells each
python test/tools/e9diff/e9diff.py run --matrix full --out DIR            # 500 seeds, 99 cells each
python test/tools/e9diff/e9diff.py analyze --out DIR
python test/tools/e9diff/e9diff.py reduce  --out DIR --max-groups 3
python test/tools/e9diff/e9diff.py collect --from DIR/e9_reduced          # into the fast layer
python test/tools/e9diff/e9diff.py collect --from DIR/e9_reduced --dry-run
```

`--camel PATH` before the subcommand names the install every run measures; without it, this tree's
`out/latest` (the rule is `test/tools/camel_arg.py`). `--out` must be outside the tree: a run
writes `e9_cells.jsonl` (one record per program), `failures/` (the programs with a finding, in the
shape `triage.py` reads), `e9_run.json` and, after `analyze`, `e9_summary.json` and
`e9_failing.jsonl`; after `reduce`, `e9_reduced/` and `e9_reduced.json`.

`run` prints, in this order, the matrix, the pin, the seed range, the three numbers and the
seconds it took. Exit 0 when the baseline held (or was not comparable), 1 when a number rose.

## The matrix and the judgment

Pipelines (`p00`..`p10`): no pass; `std::macro`; `std::macro` plus the first k of
`devirtualize specialize inline fold cse algebra dce order`, each as its own `std::opt::<name>`
pass (one round of `simplifyGraph`, `order` last); and `std::macro std::opt` (the integrated
fixpoint). Back ends: `std::nvm`, `std::fvm`, `std::jit`, `std::prl`, each under `--gc
generational` and `--gc mark-sweep`, plus `std::cpp` (translated by `camel ... std::cpp`, built by
the C++ compiler through `test/tools/cppexec/run.py`, run with no collector).

| Outcome | Class | Why |
|---|---|---|
| `MISMATCH` | **wrong value** | exit 0 and another output than the model's: the silent wrong result |
| `REFUSED_UNLOCATED` | **refusal** | a rewrite pass declined without a source position (`PassInternalError`, or after output) |
| `CRASH` `HANG` `ERROR_NO_DIAG` `CPP_COMPILE_ERROR` | **crash** | a signal, no result at the limit, a non-zero exit without a structured diagnostic, or C++ the compiler rejected |
| `OK` | — | exit 0 and exactly the model's lines |
| `REFUSED` | — | a located diagnostic of a pass before anything printed: the legal way to decline |
| `RUN_ERROR`, `FRONTEND` after the first cell | finding, beside the three numbers | a run-stage diagnostic although the program is fault-free; a program the front end rejects on a later cell |
| `FRONTEND` on the first cell | generator reject | the generator wrote something the compiler does not accept; listed, not in the matrix |
| `NOT_RUN` | — | six cells of the program had already crashed |

The `std::cpp` column is classified by the same reader as every other cell (`runner.run_cell`), so
a graph that pass refuses arrives as the located diagnostic it is -- `RuntimeDiag::TranslationRefused`
with a source position, counted `REFUSED` -- and not as a failure nobody can place. `runner.run_cell`
keeps sixty lines of what a cell printed and the compiler needs all of them, so the C++ of a
translation that succeeded is read from one more run of the same command; the outcome is then
cached by the digest of that source, because two pipelines of one program write the same C++ when
their passes leave the same graph.

A program counts once in every class it reaches, so the three numbers can add up to more than the
number of failing programs. Both are reported (programs and cells).

## Root cause

`analyze` gives every failing program one key, the campaign's: the **first failing pipeline on the
node VM** (`pipeline:std::opt::fold`), or, when the node VM is clean on every prefix,
`backend:<the back ends that fail> first bad at <stage>`. The outcome (and its diagnostic) is part
of the key, and the summary lists each key with its program count and seeds, plus the counts per
pipeline, per back end, per collector and per column. `reduce` then shrinks one representative per
key with `test/tools/progen/reduce.py`.

## The ratchet

`baseline.json` holds the three numbers of a **full** run. `run` compares a full run over the same
seeds with it: a number that rose exits 1, a number that fell is printed as news. A slow run is
never compared (it is another sample of the generator, not a smaller trend), and a run over another
seed range is not compared either -- both say so instead of reporting a silent "held".
`--update-baseline` records a new full run, and the numbers of the run that moved it belong in that
commit's message.

## What it reuses, and what the port replaced

| The reference implementation's `common` | Here |
|---|---|
| `SRC` | `runner.REPO_ROOT` (`test/tools/progen/runner.py`) |
| `CAMEL` | `--camel`, else `runner.camel_exe()`: `test/tools/camel_arg.py` |
| `PINDIR` (`$CAMEL_HOME`) | `install_root()`, the install `runner.run_cell` names to its children |
| `ENV` | the environment `runner.run_cell` builds; `child_env` for the `std::cpp` cell |
| `run(...)` -> rc/out/err/timeout | `test/tools/cppexec/run.py:run`, plus its `find_compiler` and `build_command` |
| `pmap` | the `ThreadPoolExecutor` of `test/tools/progen/progen.py` |
| `write_json`, `write_jsonl` | the writes `progen.py` does |
| `EVAL`, `OUT` (work, output) | `--out` and `--out/work` |
| `generate`, `emit`, `emit_modules` | `test/tools/progen/generate.py`, `model.py` |
| `Reducer` | `test/tools/progen/reduce.py` |

The one thing that is not reused is the matrix itself (99 cells, `std::cpp` among them) and the
three numbers, which are this tool's subject. A failing program is written to `--out/failures/` in
the shape `progen.py` uses, so `test/tools/progen/triage.py DIR` groups its cells too.

## Collecting into the fast layer

`collect --from DIR` takes the `rc*.cml` of a reduce run and, for each one, proves the claim the
fast layer makes before installing it: the same output with no pass and through `std::opt` on
`nvm`, `fvm`, `jit` and `prl`. A program that is green is copied to `test/cases/opt/e9/` and gets
its two tests appended to `test/plans/feat/opt/e9.plan.toml`; a program that is not is printed with
the cell that still fails, which is the inventory of what the fast layer is missing and why (a case
that is red the day it lands is not a case). `--dry-run` prints the same verdicts and writes
nothing. Exit 1 when a program could not be collected.

## What it does not cover

What `test/tools/progen` does not generate (its README lists it), module passes, tensors, and the
order of effects under parallelism -- the providers of those are that tool's and `benchmarks/
pipeline`'s business. A `std::cpp` cell is compared with the model like any other, but `reduce`
does not shrink one: `reduce.py` runs the camel back ends only.