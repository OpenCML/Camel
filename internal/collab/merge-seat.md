# 合并位（merge seat）

八格里**两格常驻**，不挪（D-1126-1）。这条页给任何一个合并位看：它自己该做什么、别人该给它什么。

## 两格的分工

| 座位 | 写什么 | 不写什么 |
|---|---|---|
| `seat/merge-1` | 把批并进 `integration`、跑该批判据、过闸、推 `origin/integration` | 不碰 `develop`（只有晋级那一步快进） |
| `seat/merge-2` | **准备**：把待合的批预合到当前 `origin/integration`，报干净/冲突/分级；解 a、b 类 | **不推 `integration`**、不动 `develop`、不开别人的分支做预解（先问） |

`integration` 只有集成步骤一个写者。两格同时推 = 两个写者，所以 2 号位只能准备。

## 批号（队列页给的号，这里对上靶子）

队列页 `internal/plans/2026-11-25-lane-queue.cn.md` 只写条目名、不带批号；批号活在本页，
因为只有合并位需要它。**快照会过期，判据是合并树，不是 `git cherry`。**

| 批 | 分支 | 2026-11-26 08:15 的状态 |
|---:|---|---|
| 7 | `lane/cc-int8` | 合并零改动（内容已在 `integration`） |
| 8 | `lane/milestone-fast` | 合并零改动 |
| 12 | `lane/docs-pass-forms` | 合并零改动 |
| 11 | `lane/export-matrix-path` | 冲突 b：`scripts/gen_export_matrix.py` 10 hunk，取车道侧 |
| 13 | `lane/walkmachinery-enter` | 冲突 b：1 hunk，取车道侧（取集成侧编译不过） |
| 6 | `lane/test-module-name` | 冲突 **c**：方向相反，停手等裁 |
| 14 | `lane/traces-zero` | 最后一笔，17 提交，需 `-Check ast-hashes --base` |

「合并零改动」是这一页最有用的一条读数：`git merge-tree` 出的树与 `integration` 自己的树逐字节相同 ⇒
**没有待合内容**，不是"合并成功"。`git cherry` 会在这三批上各报一笔 `+` —— patch 不等价 ≠ 内容缺失。

## 每一批必须做的四件

1. **说明写内容。** 合并提交的正文要列被合进来的东西，成批合并时把**每条车道自己的提交标题**汇进去
   （`<lane>` 在前、标题照抄、sha 缩写括注）。`Lane branch: lane/x at <sha>` 单独一行不算说明（AGENTS.md）。
2. **`.cml` 改动 ⇒ AST 账。** 批里改过 `.cml` 就在合并后 `-Check ast-hashes --base` 复测；红了窄口重录
   （`-UpdateAstHashes -AstOnly <精确路径逗号连接>`）**作为这一批自己的重录提交**；不许手写 hash、不许全量。
3. **红要分级。** 语料/记录/旧切点（同类）可自修；**方向相反**（c）停手报，不自己解。
4. **零改动也要报。** 报"没有待合内容"比报"合并成功"有用。

## 别做的事

- 不 `git stash`（共享给一百多个工作树）。
- 不杀 git 进程；`index.lock` 用 `scripts/check-index-lock.mjs` 查持有者。
- 不重写历史提交去补说明；缺内容的在报告页补账。
- 别人的分支上做预解，先问协调者。