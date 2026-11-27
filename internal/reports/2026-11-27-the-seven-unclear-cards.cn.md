# The seven `@unclear` cards, judged one by one

Read-only audit, 2026-11-27. The seven rows the board prune could not place (`internal/reports/2026-11-27-board-stale-candidates.md`, `@unclear`): a backlog row names the id, but no sha on that row is an ancestor of `origin/integration`. Nothing was deleted or changed; this page only says, per card, where its work stands and what the evidence is.

Three checks were run per card: `git cat-file -e <sha>` for any sha on the row; `git branch -a --contains <sha>` plus `git worktree list` for a branch or pool tree holding it; `git merge-base --is-ancestor <sha> origin/integration` for whether it landed. A fourth check decided most of them: **does the integration tip already contain the card's content**, read as `git diff <premise-sha> origin/integration -- <the files it touched>` (empty = the content is there under another commit) or as `git grep` for the code the card asks for. That fourth check is the one worth keeping: three of the seven are "the content is in integration under a sha the row never named".

## The seven

| card | judgement | evidence |
|---|---|---|
| `task-34` | **已由别的交付代掉** | 编译侧规则早就在：`git blame origin/integration -L1444,1452 -- src/compile/src/gir/builder_operators.cpp` → `d9ea5a905a9` (2026-10-24)，`:1444-1492` 就是 4.4 的写法——`ConditionUnmet` 的候选 `continue`、歧义只在 surviving 的 `holding` 上判；卡片要的"编译那半"无需再做 |
| `task-57` | **判不了** | 无枝无 sha：`lane-editview-a` 树里没有分支（`git worktree list`），`probe/editview` 计划在 integration（`test/plans/probe/editview.plan.toml`、`src/compile/include/camel/compile/gir/draft_graph_builder.h`），但"块头 d 绑到句柄与草稿"没有一笔提交可指；backlog 11-26 22:10 仍写"B 等 A 补块头绑定" |
| `task-97` | **真没做**（转成设计问句） | `internal/questions/2026-11-27-does-a-type-table-belong-to-a-context.cn.md`（在 integration）判"命令行内不能闭环"，要 `TypeTable` 生命周期决策；探针 `f9210eedad`/`8994fb2708` **不在任何枝上**（`git branch -a --contains` 空、`git cat-file` 在但无引用）⇒ 读数有、修没有 |
| `task-100` | **已由别的交付代掉** | `ebc7f8fe96` 不在任何枝上，但 `git diff ebc7f8fe96 origin/integration -- src/stdlib/time/module.cpp src/stdlib/process/module.cpp` **为空**；`src/stdlib/process/module.cpp` 现声明 `{"sleep", externalAmbient("clock", AccessMode::Write)}` 且注释写明"a wait is a **write** of the clock" |
| `task-124` | **已由别的交付代掉** | `6d633e1618` `Merge lane/defun-stream: a stream that is pulled is a call through the step family…` 在 integration 祖先里：`defunctionalize.cpp` +110/−26、新增 `test/cases/opt/defun/stream_pull.cml` |
| `task-192` | **真没做** | 现行判据是**操作符记录里的 `Effect::Pure`**（`src/compile/src/gir/builder_diagnostics.cpp:2300-2331`，`OperatorTable::effectOf`），不是卡片要的"读选中候选的 `with` 形参类型"；`src/compile/src/gir/builder.h:3958-3961` 的文档也只说"声明为 pure 的算子" |
| `task-258` | **已交回且已合**（记作已由别的交付代掉） | `eb8cb5514e` `The ledger records the nine direct connections the resolver lane brought`（`test/cases/runtime/graph_draft_ledger/baseline.json` +9/−1）在 integration 祖先里；该红在 promo49 读数里是 **gone 3** 之一（`internal/backlog.md:1404`）；尖端 `git grep never_rises -- src test` 已无命中 |

Counts: 已交回未合 0 · 已由别的交付代掉 4 · 真没做 2 · 判不了 1.

## If the two "真没做" are re-dispatched, first step each

- `task-97`：**先把那份问句答了**（类型表一个程序一份，还是进程一份）。不答就派，等于第五次"判据不在基上就派工"：问句在 `internal/questions/`，修法要在 `TypeTable` 生命周期上动，车道自己定不了。
- `task-192`：**先确认判据的唯一真相**是记录里的 `Effect::Pure` 还是候选的 `with` 形参类型。现行实现读记录（`builder_diagnostics.cpp:2300`），卡写的是后者；两者只能留一个，先问再动，否则把一个已成立的判据改散。

`task-57` 要动也一样：先在 integration 尖端跑 `probe/editview`，看"块头 d"的用例里哪几条还没有断言，再决定是补 A 还是判废。