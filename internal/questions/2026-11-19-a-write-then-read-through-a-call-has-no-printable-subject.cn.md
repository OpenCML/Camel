# 「调用写入、其后读到该版本」这个读法今天没有可打印的题材
2026-11-19 ｜ 来自 `lane-gate-reds-a` ｜ 对象：`test/tools/roundtrip-compare.mjs` 的 `CASES`、`test/plans/feat/trans/cml.plan.toml#print_then_run_agrees_with_the_origin`

## ① 事实：三种写法都被关掉了，不是一条能修的缺陷

`print_then_run` 原来用 `effects/writer_table_through_callee` 量「一个调用写了某个状态，调用之后的读拿到它留下的那个版本」。三种写法今天都不通：

| 写法 | 谁拒 | 出处 |
| --- | --- | --- |
| 闭包赋值它捕获的局部 `var` | 前端 | `81fa0bac9`（2026-11-16，DP 3.5 由计划变成规则）：`SemanticDiag::ClosureAssignsCapturedName`；该用例自己已因此改成 `state::ref`，头注释写着「the shapes are refused now」 |
| `state::ref` 句柄（现行唯一合法写法） | `std::cml` | 子集 S2 不印 `Ref`：`CmlUnsupportedConstruct 0x030001A5`，「a value has the type 'Ref<int>', which std::cml does not print」 |
| 模块级 `var`（`effects/module_var_write.cml`、`effects/two_calls_through_values_keep_both_writes.cml` 都是这个形状） | `std::cml` | 打印器对顶层 `var` 直接 `fail(ref, "a top-level 'var'")`（`src/passes/trans/cml/cml.cpp#emitVariable`，`isRoot_` 分支） |

⇒ 这个读法现在**没有任何**能为 `std::cml` 写出来的题材。不是某一条用例选错了文件。

## ② 损失

`print_then_run` 从 `ROUNDTRIP cases=9 identical=9 differs=0` 变成 `identical=7 differs=2`，其中一条（`effects/callback_write_ignored`）是**永远测不到的条目**：它让 `differs` 永远至少是 1，于是**真回归与已知缺口分不开**——这正是 `roundtrip-compare.mjs` 文件头写明它自己最怕的失败模式。

（另一条红 `linear/runtime/order_edges_regions` 与本页无关，见 `1e8e390b6` 里那句「A printer rule that keeps a value's name is another piece of work」。）

## ③ 两条路及代价

- **（甲）把这一条从清单里摘掉**：`CASES` 9 → 8，金标 `cases=9 identical=9` → `cases=8 identical=8`，并在两处注释里写明「是哪次判例撤掉了题材」（`81fa0bac9`，以及 `std::cml` 的 `Ref`／顶层 `var` 两条拒绝）。代价：`std::cml` 的往返覆盖少一个读法，而它对「调用写状态」这个家族的测量同时依赖别处（`feat/linear`、`feat/effects` 的行为用例仍在量语义）。
- **（乙）扩 `std::cml` 的可打印子集以容纳 `Ref`**（顺带容纳顶层 `var`）。代价：打印器子集是另一条线的活（`docs/technical/60_cml_print_roundtrip_gap.md` 已经在排它），会牵动已有金标，且今天主机与人力都在 S1 上。

## ④ 默认建议（写死，本轮就按它开工）

**采（甲）。** 理由只有一条，且是不可回避的：**一个永远测不到的条目比没有这条更糟**——它把 `differs` 钉在 1，真回归与已知缺口从此分不开；而（乙）的代价今天给不起。

若作者另有裁定，改这一页即可；在裁定之前本轮按（甲）做：`CASES` 摘掉 `effects/callback_write_ignored`，`cml.plan.toml` 的期望行改成 `cases=8 identical=8 differs=0`，两处注释写死撤掉题材的那次判例（`81fa0bac9`）。**不把任何红挪进 `known_fail`／`skip`**：这是把测不到的条目从清单里摘掉，并留下它为什么消失的记录。