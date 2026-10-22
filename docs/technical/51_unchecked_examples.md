# 51. The design examples that no guard parses

**The table is a dated snapshot; the sweep below it is live.** Measured on 2026-10-22 at
`e543f8198` (the merge this change sits on; the commits after it only edit docs) with the Linux
build of that commit. The counts in the table belong to that commit and will age; they are not a
live claim about the tree. H211 then built the sweep this page recommended, so the number that
guard checks is the tree's, never this page's.

H188 narrowed the AST snapshot to `test/cases/parse/**` and the probes. The `.cml`
programs under `docs/design/*/examples/` lost the parser check they had only
incidentally, and `test/tools/parsecheck` does not walk them either: `format`/`fuzz`
cover the repository but skip a file that does not parse (`skipped(not accepted)`), so
a broken example is silent. This page records what parses there today.

**Scope.** 203 `.cml` files under `docs/design/*/examples/`, plus the root
`hello.cml`, `test/tools/jitcov/workloads.cml` and the five `.cml` fixtures under
`test/tools/selftest/` (benchmark_failure, sweep/probe, sweep_names alpha, beta and
reuse): 210 files. Method: `camel-parsecheck hashes` and `dump` from
[the H199 tool](../../test/tools/parsecheck/check.py) on the build of `e543f8198`;
"clean" is accepted with no diagnostic, "refused" is rejected and meant to be,
"rotten" is rejected though meant to parse.

| owner | files | clean | refused | rotten |
|---|---:|---:|---:|---:|
| `docs/design/any-typing/examples/` | 38 | 37 | 0 | 1 |
| `docs/design/cpp-memory/examples/` | 31 | 31 | 0 | 0 |
| `docs/design/graph-model/examples/` | 59 | 58 | 1 | 0 |
| `docs/design/loops/examples/` | 19 | 19 | 0 | 0 |
| `docs/design/module-root/examples/` | 56 | 54 | 2 | 0 |
| **design examples** | **203** | **199** | **3** | **1** |
| hello.cml, workloads.cml, selftest fixtures | 7 | 7 | 0 | 0 |
| **total** | **210** | **206** | **3** | **1** |

The same run at the base commit `880ee74b1`, before the one fix below, gave 205
clean / 3 refused / 2 rotten (cpp-memory had 30 clean and 1 rotten). That is the
number the author needs: two examples were already broken.

## The five files that failed the parse at `880ee74b1`

| file | diagnostic | class | evidence and age |
|---|---|---|---|
| `any-typing/examples/t14_union.cml` | `SyntaxDiag::MissingComma` at the union bar (line 3) | rotten, still | The [any-typing page](../design/any-typing/README.cn.md) §2.1 says of this file "用户函数的形参写成 `int \| string` 可以通过声明" ("the parameter form is accepted by a declaration"). `A \| B` was retired in `8e8b68cdd` (2026-10-19, enums) and is an ordinary syntax error since `69b2e58b7` (H26, 2026-10-20): broken for two days. |
| `cpp-memory/examples/defects/d10_tail_loop.cml` | `SyntaxDiag::KeywordAsIdentifier` at `func loop` (line 5) | rotten, fixed here | The [cpp-memory page](../design/cpp-memory/README.cn.md) presents it as a running program ("一个尾递归的循环 `loop` 变成 `f_loop` ...") and D8 reproduces its crash. `loop` became a keyword in `b4b5b34a0` (2026-10-13), before the file's first commit `eb4aff7cf` (2026-10-14): the committed file had never parsed. Renamed to `loop_` in this change; the page's one generated-name mention follows, and the file now parses. |
| `graph-model/examples/ns/p12_use.cml` | `SyntaxDiag::UnexpectedToken` at `use` (line 3) | refused | The plan row `ns.p12_use_retired` says "the word is an ordinary name now, so this is a syntax error" ([namespace_rules.plan.toml](../../test/plans/feat/compile/namespace_rules.plan.toml)); the [graph-model page](../design/graph-model/README.cn.md) §8.2 N6 says `use` is retired. |
| `module-root/examples/misc/top_stmt.cml` | `SyntaxDiag::UnexpectedToken` at `println` (line 5) | refused | The [module-root page](../design/module-root/README.cn.md) §1.2 answers "顶层能写 `println(...)` 这样的语句吗 — 不能，语法错误 ... (`examples/misc/top_stmt.cml`)". |
| `module-root/examples/misc/top_sync.cml` | `SyntaxDiag::UnexpectedToken` at `{` (line 1) | refused | The same page (line 544) says "顶层不能写 `sync { ... }` 块作为初始化表达式 (`examples/misc/top_sync.cml` 是语法错误)". |

## What references them

No fence imports a file by path. Four examples are named inside a `camel` fence
(`t06b`, `t11`, `t13`, `t20`, all any-typing), but three of those fences are excerpts
— on a design page only a fence with `func main` is compiled — and `t20`'s fence
reproduces the code without the module line or the comments. A copy is not the file.
No design example is a case of a plan; `p12`'s construct has a mirror case in the
plan named above. `hello.cml` is run by
[have-native-jit.mjs](../../test/tools/have-native-jit.mjs) and is a compiled fence in
`README.md`; the five selftest fixtures are cases of the selftest plans, run through
[runner.plan.toml](../../test/plans/feat/consistency/runner.plan.toml);
`workloads.cml` is named by no fence and no plan (only by
`docs/technical/07_jit_architecture.md` prose). So at `e543f8198` 204 of the 210 files — the 203
examples and `workloads.cml` — had no check that parses the file, and 199 of the examples were
named by neither a fence nor a plan. H211 closed that gap: the sweep below parses all 210, and
the plan selection now runs it when an example, the checker or one of the seven changes.

## The sweep (H211)

`python3 test/tools/parsecheck/check.py examples` — the test `frontend.examples_parse` in the
fast layer of [frontend.plan.toml](../../test/plans/feat/parse/frontend.plan.toml) — parses every
`.cml` the tree puts under `docs/design/*/examples/`, plus the root `hello.cml`,
`test/tools/jitcov/workloads.cml` and the five fixtures under `test/tools/selftest/`. The
selection is the glob plus those seven named paths, counted from the tree on every run; a named
file that disappears, or a glob that matches nothing, fails the check instead of passing over
less. The parser is the existing [camel-parsecheck](../../tools/parsecheck/main.cpp): its
`hashes` mode decides, and its `dump` mode supplies the diagnostic for a file that fails. No
second parser was added.

The files that are meant not to parse carry the marker on their first line:

```
// @refused-example <reason>
```

The marker is the whole exception list. The checker requires the reason, requires a marked file
to still fail to parse (a marked file that starts parsing is a lie), and requires an unmarked
file to parse; both directions are failures. A marker anywhere but line 1 is reported, not
ignored, so an edit cannot separate the exception from the file it describes. Every marked file
is printed with its reason and the summary counts it, so nothing is skipped silently.

The four marked files are the three deliberate negatives and one open question:

| file | marker's reason |
|---|---|
| `graph-model/examples/ns/p12_use.cml` | `use` is retired; the word is an ordinary name now, so the file is a syntax error |
| `module-root/examples/misc/top_stmt.cml` | a top-level statement is a syntax error (the page names this file) |
| `module-root/examples/misc/top_sync.cml` | a top-level `sync { ... }` initializer is a syntax error (the page names this file) |
| `any-typing/examples/t14_union.cml` | retired-feature probe: the page says `int \| string` is accepted, the file has not parsed since `A \| B` was retired; the page and the file disagree, awaiting the author |

The table above is unchanged — 210 files, 206 clean, 3 refused, 1 rotten at `e543f8198`: the
markers reclassify `t14` from "rotten" to "deliberately refused pending the author", they do not
change what parses. If the author rules that `A | B` is back, the file starts parsing and the
check turns red until the marker is removed, which is the direction that makes the decision
visible in the tree rather than in a list.

What makes the check red: drop a marker from a refused file (reported unmarked, with its
diagnostic), delete a `}` from a clean example (same), add the marker to a file that parses
(reported as a lie), or leave a marker without a reason.

[test/plan-selection.json](../../test/plan-selection.json) names
`feat/parse/frontend.plan.toml` (layered, so `--tier fast` keeps its slow fuzz test out) for
`docs/design/`, `hello.cml`, `test/tools/parsecheck/`, `test/tools/jitcov/workloads.cml` and
`test/tools/selftest/`: editing an example, the checker or a fixture runs the sweep, and the
design pages keep the documentation checks they already selected.