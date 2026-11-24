# 24 条车道分支的处置（依据 internal/reports/2026-11-24-lane-graph.txt）

读数里有三件事决定了处置方式：

- 一半车道的分出点在今天早上，落后 integration 一两百笔。合并位每合一条都要替它解两百笔的冲突，然后重测。
- 改 `internal/tools` 合并工具的车道有五条，它们彼此冲突，而且有几条已经被后来的 `merge-tooling` 取代。
- `seat/merge` 从 17:18 起停在 op-overload 上，落后 70 笔。

所以分五类：删掉的直接删；旧车道先在自己的工作树里追平，追平以后再排进批次；新车道直接成批合并；工具车道归成一条；还在跑的大车道分段交回。

## A. 现在就删（各留存档标签）

- 补丁为 0：`lane/red-b-fixes`、`lane/selection-docs`、`lane/macwheel-freed`。
- `seat/merge-2-dev`：旧的合并位分支，已被 `seat/merge-2` 上那次 fsh-dev 合并取代。
- `seat/merge-3` 推上 gir-opertarget 之后：`lane/sync-three`。gir-opertarget 和 view-graph-identity 都包含它，它自己的 6 笔会随之落地。删之前用补丁计数确认为 0。

## B. 合并工具归成一条

这五条都改 `internal/tools/merge-lane.ps1`、`scripts/merge-readings.mjs` 和钩子，彼此冲突：`lane/batch-merge`、`lane/plan-set`、`lane/merge-reuse`、`lane/red-bisect`，以及 `lane/merge-tooling` 后加的 4 笔。

- `batch-merge` 照现在的安排排第一。
- `merge-reuse`（D-1123-103，在分离头临时工作树里合并）已经被 integration 加合并位的设计取代，删掉。
- `plan-set`（D-1123-102）和 `red-bisect`：由 merge-tooling 那条车道逐笔对照 integration 上的现行工具。已经有的就删；还没有、而且仍然需要的，移植成 merge-tooling 的一笔。报告文件照搬进 `internal/reports/`。
- 之后合并工具只在 merge-tooling 这一条车道上改。

## C. 旧车道先追平（落后 200 笔左右）

`lane/op-overload-merge`、`lane/3a-transforms-autodiff`、`lane/3a-rewrite`、`lane/parser-old-spelling` 四条，在各自的工作树里把 `origin/integration` 合进车道，解冲突时不选边，跑各自选中的计划，报 new/fixed，然后排进批次。四条并行做（审读合并，闸门破线时允许），各派一个子代理。

- `seat/merge` 停掉它那次落后 70 笔的 op-overload 合并。车道追平以后，op-overload 进下一批。
- `parser-old-spelling` 的主体已经在 11-23 合进去了。剩下这 6 笔先列出提交标题：如果只是 `Range` 缺 `map` 的测量和钉期望（C-1123-56），已经被 D-1124-60 那件取代，就删掉；否则追平。
- `3a-rewrite` 在 backlog 里没有对应的行，补一行。

## D. 新车道直接成批合并

- **第一批**：`refgen-body`、`comprehension-shadowed`、`test-suite-refactor`、`precise-select`，改动的文件互不相交。`test-suite-refactor` 改了 `internal/backlog.md` 的那部分不带进来，backlog 只由协调者写。
- **第二批**：`coverage-table`（它和 `precise-select` 都改 `test/selection`，所以排在第一批之后）、`oversight-live`（只改插件目录）。
- **编译器这一串**交给 `seat/merge-3` 顺序合，它的树是热的：gir-opertarget（正在合）→ view-graph-identity → op-overload-merge（追平以后）→ 3a-transforms-autodiff → 3a-rewrite。

## E. 还在跑的大车道分段交回

`lane/misc`（25 笔、41 个文件）、`lane/test-refactor-2`（14 笔、93 个文件，其中 69 个在 test/plans）、`lane/front-facts`：现在先把已完成、能单独成立的部分交回，进下一批；以后每两小时左右交一次。test-refactor-2 改的计划文件越多，和别的车道冲突的面就越大，所以拖得越久越难合。常驻的杂项车道每轮合并都带上它已经完成的部分。

## 验收

每合进一批、删掉一批，报一行：哪些、哈希、补丁计数归零的。以后每晚出一份同样格式的 lane-graph 读数，覆盖写入同一个文件。目标是今晚把 lane/ 下的未合分支降到 12 条以下，最落后的不超过 100 笔。
