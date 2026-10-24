# 独立审计：5 条没有归属文件的红，和一次「比审计基线晚一次合并」的报绿读数

h484（翻转审计车道）在自己的树上复算集成车道报上来的账。审计基线是
`handover/h465-flip-integration` @ `aa8f710d5`（2026-10-25 05:26:21），按口径先
`git merge --no-ff origin/develop`（当时 `de9b54c86`，合并后 `c9be74f1f`）。审计期间该分支
推进到 `2dde76da8`，于是同一条命令在推进后的树上又量了一次（合并 develop `1df5d16da` 后
`3aeacd6ba`）。本文件只写问题与读数，不改任何源码、期望或计划。

## 一、两次读数（同一条 `camel-remote.ps1`，`-Tier fast -Jobs 8`）

| 树 | 命令 | `[SUMMARY]` |
|---|---|---|
| 审计基线 `c9be74f1f` = `aa8f710d5` + develop `de9b54c86` | `-Plan smoke,test/plans/feat` | total 6210, pass 6172, **fail 19**, skip 11, known 8 |
| 同上 | `-Plan .` | total 6200, pass 6162, **fail 19**, skip 11, known 8 |
| 推进后的 tip `3aeacd6ba` = `2dde76da8` + develop `1df5d16da` | `-Plan smoke,test/plans/feat` | total 6210, pass 6174, **fail 17**, skip 11, known 8 |

- 两个口径差 10 条 = smoke 的 fast 层（`test/runner.mjs:82`：`.` 解析成 `test/plans/feat`
  一个目标、不含 smoke），两次的 19 条红**逐名相同**。
- 基线 → tip 的红名单差集只有两条，都是转绿，**0 条新红**：`runner.backend_matrix`、
  `string_refused`。

## 二、审计基线上「报了绿但仍红」的两条：是基线落后一次合并，不是假读数

| 条目 | 基线 `aa8f710d5` | tip `2dde76da8` | 修复提交（在 tip 上、不在基线上） |
|---|---|---|---|
| `feat.consistency.runner runner.backend_matrix` | `[FAIL]`：自测第一行 `plan-selection: 6 tracked file(s) match no rule … notes/*.py (notes/h419-after-collect.py …); notes/*.log …; notes/*.txt …` | `[PASS]` | `f020350b5`「Drop the six scratch files under notes/ that no plan selects, so the runner's self-test can pass」 |
| `feat.modules.tensor.dtype string_refused` | `[FAIL]`：`output does not contain 'the element type is f32, f64, bf16, f16, i32, i64 or bool, not 'string''`，实测已是 `src/core/type/composite/array.cpp:308` 的「the element type of a Tensor is a numeric scalar (i32, i64, f32, f64, bf16, f16, bool, c64 or c128), not 'string'」 | `[PASS]` | `f521af9f8`「Re-derive the two tensor readings the array alias and the one print form moved」 |
| `feat.modules.tiles cast_like_aliases_the_same_element_type` | `[PASS]` | `[PASS]` | 同上 |

`git merge-base --is-ancestor f020350b5 aa8f710d5` 返回 1：两条修复都不在审计基线上。所以
h480 的转绿清单在它自己的树上成立、在审计基线上不成立，差别是**基线选在 h480 合并之前**。

`runner.backend_matrix` 的另一半事实：那 6 个草稿（`notes/h419-{after,base}-{collect.py,layer.log,reds.txt}`）
是集成分支自己提交的（`5486745c2` 等，2026-10-24），`origin/develop` 上一条都没有
（`git ls-tree -r --name-only origin/develop -- notes` 计数 0）；判定代码
（`test/tools/selftest/run.mjs`）与规则表（`test/plan-selection.json`）在 develop 与本树
逐字相同，所以这条红只由集成分支的草稿产生，与 develop 无关。

## 三、合并树上还没有任何归属/问答文件的 5 条红（tip `3aeacd6ba` 读数）

| 计划 / 用例 | 首个差异（逐字） | `docs/handover` 里检索 |
|---|---|---|
| `feat.modules.graph_facts rewrite_inline` | `verification output: Error: missing: FACTS seeded core.shape 13` | 0 处 |
| `feat.modules.graph_facts rewrite_opt` | `verification output: Error: missing: FACTS graph main valid=0 revalidated=0 stale=11` | 只在一份旧 review（`H8.md`）里出现过别的 `rewrite_opt` |
| `feat.modules.graph_facts rewrite_macro_simplify` | `verification output: Error: missing: FACTS graph main valid=0 revalidated=0 stale=11` | 0 处 |
| `feat.modules.graph_facts persistence_round_trip` | `verification output: Error: missing: FACTS exported core.shape 13` | 0 处 |
| `feat.modules.rules rules.check_program_parameter` | `verification output: Error: the program's own type joins the candidate set: sampled add_zero: proposition equality, evidence samples 100, fired 8 of 8, coverage integer scalar int: (x + 0) = x …` | 0 处 |

读数的性质：这 4 条 `graph_facts` 的计划与用例相对 `origin/develop` **一字未动**
（`git diff --name-status origin/develop HEAD -- test/plans/feat/modules/graph_facts.plan.toml`
与 `test/cases/modules/graph_facts` 为空），而翻转改到了 facts 的实现
（`include/camel/runtime/core_facts.h`、`src/runtime/core_facts.cpp`），所以这 4 条是翻转
造成的读数位移、不是用例本身过期；`rules.check_program_parameter` 的验证脚本报的是
「程序自己的类型进了候选集」。

## 四、要什么

1. **集成车道的转绿清单请连同它量它的那个 tip 哈希一起报。** 本次两条差异全部来自基线落后
   一次合并（第二节），不是读数造假；但按「以审计基线为准」的口径它就是不成立。
2. **第三节 5 条请在树里指定归属**（某个 lane 去修，或裁定为已知红并说明它钉的是哪条设计）。
   其余 12 条红在本树里已经各有问答或报告文件（`nn.lrit`/`mnist` = h450 的裸 `Tensor` 与
   `f32[?, ?]` 混用、`static_types`、`export_fold`、`docs.snippets_compile`、
   `print_and_recompile`、`declaration_evaluator`、以及 h446 钉住的
   `loop_grad_tensor_iterable.{runtime.nvm,macro.fvm}` 真实缺陷）。

## 五、第二轮复核：第三节的 5 条已被回答，本文件的两个问题清零

集成树推进到 `db6bf729c` 后（+ merge develop `3f235254f` = 审计树 `604c60778`），同一条命令复量：

| 口径 | `[SUMMARY]` |
|---|---|
| `-Plan smoke,test/plans/feat -Tier fast -Jobs 8` | total 6214, pass 6185, **fail 10**, skip 11, known 8 |
| `-Plan . -Tier fast -Jobs 8` | total 6204, pass 6175, **fail 10**, skip 11, known 8 |

两个口径差 10 = smoke 的 fast 层：`test/plans/smoke/` 只有一个计划 `core.plan.toml`（10 条
`[[tests]]`）。`all` 才是整棵树：`test/runner.mjs:38` 的 `all: PLANS_ROOT`；`.` 之所以落到
`test/plans/feat`，是 `test/runner.mjs:59` 把前导 `.` 剥成空串后名字与斜杠两条路径都不命中，
于是 `:78-79` 的 `['feat', ...]` 兜底命中 `test/plans/feat` 目录（单目标、不含 smoke）。
两个口径的红名单逐名相同（10 条）。

相对第一轮基线（19 红）：**9 条转绿、0 条新红**。第三节那 5 条全部转绿，红留给了作者：

| 第一轮的红 | 现读数 | 谁回答的 |
|---|---|---|
| `feat.modules.graph_facts` 的 rewrite_inline / rewrite_opt / rewrite_macro_simplify / persistence_round_trip | `[PASS]` | `739fd24e0`「the FACTS records are re-read from the tool; no assertion was widened」 |
| `feat.modules.rules rules.check_program_parameter` | `[PASS]` | 同上：候选集里的 `sum_swap<Tensor>` 随 H466 的 `load_npy` 声明消失，断言改为 `sum_swap<i32>` 并加了更强的否定断言 |

两条「必须仍红」的仍红，且期望自第一轮基线起**一字未动**（
`git diff aa8f710d5 HEAD -- test/plans/feat/modules/onnx.plan.toml | Select-String export_fold`
与 `… -- test/plans/feat/modules/rules.plan.toml | Select-String tensor_saturation` 都是空）：

- `export_fold`：`expected exit 0, got null`，导出没写出模型（`ENOENT …/export_fold.onnx`）；
  `88ed6f8ad` 已把它钉到 `bb4e01be8`（4096 定点的守卫）并落成问答。
- `rules.tensor_saturation_keeps_values`：`expected exit 0, got 1`，
  `Pass 'rules::saturate_all' failed: error RewriteInvalid: Two writings of a value have the same
  type, region and permissions…`；`739fd24e0` 已把原因（两个一类型按地址比较、类型特化键持有
  按调用点构造的 `Type*`）写进问答。

其余 8 条红各有归属：`nn.lrit`/`lrit`/`nn.mnist`/`mnist`（`50305479f` 的提问文件）、
`print_and_recompile`（h485 未合）、`declaration_evaluator`（`7b6b93cbb` 只把消息写成
`dtype differs (kernel f64, declaration f32)`；红的原因是验证脚本要求零 `EVAL_MISMATCH`，
问题写在 `rank-zero-array-and-a-float-literal.md`）、
`loop_grad_tensor_iterable.{runtime.nvm,macro.fvm}`（h446 未合）。第一轮第二节那两条
「报了绿但审计基线上仍红」的，本轮两棵树上都是 `[PASS]`。本轮快照里 h485 / h487 / h488 /
h446 四条 lane 还没合进来，它们的转绿清单未复核。