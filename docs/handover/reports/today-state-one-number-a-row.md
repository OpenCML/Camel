# 今天的状态：一个数字一行

> **一张数字表的价值不在数字，在于每行都能被重新量一次。**

日期：2026-10-31。四栏：数字、值、在哪个 sha 上量的、重量的命令。**没有第四栏的行不要写进来**——它明天就会变成过时的数字。

| 数字 | 值 | 在哪个 sha 上量的 | 重量的命令 |
|---|---|---|---|
| 整闸 | 7997 / 7988 / 0 / 7 / 2（total / pass / fail / skip / xfail） | `c85878ac9` | `node scripts/test.js all --jobs 8`（看 `[SUMMARY]` 行） |
| 账本（graph-store step 2b） | 45 / 389 / 2 / 4（files / references / constructions / decode）；`constructions` 两处按 baseline 原文：`region_extract.cpp#extractRegion`、`draft.cpp#decode` | `lane-gs2b@9dc8b2082` | `node test/cases/runtime/graph_draft_ledger/check.mjs`（读 `baseline.json`；`--record` 才改写） |
| `ASSERT` / `GUARD` | **1127 / 62** @ `src include`（canonical）｜**1183 / 63** 全树 | `audit` 最新 | `git grep -o 'ASSERT(' -- src include \| wc -l`（canonical）；全树把路径换成 `-- src include modules tools test`；`GUARD(` 同法 |
| `perf` 层 | 47 / 42 / 5（total / pass / fail；4 条是修复还在分支上，1 条 `startup.cost` 是机器条件） | `b5d86e610` | `node scripts/test.js test/plans/perf --jobs 8` |
| `perf` 层（新口径） | 47 / 46 / 1 | `audit` 最新（待它给 sha） | `node scripts/test.js test/plans/perf --jobs 8`（与上一行同一命令，两个 sha 各跑一次） |
| `perf` 的编译面 | 3670 条里编译失败 1（`transformer_step.cml`，现已修）；认 `[INFO] does not compile` 那一行，不认 `[FAIL]`——两栏模式的定义 | `b5d86e610` | `node scripts/test.js test/plans --compile-only --jobs 8`（`[SUMMARY] total: 3670`；`[INFO] does not compile` 那一行 = 1） |
| 算子表 | `operations()` 24（22 个字面名 + 2 条 `emplace`）；`major=1` 2 个 | 本树 `ad017fa16` | `git grep -n "result.emplace(table.intern" -- modules/tiles/src/graph_regions.cpp`；字面名数 `Select-String -Path modules/tiles/src/graph_regions.cpp -Pattern '^\s+"'` |
| 规则集 | `rule` 10 条（`jvp` 5 / `transpose` 5）；索引键含元数（`head@arity`）；消费者是 `reinterp<H>` | 本树 `ad017fa16` | `git grep -c "^export rule" -- stdlib/rulesets`；键在 `src/compile/gir/program.cpp:680` |
| 环境变量开关 | `CAMEL_RULESET_INDEX` 已清（本车道 HEAD 仍有 `program.cpp:662`，见下）；onnx/tensor/tiles 那批在队列里 | `origin/develop` | `git -C D:\Projects\Camel grep -n "CAMEL_RULESET_INDEX" origin/develop -- src`（0 命中） |

> **同类数字在不同 sha 上不同是正常的**（同一套命令，`src include` 上是 1127/62，全树是 1183/63——范围变了数就变；`faults` 上 9 处 `ASSERT` 转 `GUARD` 也会动这一行），所以第四栏必须是**稠命令 + 取数 sha**；换 sha 读数变了不是"表错了"。

## 复量差异（本次核过的，按上面命令）

- **`ASSERT` / `GUARD`**：口径由 `audit` 定：**`src include` 里 1127 / 62（canonical）**，**全树 1183 / 63**。本树今天按同一条命令量到 ASSERT 1139（`src include`）/ 1188（全树）、GUARD 53 / 53，与 audit 的 62 / 63 有差——差在 sha 与分类口径，以 audit 为准。"release 会说话 273"只有 `reports/assert-release-hole.md` 的值，本页不再单列。
- **账本的两处 `constructions`**：`lane-gs2b@9dc8b2082` 的 `baseline.json` 原文是 `modules/tiles/src/region_extract.cpp#extractRegion` 与 `src/runtime/draft.cpp#decode`。**`gs2b` 口头的"两处都在 `graph_txn.cpp`"是错的，已被 baseline 原文推翻**——这是今晚又一次"两处答案"，表按原文写。数字 45/389/2/4 与这份 baseline 逐字对上。
- **`CAMEL_RULESET_INDEX`**：这条要在**当前 develop 的 sha** 上跑：`git -C D:\Projects\Camel grep -n "CAMEL_RULESET_INDEX" origin/develop -- src` → 0 命中。**本车道 HEAD（`ad017fa16`）的 `src/compile/gir/program.cpp:662` 里还在**——本车道合的 develop 早于清掉它的那一笔，"已清"说的是 develop，不是本车道。
- **整闸 / perf 两行**：本机不构建，这些行本次**没有复量**，按给出的 sha 原样抄；复量命令已在第四栏。`perf` 的 `47 / 46 / 1`（`audit` 最新）与 `47 / 42 / 5`（`b5d86e610`）是两个 sha 的读数，不是谁对谁错。
- **`perf` 编译面的两个数不是同一次**：`reports/compile-census.md` 里 `total: 3670` 那次 `[SUMMARY]` 是 `fail 9`（5 条机器条件 + 4 条"判据不适用"）；"编译失败 1"出自 `perf` 两栏的 47 条读数（`[INFO] does not compile (transformer_step.cml)`）。**重取时只认 `[INFO] does not compile` 那一行，不认 `[FAIL]`**——这是两栏模式的定义：一个只回答一个问题的工具，第三栏（判据不适用）必须显式写出。
- **`major=1` 2 个**：本次没有在树里找到可复算的取数命令（`declared major=1` 是运行期读数，本机跑不了），先按给出的值记。

## 这张表不做什么

- 不替代计划文件：数字是读数，判据在 `test/plans/**` 与 `docs/design-principles.cn.md`。
- 不承诺明天：每行都会过时；过时了按第四栏重取，不要改这一页的历史值（要更新就换 sha 重写一行）。