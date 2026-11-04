# 这一批落了什么、撤了什么、还在等什么

2026-11-04，`lane-reds` 把 `integ3/exclude` 这条线推进 `origin/develop`。本页只写可核的数，括注是量它的命令。

## 1. 推了什么

- **`origin/develop`：`34b659107b71abe161784c55c493260b8b94b937` → `25d4ac7ae53306f178098db7d1ba591b86dac529`**，
  快进推（没有 `--force`，没有 rebase）。
- 范围里 **435 个提交、108 个合并**（`git rev-list --count origin/develop..HEAD`、
  `git rev-list --merges --count origin/develop..HEAD`）。**107 个合并把内容带上来，第 108 个
  （`ab9b5bf1a`）什么都不带** —— 它在历史里，内容已经被撤干净（见第 2 段）。
- 净变化 **267 个文件，+19249 / −901**（`git diff --shortstat origin/develop..HEAD`）。
- 闸门：合并树的 tip 上跑全量 `-Tier fast -Jobs 4`（2026-11-04 11:36–11:43，1 分钟 load 9.55 起、
  15.79 落），`total 8762 = 8718 pass / 3 fail / 11 skip / 30 known failures`，`killed 0`。
  三条红是 `matmul_t` 的三条，在真基线 `34b659107` 上同样红（其中一条钉了会漂的节点号：
  `node=6` 对 `node=7`）。

## 2. 撤出了什么、为什么

**撤出 `ab9b5bf1a`（`integ3/backlog-b`，十车道合并）与 `f91893c86`（seal 判据）。**

原因是一条在**真实可跑程序**上成立的 `std::opt` 失败，而不是一条构造用例：

```
Runtime graph 'train_stateful' breaks the branch-arm invariant: OPER 70 (:io/println) reads
OPER 108 (time:now), which lies in an arm of BRCH 106 that may not be taken when OPER 70 runs
```

（`benchmarks/mlp/model.cml`。）

**切断的是"推迟"，不是"修好"。** `closeChainOver` 一侧的图构造没有改，`time:now` 仍然会被放进
一个运行时不保证取到的分支臂里；**chain-place 那批重新落地时，这条红会原样回来** —— 回来以后仍
然要在那一侧修，不是在测试里让一步。

**`f91893c86` 必须与 `ab9b5bf1a` 一起回来**，而且它自己已经被单独撤过一次（`6a097aa68`
`Revert "seal: a sync edge's end may be a statement the graph derives, ..."`）。理由是**编译墙**，
在这棵树上直接量得到：`f91893c86` 调 `camel::runtime::nodeIsChainStatement(graph_, ref)`，而这个
谓词由 `70d3993d3`（`graph: one definition of "a statement of a chain", ...`）引入，`70d3993d3`
**只在 `ab9b5bf1a` 里存在过**（`git rev-list ab9b5bf1a^1..ab9b5bf1a` 命中它，
`git merge-base --is-ancestor 70d3993d3 ab9b5bf1a^1` 退出 1）。当前树上
`git grep nodeIsChainStatement HEAD -- src` 没有任何命中，所以只回 `f91893c86` 编译不过。

撤出的净删除：`0e860af5c` 63 文件 **+184 / −2155**，补齐的 `bf18d9a2b` 再删 20 个页、还原 6 个
文件（派工里写的 −2197 在这棵树上量到的是 −2155，以实测为准）。`ab9b5bf1a` 自身是 91 文件
**+4368 / −352**。

## 3. 被推迟的十条车道

**分支引用已经不在了**（lane 已合、ref 已删，`git log -1 --format=%D` 对这十个提交全空），所以
"分支名"这一列没有；重新落地的句柄是**提交本身**，都在 `ab9b5bf1a` 的第二父侧。

| 车道 | 句柄 | 重新落地时要连带 |
| --- | --- | --- |
| writers 的捕获写 | `b51ccd51a` | `src/passes/trans/cpp/function_emitter.cpp#lowerVariable` 的 `VarAfter` 分支（捕获变量的写要落成对那个变量的写，线性检查跟着它走） |
| remat 标签保留残差 | `1d90d75a4` | `split` 的 remat 保留路径，以及"一个 effect 站什么上"那条记录 |
| fastvm 把 `not` 并进等值 | `953725067` | `feat/compile/equality` 的两条用例必须一起回 |
| `extent_of::<A>()` 折叠 | `bca5c1044` | 折叠点在类型实参还在手上时做，别在 lowering 之后再找 |
| `sum_axis` 正律与余切 | `428ac75cf` | **注意 `617453bec` 已把正律改成 stated 形态**，不要按旧形态贴 |
| 三条 elementwise 斜率 | `bb2131632` | 三个用例 + 引擎与库两条读数 |
| 三条 reduced crash | `3d33d57c8` | `feat/trans/reduced_crashes` **整份计划** |
| `with` 函数的转置判据 | `4b2f502c1` | 判据用例 + 它量出来的读数 |
| array 状态循环提升 + **分支后链位** | `2d5168a65` | **回归就在这一条**，见第 4 段 |
| `mean` 负轴梯度 | `2e26f0b9c` | 引擎与库两条读数（`mean_axis`） |

## 4. 重新落地的前提

1. **先修 `closeChainOver`（`src/runtime/draft_inline.cpp`）一侧的图构造**，让"分支之后的链位"
   不再破坏支配关系。在那之前，这条线上的内容一落地就在 `benchmarks/mlp/model.cml` 上报第 2 段
   那条红，`std::opt` 会在真实程序上退回去。
2. **`f91893c86` 与 `ab9b5bf1a` 同进同出**：它调的谓词只在那一个合并里（第 2 段有量法）。
3. `sum_axis` 的正律在 `617453bec` 之后是 stated 形态，重新落地时按新形态写。

## 我们改了什么判据

四条红里，**两条是把过期的期望更新到"行为变好之后"**，**一条是账本名单少记了一个成员**，还有
**一条是给一条被工作量以外的开销卡住的条目把门槛抬到与工作量相称**。**没有任何一条被改成
`known_fail`、`known_fail_msg`、skip，或者放宽成近似值；没有用 `CASE_EXCEPTIONS`；
`test/cases/runtime/graph_draft_ledger/check.mjs` 与 `graph_draft_ledger.plan.toml` 一个字没改。**

| # | 文件 | 旧值 | 新值 | 为什么 |
| --- | --- | --- | --- | --- |
| 1 | `test/plans/feat/showcase_s2.plan.toml#first_step.tensor_vjp_answers_the_step`（旧名 `#first_step.tensor_vjp_stops_at_split`） | `kind = "negative"`；`expect_exit = 1`；`expect_stdout_contains = ["transforms: split does not build this function", "the port is not a parameter of the graph, so neither half takes it"]`；`expect_stderr_contains` 钉 `REINTERP split` 那两行 | `kind = "behavior"`；`expect_exit = 0`；`expect_stdout_contains = ["first_step tensor vjp loss=10.34375 dL/dw=([[3.75, 13.5], [5.5, 19.5]])"]`；新增 `expect_output_not_contains = ["split does not build this function", "neither half takes it"]`；`expect_stderr_contains` 删除 | 墙倒了（`split` 读得到被捕获值的端口；`stdlib/rulesets/transpose.cml#matmul_t` 把乘积的反向补上），程序开始出数；**用例自己写的判据就是"墙倒那天改钉上面那个数"**。这一行是加强：钉精确值（二进制浮点下精确，无容差）＋ 钉 `format` 打印的形状（pullback 答一元组，所以是 `((...))` 而不是 `(...)`）＋ 断言两句拒绝不再出现 |
| 2 | `test/cases/runtime/graph_draft_ledger/baseline.json`（`lists.files`） | 38 条成员 | 39 条成员：`+ "modules/transforms/rule_expand.cpp"` | **名单少记一个成员，不是多了一个住户**：该文件在 `c6bbd67f5^` 上就已经 `auto &draft = transaction.edit()` 并调 `draft.materializeStaticValue`；名单按整词 `GraphDraft` 数，而 `auto &` 不写类型名。改的是拼写，不是连接。用 `--record --force` 记（`--record` 拒绝写涨了的成员），`docs/handover/reviews/graph-store-step2-b-ledger.cn.md` 新增 §62 说明为什么 |
| 3 | `test/plans/feat/compile/transforms.plan.toml#cross_call_gradient_is_not_zero` | **没有这一行**（用例在盘上，没有任何计划读它） | 一行正例：`expect_exit = 0`；钉 `g(3) = 18.0`、`g'(3) = 12.0`、`flat(3) = 18.0`、`flat'(3) = 12.0`；`expect_output_not_contains = ["g'(3) = 0.0", "flat'(3) = 0.0"]` | 这一行不是被删的：修 `modules/transforms/split.cpp#residualsOf` 的提交把那行 `known_fail` **整条改写成了 `grad_across_a_call`**，`.cml` 留在盘上成了孤儿。**没有恢复 `known_fail` / `known_fail_msg = "g'(3) = 0.0"`**，反过来把两个 `0.0` 拼写钉成"不许出现" |
| 4 | `test/plans/feat/para/core.plan.toml#file_chunk_hash` | `timeout_ms = 10000` | `timeout_ms = 60000` | 这条红的数是**线程池的 `sys`，不是哈希**：同一程序换 `std::nvm`（无池）0.11 s wall / 0.11 s CPU，`std::prl` 1.7–3.9 s CPU 其中 `sys` 1.4–3.6 s、**wall 始终 0.2 s**，用例自己报 `Chunk hashing completed in 0.09–0.22 seconds`；10000 只留 4× 的 2.5 s，在 load 15 的宿主上红了而用例一字未改。60000 = 4× 15 s，与计划里其它启动主导条目同档，仍未放宽任何断言 |

**可能被误读为"改判据"、其实不是的三件事**：

1. `docs/handover/questions/README.md` 从 61 行变 49 行再变 50 行 —— **生成物**，用
   `node scripts/check-doc-links.mjs --questions-index --write` 重建，没有手写。
2. 20 个页删除、6 个文件还原到 `ab9b5bf1a^1`（含 `docs/tutorial/images/10_var_order.dot|svg`、
   `docs/design-principles.{cn,en}.md`）—— **补齐不忠实回退**，是"把没落地的批次撤干净"，
   这些路径没有任何测试把它们当期望值。
3. 两个用例的**注释**改写（`cross_call_gradient_is_not_zero.cml`、`first_step_tensor_vjp.cml`）
   —— 注释不是判据；改的是"当时说什么/现在说什么"的记录，判据全在计划行里。