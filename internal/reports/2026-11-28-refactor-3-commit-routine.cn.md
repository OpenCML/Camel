# `D-1128-180 ③`（遍即函数）：提交只有一个例程 —— 调用面清单

**先纠一个路径，别再去 `src/kernel/runtime/txn.cpp` 找**：那个文件不存在。本树（`lane/c4-min-verify@375e4f77e9`，
代码 = `integration@27aea1ba45`）只有两个相关文件：

- `src/kernel/runtime/graph_txn.cpp`（**29 行**）：`GraphTransaction::open` / `decode` / `borrow` 三个工厂
  （`:12` / `:20` / `:25`），**没有 commit**；`GraphDraft`（`draft.h:441`）也没有 commit 方法。
- `src/stdlib/graph/txn.cpp`（1333 行）：Camel 侧的 `graph::commit` 算子（`commitKernel` `:1158`，
  注册 `:1274`，调 `session.commit(true)` `:1175`）。

**提交只有一个例程**：`RuntimeGraphDraftSession::runCommit`（`src/kernel/runtime/draft_session.cpp:1640-2203`）。
下面五个入口全部落到它（或落到它的"只编码"半段）。

## 一、五个入口

| # | 入口 | file:line | 它是什么 | 谁调 |
|---|---|---|---|---|
| ① | `RuntimeGraphDraftSession::commit(bool detached = false)` | `draft_session.cpp:1636` → `runCommit(false, detached)` | **唯一的真提交**：整理 + 编码 + 校验 + 发布 | 非测试 **15** 处，含 `src/stdlib/test/` 共 **88** 处 |
| ② | `commitChecked()` | `:1617` | 冲突预检后转 ①（`throw GraphCommitConflict`） | **1** 处：`rewrite/module.cpp:4261`（+ test `edges.cpp:514`） |
| ③ | `prepared()` | `:1638` → `runCommit(true, false)` | **只编码**：不算账、不校验、不交付（staged 早退 `:1957-1970`） | **1** 处：`tiles/src/region_outline.cpp:318`（+ test `commit_checks.cpp:1669`） |
| ④ | Camel 侧 `graph::commit` | `stdlib/graph/txn.cpp:1158`（`commitKernel`）、注册 `:1274`、调 `:1175 state->session.commit(true)` | 用户程序与 Camel 写的遍提交草稿（detached，永不装 context） | Camel 程序；`std::gct` 的编辑块经它 |
| ⑤ | `GraphDraftGroup::encode()` | `draft_session.cpp:2253` | **编译侧"建完直接编码"**：不查六项、不发布，只 `validateRuntimeGraphPayload` `:2282` + `validateRuntimeGraphCalls` `:2285`，自带一次 `normalizeChainPlaces()` `:2260-2262` | **10** 处，全是 `transforms` / `autodiff`：`pullback.cpp:5412`、`first_with_as_param_operator.cpp:295`、`jvp.cpp:524`、`transforms/module.cpp:247`、`remat_operator.cpp:168`、`split.cpp:1654/2709`、`split_loop_cut.cpp:835`、`transpose/entry.cpp:56`、`vmap.cpp:831` |

## 二、`runCommit` 的 12 步（按代码顺序）

| 步 | 做什么 | file:line | 层 |
|---|---|---|---|
| 1 | 端口预检 `validateDraftPorts()`（端口指向不存在的节点就不编码；实现 `:1586-1615`） | `:1653` | **session 级** |
| 2 | 合并/换基：`merging` 且无冲突时把草稿 re-key 到最新 program | `:1676-1719` | **session 级** |
| 3 | 闭包 `collectCommitClosure()` | `:1721` | session 级 |
| 4 | **整理/结算**（per graph）：`ensureDraft` `:1730`；`written` `:1742`；`normalizeChainPlaces()` `:1743`；**`dissolveUnorderedGates()` `:1744`**；**`rehomeOrderEdges()` `:1749`**；全无变化 ⇒ `edited_.erase` `:1755` + 打 `kGraphFlagGatesDissolved` `:1757` + 丢草稿 `:1762-1764`；`deliveredMutations` 记账 `:1770` | `:1723-1771` | **一张图** |
| 5 | **写回/编码**（per graph）：`reserveProgram` `:1780`；`allocateGraph` `:1799`；编码前 `setFlags(… | kGraphFlagGatesDissolved)` `:1808`；`encodeDraftGraph` `:1809` | `:1780-1831` | **一张图** |
| 6 | staged 早退（只 `recordMade` 后交回） | `:1957-1970` | session 级 |
| 7 | **三项图级校验**：`validateRuntimeGraphPayload` `:1974`、`validateRuntimeGraphCalls` `:1979`、`validateRuntimeGraphPortTypes` `:1990`；`setRoot` `:1993` | `:1972-1993` | **一张图** |
| 8 | 交付自检 `verifyDraftDelivery`（Debug 或 opt-in） | `:2003-2025` | session 级 |
| 9 | **六项提交检查**：`editTier()` `:2099`（Debug 全问 `:2097`）、`lowLevelEdits()` `:2102`、`editNotes()` `:2104`、`commitViolation(...)` `:2111` → `GraphCommitRefused` `:2122` | `:2047-2124` | **一张图** |
| 10 | 版本记账 `inheritRevision`（`changed = edited_ && !sameContent`）、`recordMade` | `:1939-1942`、`:1949-1955`、`:2179` | session 级 |
| 11 | **发布 CAS** `installRuntimeProgramIfCurrent` `:2151`；输了 rebase `:2159-2169`；不发布则 `trackDetachedRuntimeProgram` `:2175`；`retargetFunctions` `:2182-2186` | `:2146-2186` | **context 级** |
| 12 | **记账位重置**：`program_`/`runtimeRoot_`/`work_`/`closureCache_`、`noteDraftsCleared` `:2196`、`drafts_.clear()` `:2198`、`draftedIds_`/`draftRefs_`/`edited_` 清空 | `:2190-2202` | **session 级** |

**"针对一张图"的四步 = 4、5、7、9**；**session/context 级的四步 = 1（`:1653`）、2（`:1676-1719`）、11（`:2146-2176`）、12（`:2190-2202`）**；
其余（3 闭包、6 staged 早退、8 交付自检、10 版本记账）是 session 级但薄，抽例程时不必动。

## 三、最小可行的一刀，与停手点

**一刀**：把"针对一张图"的 4~9 步抽成 `commitOneGraph(DraftEntry&, Program&, EditTier, …)`，
**先让 `GraphDraftGroup::encode()` 改调它** —— 那 10 处 `group.encode()`（`transforms`/`autodiff`）今天各自
复制了 `normalizeChainPlaces` + 两项校验（`:2260-2262`、`:2282`、`:2285`），这是"提交有两个例程"里最容易并的一半：
它不读 session 状态，签名干净，行为可逐条对（编码前后同一批校验）。

**不动**：`validateDraftPorts` `:1653`（session 级预扫描，作用域不同）、合并换基 `:1676-1719`（吃 `mayPublish_` /
`conflicts()`）、发布 CAS `:2146-2176`（context 级）、记账位重置 `:2190-2202`（session 生命周期）。

**停手点（归①，先不动）**：这一刀**会连带第 4 步的懒整理与三行 `kGraphFlagGatesDissolved`**
（`draft_session.cpp:1727` 的跳过判定、`:1757` 的置位、`:1808` 的编码前置位；枚举在 `graph.h:247`，
全树 4 处命中）。"删掉建完顺手整理"是 ① 的事，抽例程时必然要移动的那几行正好就是它，所以这一刀停在清单。

另记一条读数供 ③ 的人查：**`review/editview-commit@a5c29944d5` 现存三笔
（`50eaac705d`、`08520f9523`、`a5c29944d5`）都不碰 `kGraphFlagGatesDissolved`**
（`work/c1-c2` 那棵工作树也干净、flag 仍在）—— 所以"那条枝正在删这个 flag"在本树上**无法证实**。