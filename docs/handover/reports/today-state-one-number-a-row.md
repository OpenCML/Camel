# 今天的状态：一个数字一行

> **一张数字表的价值不在数字，在于每行都能被重新量一次。**

日期：2026-10-31。四栏：数字、值、在哪个 sha 上量的、重量的命令。**没有第四栏的行不要写进来**——它明天就会变成过时的数字。

| 数字 | 值 | 在哪个 sha 上量的 | 重量的命令 |
|---|---|---|---|
| 整闸 | 7997 / 7988 / 0 / 7 / 2（total / pass / fail / skip / xfail） | `c85878ac9` | `node scripts/test.js all --jobs 8`（看 `[SUMMARY]` 行） |
| 账本（graph-store step 2b） | 45 / 389 / 2 / 4（files / references / constructions / decode） | `lane-gs2b@9dc8b2082` | `node test/cases/runtime/graph_draft_ledger/check.mjs`（读 `baseline.json`；`--record` 才改写） |
| `ASSERT` / `GUARD` / release 会说话 | 1163 / 61 / 273；原 1012 处空语句剩 951 | `lane-faults` 最新（本树看到 `e65a4766b`） | `git grep -o "ASSERT(" -- src include modules tools test ':!include/camel/utils/assert.h' \| Measure-Object`；`GUARD(` 同法；release 那一栏按 `reports/assert-release-hole.md` 的三种命运分类数 |
| `perf` 层 | 47 / 42 / 5（total / pass / fail；4 条是修复还在分支上，1 条 `startup.cost` 是机器条件） | `b5d86e610` | `node scripts/test.js test/plans/perf --jobs 8` |
| `perf` 的编译面 | 3670 条里编译失败 1（`transformer_step.cml`，现已修） | `b5d86e610` | `node scripts/test.js test/plans --compile-only --jobs 8`（`[SUMMARY] total: 3670`；`[INFO] does not compile` 那一行 = 1） |
| 算子表 | `operations()` 24（22 个字面名 + 2 条 `emplace`）；`major=1` 2 个 | 本树 `ad017fa16` | `git grep -n "result.emplace(table.intern" -- modules/tiles/src/graph_regions.cpp`；字面名数 `Select-String -Path modules/tiles/src/graph_regions.cpp -Pattern '^\s+"'` |
| 规则集 | `rule` 10 条（`jvp` 5 / `transpose` 5）；索引键含元数（`head@arity`）；消费者是 `reinterp<H>` | 本树 `ad017fa16` | `git grep -c "^export rule" -- stdlib/rulesets`；键在 `src/compile/gir/program.cpp:680` |
| 环境变量开关 | `CAMEL_RULESET_INDEX` 已清；onnx/tensor/tiles 那批在队列里 | `origin/develop` | `git grep -n "CAMEL_RULESET_INDEX" -- src`（0 命中） |

## 复量差异（本次核过的，按上面命令）

- **`ASSERT` / `GUARD`**：在 `lane-faults@e65a4766b` 上复量是 **1174 / 62**，不是 1163 / 61；差 11 / 1，同一套路径，差在 tip 或分类口径。release 会说话 273 与 951 本次没有复量。
- **账本的两处 `constructions`**：在 `lane-gs2b@9dc8b2082` 的 `baseline.json` 里是 `modules/tiles/src/region_extract.cpp#extractRegion` 与 `src/runtime/draft.cpp#decode`，**不都在 `graph_txn.cpp`**。数字 45/389/2/4 与这份 baseline 逐字对上。
- **`CAMEL_RULESET_INDEX`**：`origin/develop` 上 0 命中；**本车道 HEAD（`ad017fa16`）的 `src/compile/gir/program.cpp:662` 里还在**——本车道合的 develop 早于清掉它的那一笔，"已清"说的是 develop。
- **整闸 / perf 两行**：本机不构建，这两行本次**没有复量**，按给出的 sha 原样抄；复量命令已在第四栏。**`perf` 的编译面**同理，`3670` 与"1 条"本次未复量。
- **`perf` 编译面的两个数不是同一次**：`reports/compile-census.md` 里 `total: 3670` 那次 `[SUMMARY]` 是 `fail 9`（5 条机器条件 + 4 条"判据不适用"）；"编译失败 1"出自 `perf` 两栏的 47 条读数（`[INFO] does not compile (transformer_step.cml)`）。本页按给定口径写成一行，重取时认 `[INFO] does not compile` 那一行，不要用 `[SUMMARY]` 的 fail 数。
- **`major=1` 2 个**：本次没有在树里找到可复算的取数命令（`declared major=1` 是运行期读数，本机跑不了），先按给出的值记。

## 这张表不做什么

- 不替代计划文件：数字是读数，判据在 `test/plans/**` 与 `docs/design-principles.cn.md`。
- 不承诺明天：每行都会过时；过时了按第四栏重取，不要改这一页的历史值（要更新就换 sha 重写一行）。