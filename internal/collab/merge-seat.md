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

## 接手一条分支 = 冻结它

2026-11-26 08:20 的实例：1 号位把 `lane/traces-zero` 解到 `4be8d3152`，同一条分支又往前走了 5 笔
（`848c8f0d96`，含它自己的 207 行重录）⇒ 解了 28 分钟的解冲突作废，整批重挂到新尖端重做一遍。

**规矩：合并位说"我要合这条分支了"那一刻起，那条车道不再往上提交。** 要接着做就等这一批落地、
从新的 `integration` 开下一页；急着做就明说"这一批先合到旧尖端，后面的另起一批"。
车道交回时的 tip 就是合并位要合的那个 tip，两边不是同一棵树时不许动手。

## 别做的事

- 不 `git stash`（共享给一百多个工作树）。
- 不杀 git 进程；`index.lock` 用 `scripts/check-index-lock.mjs` 查持有者。
- 不重写历史提交去补说明；缺内容的在报告页补账。
- 别人的分支上做预解，先问协调者。
## 零待合内容的分支：存档后删，不要合（D-1126-3）

`merge-tree` 出的树与 `integration` 自己的树**逐字节相同** ⇒ 内容已进树，**合进去什么都不会变**。
这种分支**不要合**：合并位会正确地拒绝它（`the base and this tree are the same commit … there was no change to measure`，exit 2），
因为门禁不肯把“没测”写成“绿”。**处理办法是存档后删**：

```
① 打存档标签（本地，不推）：`git tag archive/<日期>-<lane> <tip>`
② 工作树必须干净、无 `.git/index.lock`、无活会话
③ `git worktree remove <路径>`（**不许 `--force`**）→ `git branch -D <分支>`
   —— 这里 `-d` 会拒绝（它不是祖先），所以用 `-D`；依据是第 ①/② 步的读数，不是“我看它像已合”
④ 报：五条各自的标签名、tip、删除前后的 `lane/*` 计数
```

**常驻规矩：**经 `b/<lane>` 重挂副本合进去的车道，**合完当轮就按上面这一步删原分支**，不要留到计数里去。
2026-11-26 的实例：5 条（cc-int8 / cc-onnx-loop / docs-pass-forms / milestone-fast / overload-guard）挂在“未合 21”里，
其中没有一条是待办；删掉后闸门 21 → 16。