# 十车道批次欠下的 49 条红

**读数出处**：树 `D:\Projects\Camel-wt\chainplace`（宿主 `/home/zjwei/camel-work/chainplace`），分支 `lane-chainplace`，`19e6876a6`（`ab9b5bf1a` 的整块内容 + `f91893c86`，父提交 `d7f15f2b3`），`-Plan test/plans/feat -Tier fast -Jobs 4`，2026-11-04T12:41，load 1m 25.29（宿主 32 核跑着另外八个车道，读数不可与静场比）：

```
[SUMMARY] total: 8777, pass: 8684, fail: 52, skip: 11, known failures: 30
```

`d7f15f2b3` 自己读 `8762 = 8718 / 3 / 11 / 30`。**这 52 条里 3 条是 `matmul_t` 的既有红（逐条同名），另外 49 条是这批内容带进来的**，本页把它们点清楚，交给各有主的车道，**不在本轮修**。

同一棵树、同一套参数、**加上 `handChainPlaceTo` 的修**再跑一遍读 `fail = 54`，多出的两条是 `json.accessor_error.opt` 与 `var_read_before_write.opt_nvm`，两者都是 `sync chain forks at node N`——修法第一版"跨块就跳过"造成的（见第 5 节），与本页的 49 条无关。

## 1. 簇表

每条簇写：红的用例名、症状一句、指向批次里的哪个提交（**未定位就写"未定位"，不猜**）、属于哪条被推迟的车道。

| 簇 | 用例 | 症状 | 指向 | 车道 |
| --- | --- | --- | --- | --- |
| `var_capture.*`（10 条） | `var_capture.opt`、`.opt_fvm`、`.opt_jit`、`.opt_tf`、`.macro_opt`、`.macro_opt_jit`、`.macro_opt_tf`、`.macro_simplify`、`.simplify`、`.simplify_jit` | 全部 `exit 1`，`std::opt`（或 `std::simplify`）报 `breaks its edge invariants: sync chain forks at node N: statements A, B follow it` | 未定位（症状与 `var_capture` 的捕获写无关：是链分叉，不是值错） | `writers` 的捕获写（`b51ccd51a`）——**症状指不到它**，但只有它动这个用例 |
| `opt.camel_passes.*`（6 条） | `camel_passes.cse.by_cpp`、`.cse.by_camel`、`.cse_written_variable.by_cpp`、`.cse_written_variable.by_camel`、`.dce.by_cpp`、`.dce.by_camel` | `by_cpp` 是 `snapshot differs from golden`；`by_camel` 是 `verification exited 1`（`graph_equal.mjs` 抛"the Camel pass's graph is not the graph <golden> records"） | 未定位 | 未定位（通用 `std::opt` 与 Camel 侧同一遍的两条腿） |
| `sync_literal_operands.*`（3 条） | `.macro_opt.nvm`、`.macro_simplify.tf`、`.simplify.fvm` | `exit 1`，`sync chain forks at node N`（一条里同时报 `17` 与 `15` 两处） | 未定位 | 未定位 |
| `keep_shapes.real_pipeline.*`（4 条） | `.nvm`、`.fvm`、`.jit`、`.prl` | `expect_stdout_exact` 不符：`KEEPSPOTS keeps=2 in_chain=0 out_of_chain=2 waited=0` 读成 `keeps=0 in_chain=0 out_of_chain=0 waited=0`——**一条保形语句都没走到** | 未定位（`std::opt` 的保形路径，`keepThroughValue` 一族） | 未定位 |
| `opt.edges`（3 条） | `gate_waits_on_the_sync_chain`、`a_chain_end_hands_its_value_on_to_two_blocks`、`the_five_user_sets_read_the_same_on_both_sides_of_the_freeze` | 测试遍用 `draft` 造图，封存时 `EDGES refused: … sync edge 3 -> 5 starts at a node no 'sync' block wrote (kind 2, flags 0, data type 'f32')`——**边从纯值起** | 未定位（`src/passes/test/edges.cpp` 与 `f91893c86` 的"两端都可派生"只放宽了**端**，没放宽**起**） | 链位那批（`3bdd5ee47` 一族） |
| `logical_branch.*`（2 条） | `logical_branch.short_circuit.passes`、`.simplify` | `exit 1`，`internal error`，7 条违规 | 未定位 | 未定位 |
| `docs.*`（2 条） | `docs.links.citations`、`docs.tutorial_images.current` | 引用/图片清单与盘上不符 | `bf18d9a2b`（"docs: finish the cut's revert"）删了 20 个页、还原 6 个文件 | 不属于任何车道：**是本批搬运不忠实** |
| 其余（19 条，逐条一条） | `05_effect_order`、`algebra_rules.library_only.by_cpp`、`canonical_document_and_equivalence`、`core`（`regions`）、`entering.positions.plan_regions.nvm`、`gate.case_ledger`、`gate.plan_inventory`、`generic_values.graph.nvm`、`if_without_else.graph`、`pass_written.inventory.nvm`、`poison:modules.rename_identity:rename.env_graph_identity`、`poison:modules.resources:core`、`print_and_recompile`、`print_then_run_agrees_with_the_origin`、`rename.env_graph_identity`、`rename.process_graph_identity`、`runner.backend_matrix`、`runtime.graph_draft_ledger.never_rises`、`tools.autodiff_migration_ledger.ratchet` | 混合：清单/账本类（`gate.plan_inventory`、`gate.case_ledger`、`pass_written.inventory`、`*.never_rises`、`*.ratchet`、`runner.backend_matrix`）是**批次新增的计划条目与账本计数**；`generic_values.graph.nvm` 是 `edge count mismatch actual=25, expected=26`；`if_without_else.graph` / `print_*` 是图印出与重编不一致 | 未定位（逐条要单独读） | 未定位 |

**合计**：10 + 6 + 3 + 4 + 3 + 2 + 2 + 19 = 49，加 `matmul_t` 三条 = 52。

## 2. 一句话归纳

49 条里**主症状只有两个**，而且都在链上：

1. **`sync chain forks at node N`**（`var_capture` 10 条、`sync_literal_operands` 4 条、`logical_branch` 2 条、`json`/`var_read_before_write` 那两条是同类）——一批语句被接到同一个前驱上，链成了分叉。这正是一条链位交出时**没有看两端在不在同一个块**能造出来的形状（`docs/design/effects/edges.cn.md` 5.3）。
2. **边从纯值起 / 图与 golden 不符**（`opt.edges` 3 条、`opt.camel_passes` 6 条、`generic_values.graph`）——写边的那一侧还在往纯值上挂顺序边，而封存检查按 4.6 拒绝它。

**所以这 49 条不是 49 件事，是同一处图构造在 49 个程序上的两种表现。** 本轮的 `handChainPlaceTo` 修只堵了"跨块"这一种，而且（第 4 节）只在批次的图上成立。

## 3. 两处相交面的合并口径（本批不进树，但这节要留着）

将来谁再搬 `ab9b5bf1a`，照这节合，才不会把 `7f697508c` 之后的正文冲掉：

- **`agents/test.md`**：批次那笔只**加 12 行**；`7f697508c` 在同一文件里新增了 `## Guess the name and the count is wrong, not small`、`## A wall clock on this host is not a cost…`、`## Getting a stack trace` / `### the reading was stable, plausible, and not a reading at all`、`### Editing files`、`## 改名与抽谓词这类重构…` 六节，**并删掉**了 `## Notes and comments hold the criterion, never the measurement`。**合的时候正文全取 `7f697508c`，只把那 12 行插进去**；拿我这侧的整文件会把六节正文冲掉、把已删的一节搬回来。
- **`test/plans/feat/compile/rules_angle_args.plan.toml`**：批次那笔只**多一条** `rules_angle_args.sum_axis_jvp`（`backends = ["nvm","fvm"]`，钉 `jvp=[5.0, 7.0, 9.0] tangent=[1.0, 2.0, 0.0]`）。**只留这一条**，其余行取 `7f697508c`。
- **`modules/transforms/split.h` / `split.cpp`**：批次里没有 `SplitGraph::standing`（`git show ab9b5bf1a:modules/transforms/split.h` 只有 `nestedPrograms`），`7f697508c` 也没有；我这棵树里还留着它**只是因为 base 是 `d7f15f2b3`**。合的时候**保持已删**，批次那边不会要这个成员。

## 4. 为什么整块搬运被否

1. **一次合并带进 49 条没人认领的红**：今晚已经付过一次代价（`f53e7a66d` 上 59 红、43 条来自这批，天亮才发现）。修好一项不等于 188 笔都该今天进树。
2. **这批的图上还剩同一处图构造的两种表现没修**（第 2 节）：`fail` 从 52 走到 3 之前，整块落地就是把 49 条红转交给下一个人。
3. **`handChainPlaceTo` 的修在 `7f697508c` 上是空跑**：本轮写的最小程序（`sync` 块里调用一个正文只有分支、每个臂的最后一条语句是 `println` 的函数，调用后接一条打印）**在 `7f697508c` 上加不加这个修都 `exit 0`，四行输出一样**；把它拿到本页这棵树（批次已就位）上跑，**也一样 `exit 0` 四行齐**——所以它**连批次的复现都不是**；`benchmarks/mlp/model.cml` 那个形状（`train_loop` 的内联体落进 `else` 臂，`log_final` 又内联进同一块，两条链位在同一个块里相遇）才是。**"修 + 用例先红后绿"这件动作，在只有 develop 的树上是做不出来的**：那条边是在批次的图构造下才被写出来的，最小复现要按 mlp 的两层内联重写。

## 5. 修法的第一版为什么被换掉

"跨块就跳过（不写边）"读全量 feat 时多出两条红（`json.accessor_error.opt`、`var_read_before_write.opt_nvm`），都是 `sync chain forks at node N`：跳过等于让两个后继都还跟着被替换的那条语句，`closeChainOver` 再把两条都接到同一个前驱上，链就分叉了，而分叉是另一条不变量（5.3）。换成的第二版是"跨块的后继自己当本块的链头"（`setSyncInputs(user, {})`）。

**第二版在 `7f697508c` 上也不是中性的**：同一条最小用例，`std::macro std::opt std::nvm` 下

- 不修：`pick: positive` / `after 10` / `pick: not positive` / `after 20`（四行）
- 修后：`pick: positive` / `after 10` / `after 20`（**少了 `pick: not positive` 一行**；`std::macro std::nvm` 三行管线全跑时四行都在，只有带 `std::opt` 的那两条少一行）

少的那一行是**实测**，原因只是**候选、未逐一证明**：`handChainPlaceTo` 除了内联那条路，还有 `src/passes/opt/generic/generic.cpp#pruneBranch` 一处调用（`handChainPlaceTo(join, place)`——分支被剪掉后，臂里的代码要接进本块），"跨块就清边"可能把这条合法的交接一起堵了，于是那条效果语句没了后继。**要证明只需一处探针**：在 `pruneBranch` 的调用点上打印 `join`/`place` 的块与 `users`，看那一对是否正是 `same=0`。**下一版要分开"不该交"与"该交"**——候选判据是**尾节点所在区域是不是一个"还活着的分支的臂"**（区域种类 `Arm` 且 `owner` 节点还活着 ⇒ 不交；`pruneBranch` 剪掉分支后 owner 已死 ⇒ 照交），并且要用 `test/cases/opt/generic/*` 的保形用例把两条路各钉一次。