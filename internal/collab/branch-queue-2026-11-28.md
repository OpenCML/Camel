# 未合 `lane/*` 的去向账（2026-11-28 08:05，对 `origin/integration = 97e6bf8a47`）

字段：枝 | ahead | behind | 最后提交 | 谁在做 | 去向。
闸门是 20 条；此刻 **17 条**。这张表每批合并后重算一次，**表里没有"待定"，每条都有一个人和一格**。

| 枝 | ahead | behind | 最后提交 | 谁在做 | 去向 |
|---|---|---|---|---|---|
| `lane/onnx-run` | 26 | 102 | 07:54 | `lane-ssa-residue` | 它的主枝；迁移批次每笔一交，攒够后换底进合并队列 |
| `lane/opt-inline` | 23 | 14 | 07:29 | 无人（已交回） | **合并队列第 2 位**，按净 diff 10 文件收 |
| `lane/editview-commit` | 16 | 25 | 07:52 | `lane-editview-a` | 等它交回后按 `D-1128-59` 两条判据验（17 条用例原样通过 + 反向不成立的新用例），再进队列 |
| `lane/task-229` | 14 | 8 | 07:43 | `lane-arrangeable` | **合并队列第 4 位**；要先换底 + soak 重跑（capacity×2 + harness×5） |
| `lane/pass-decl-3` | 14 | 36 | 07:03 | **无人** | 活的那条（`pass-decl` 已删）。需派一格：换底 → 跑它碰过的计划 → 进队列 |
| `lane/blocks-scope` | 9 | 277 | 02:52 | `lane-parked-rebase` | 换底中（落后 277，冲突面大，按上游侧解） |
| `lane/arg-parse-uaf-w` | 8 | 0 | 08:03 | `cap-probe` | 求导正确性格；`cherry` 8 笔全 `+`，换底后做 ①验证 ②签名 ③十律 |
| `lane/resource-wordtable` | 7 | 333 | 00:44 | `lane-parked-rebase` | **已批删**（`cherry` 七笔全等价）；先打 `archive/` 标签再 `-D` |
| `lane/ssa-residue` | 7 | 104 | 05:59 | 无（旧枝） | 它的活在 `lane/onnx-run` 上；这条是旧枝 ⇒ **判删候选**（先 `cherry` 核） |
| `lane/nvm-int-compare` | 5 | 4 | 08:04 | `lane-backend-speed` | **合并队列第 6 位**；枝尖多一笔文档 `40964bdb4a`（代码面同一） |
| `lane/onnx-g2` | 4 | 1 | 08:02 | `lane-parked-rebase` | 换底中；换底后跑它碰过的计划 |
| `lane/cc-loop-forward` | 4 | 0 | 08:05 | `lane-oper-shape-guard` | 换底 + 判 4 笔有效性 + 只交一笔读数（轻口径） |
| `lane/loop-tape-values` | 3 | 114 | 07:56 | `lane-block-hash` | 四段实现已存成枝上补丁，**等"值被存进数组或闭包"那堵墙读明白**再落 |
| `lane/main-effects` | 2 | 0 | 08:04 | `seat-merge-1` | **整枝一笔推**（`ee550f53dc`，冲突已解对：新 key + 新 reason） |
| `lane/std-run` | 1 | 1 | 08:03 | `lane-stdlib-reds` | `std::run` 一格；换底后按计划 1–5 |
| `lane/index-axis` | 1 | 30 | 07:10 | **无人** | 已做完、等合（behind 只有 30，换底便宜）⇒ 补进合并队列第 7 位 |
| `lane/conditional-samples` | 1 | 4 | 07:47 | 无人（已交回） | 合并队列第 5 位（`20673f1e1e`；与 `0cc6ab9fa4` 内容相同） |

## 已结清

- **删**：`lane/pass-decl`（`archive/pass-decl-20261128 = cc8af549a8`）、`lane/pass-as-value-crash`、`lane/sum-to-decl`（后两条已批、`archive/` 标签由 `lane-parked-rebase` 打）。
- **判"仍需人"**：`lane/cc-loop-forward`（4 笔全 `+`，不可按"落后就删"处理）。
- **判"推进中"**：`lane/index-axis`、`lane/editview-commit`。

## 规矩（写在这里，省得每次重讲）

1. 表里每条都要有"谁在做"和"去向"；出现"无人 + 待定"就当场补人或补队列位。
2. 判删只认 `git cherry` 全等价（`+0 / -N`），且**删前先打 `archive/<枝名>-<日期>` 本地标签**、贴标签 sha 与删除输出。
3. 超过 100 笔落后的枝由 `lane-parked-rebase` 点名处理（换底或判删）；**>"落后就删"不是判据**，`cc-loop-forward` 就是反例。
4. 一棵树同时只归一条车道；枝被别的树检出时，那条车道不得在同一枝上开工（`lane/std-run` 在 `backend-speed` 树、`lane/arg-parse-uaf-w` 在 `kinds-oper` 树）。