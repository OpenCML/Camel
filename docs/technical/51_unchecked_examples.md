# 51. The design examples that no guard parses

**Dated snapshot.** Measured on 2026-10-22 at `e543f8198` (the merge this change
sits on; the commits after it only edit docs) with the Linux build of that commit.
Every count below belongs to that commit and will age; it is not a live claim about
the tree.

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
`docs/technical/07_jit_architecture.md` prose). So 204 of the 210 files — the 203
examples and `workloads.cml` — have no check that parses the file, and 199 of the
examples are named by neither a fence nor a plan.

## Recommendation

A 15-line sweep (one corpus list, `camel-parsecheck hashes`, fail on `no`) would have
caught both rotten files and would catch the next language change that breaks an
example; it would falsely flag the three deliberate negatives (`p12`, `top_stmt`,
`top_sync`) unless they are allowlisted, and `t14` too if the author keeps it as a
retired-feature probe. It would not catch semantic rot: the 38 any-typing examples
parse and do not compile by design (the page's opening paragraph says `any` is an
unknown type name today). 200 examples are meant to parse (203 minus the three
refused); two were broken at the base commit, one (d10) is fixed in this change, so
the tree this page rides on has 206 clean / 3 refused / 1 rotten. The files actually
at risk are those 200 plus `workloads.cml`: the sweep's value is preventing the next
silent break, not repairing today's one.