# RQ3 · library size (`tab:libraries`) — preliminary

`tip 353d36e3d1` (`lane/cc-eval-prelim-2`; its sources are identical to the first reading's `fd950804d6` —
`git diff --name-only fd950804d6 353d36e3d1 -- src` is empty) · build host `zjwei@camel-work` ·
`[load]` ≈30 (1m)（其中约 3 核非 Camel：qemu/sidekiq/llm_server）· 2026-11-28 · **preliminary, not final**.

## Instruments, named

- **`lines` = `wc -l`**: the number of newline bytes in the file, i.e. physical lines, comments and blank
  lines included. Measured per file with `[regex]::Matches((ReadAllText $f), "`n").Count`.
- The first reading of this table used **`Get-Content $f | Measure-Object -Line`**, which counts **non-empty**
  lines (an empty string contributes 0). Both figures are in `A-libraries.csv`, so the difference is visible
  rather than argued: the gap per category is exactly its blank lines (399 autodiff, 369 rulesets, 705 ONNX,
  326 rewrite, 464 `std/cxx`, 68 `opt.cml`).
- **Declarations**: `git grep -c '^export rule'` and `git grep -c '^export func'`, reported **in two columns**.
  They are not one number: `int8.cml` declares 1 `export rule` and 0 `export func`, so a single
  "43 export func" column could not have covered it — and 43 is the count for the three autodiff rule sets
  only, not for the ruleset directory.
- One row per file, with the C++ file it replaces, is `A-libraries-files.csv` (73 files).

## Every category, traced to its files

| category | files | lines (`wc -l`) | declares | replaces |
|---|---|---|---|---|
| `autodiff_cpp` | `src/stdlib/autodiff/**` (12 files) + `transforms/jvp.cpp` 3509 + `transforms/split.cpp` 2676 | 12530 | — | is the C++ side |
| `autodiff_rulesets_cml` | `src/stdlib/rulesets/jvp.cml` 2024, `transpose.cml` 1641, `vmap.cml` 664 | 4329 | 187 rule / 43 func | `transforms/jvp.cpp` + `jvp_operator.cpp`; `transforms/transpose/**`; `transforms/vmap.cpp` + `vmap_operator.cpp` |
| `quant_ruleset_cml` | `src/stdlib/rulesets/int8.cml` 71 | 71 | **1 rule / 0 func** (`export rule matmul_int8`) | **—** : no C++ counterpart in this tree |
| `onnx_export` | `src/stdlib/onnx/**` (18 files) + `onnx.cml` | 13585 | — | is the C++ side |
| `rules_rewrite` | `src/stdlib/rewrite/**` (10 files) | 6725 | — | is the C++ side |
| `cxx_opt_lang` | `src/stdlib/std/cxx/**` (27 files) | 8768 | — | is the C++ side |
| `opt_cml` | `src/stdlib/opt.cml` 552 | 552 | 0 rule / 6 func | `src/stdlib/std/cxx/opt/**`, the four rewrites |

Cases of the quantization rule set: `test/cases/modules/rules/quant_int8.cml` (69 lines by `wc -l` / 65 by
`Measure-Object -Line`) and `quant_int8_without_change_precision.cml` (41 / 39); the plan that runs them is
`test/plans/feat/modules/rules_quant.plan.toml`.

**Excluded on purpose:** `src/stdlib/rulesets/probe_batch.cml` (26) and `probe_missing.cml` (22) are probe
files; they are in no category and in no row of `A-libraries-files.csv`. The old glob
`src/stdlib/rulesets/**` would have counted them (48 lines of the old 4071), which is the second reason the
old number was wrong.

## What changed against the first reading, and why

| category | old (`fd950804d6`) | new | the difference is |
|---|---|---|---|
| `autodiff_cpp` | 12131 | 12530 | **instrument**: 399 blank lines (the old tool skipped them) |
| `autodiff_rulesets_cml` | 4071 with "43 export func" | **4329** (three files) | **scope**: 4071 covered 6 files including the two probes (48) and `int8.cml` (71), so 4071 ≈ 4448 − 377 blanks; the category now holds the three autodiff rule sets only |
| `quant` | — ("no path matched `quant`") | **71**, 1 rule / 0 func | **a wrong glob, not an absent library**: `src/stdlib/**quant**` cannot match a file named `int8.cml`. The `—` was my error and is retracted |
| `onnx_export` | 12880 | 13585 | instrument: 705 blank lines |
| `rules_rewrite` | 6399 | 6725 | instrument: 326 blank lines |
| `cxx_opt_lang` | 8304 | 8768 | instrument: 464 blank lines |
| `opt_cml` | — ("no `.cml` under `std/cxx`") | 552, 6 func | **wrong place looked in**: the Lang-side rewrite is `src/stdlib/opt.cml`, which the old glob `std/cxx/**/*.cml` could never find |

**No line of `src/` changed between the two readings** (`git diff --name-only fd950804d6 353d36e3d1 -- src`
is empty), so every difference above is either an instrument or a scope error in the first reading — none of
them is a change in the code.

## Direction against the design

The Camel rule sets replace the autodiff transforms at **4329 lines against 12530** (35%) and the four rewrites
at **552 against 5504** (10% of the C++ implementation surface, from the B reading). The quant rule set (71
lines, 1 rule) has **no C++ counterpart in this tree**, so it is additional capability rather than a
replacement, and the table says so instead of inserting it into the autodiff category.

## Unexpected

(a) The first reading's two "—" rows were both **globs that could not match**, not facts about the tree; a
reading that reports "nothing matches" without listing the files it looked at can hide exactly this.
(b) `int8.cml` is the only ruleset that declares a `rule` and no `func` — the two declaration kinds are not
interchangeable, and a single column would have reported it as zero.
