# 第 5 条第三条：走不到的警告与没人调用的链位函数（只读清点）

**问：作者 T+72 §5 第 5 条的完成条件「删掉走不到的警告和没人调用的链位函数」——今天到底有哪些属于这两类？**

出处：`docs/handover/batches-2026-10-31-72h.cn.md` §5 第 5 条，作者原文「删掉走不到的警告和没人调用的链位函数」。

**这一轮只报不删。** 今晚已经付过两次代价：删了一个正在生效的守卫（`lamwith` 按位置取值 → 10 条绿变红），以及留了一个不生效的守卫（`inUnusedSyncBlock`）。所以先有清单、再有闸门、再有见证。

## 表一：走不到的警告

| 诊断 | 位置 | 判定 | 我怎么试的 |
| --- | --- | --- | --- |
| `SemanticDiag::EffectNotExecuted`（「nothing depends on it and it is not in a sync block」） | `src/compile/gir/builder_diagnostics.cpp#Builder::reportDroppedEffects` | **走得到** | `/tmp/w1.cml`：非 `sync` 体里 `println("s")` 与 `side()` 两处值的读无人要——两条警告都响，`std::nvm` 退出 0 |
| `SemanticDiag::IgnoredSideEffect`（「is not in a sync block, and it takes a var argument」） | `src/compile/gir/builder_diagnostics.cpp#Builder::reportIgnoredCapturedWrites` | **走得到** | `/tmp/w2.cml`：`func nowrite(): int { var v = 1; v = 2; return 0 }`——响一条，报文点着 `v = 2` 那一行 |
| `SemanticDiag::EffectNotExecuted` 的另一支措辞「the sync block it is in is not used」（`inUnusedSyncBlock`） | 已不存在 | **已经删掉了，无需再删** | `git grep -n inUnusedSyncBlock -- src modules` 今天 **0 命中**；`semantic.cpp` 里只剩保留下来的那半句。另一条车道在 `a49cac54d` 回退后写过 `docs/handover/questions/2026-11-03-is-the-unused-sync-block-warning-reachable-at-all.cn.md`，它列的删除清单已经落地 |
| `SemanticDiag::FailNotExecuted` | `src/compile/gir/builder_diagnostics.cpp#Builder::reportDroppedEffects`（同一函数内，`fail` 那一支） | **未判定** | 三次都没写对语法：`fail("boom")` → `NoMatchingFunction`（`fail` 的签名要 `<Kind: string> (message: string)`）；`fail(DivisionByZero, "boom")` → `UnresolvedReference`；`let e = fail()` → 同样 `NoMatchingFunction`。`test/cases` 里现成的写法是 `error('Probe', 'always')`，我没把它接到 `fail` 上。**这一条我不写“走不到”**，只写“我没造出触发它的程序” |

结论：**链位/语句/前驱这一族的警告里，今天没有一条是可证明走不到的**——唯一那条（未使用的 `sync` 块）已经在代码里删掉了。剩下要么响（表里前两行），要么我没造出（最后一行）。

## 表二：没人调用的函数（`file#Symbol`）

判据一致：`git grep -n "\b名字\b" -- include src modules` 只出现**声明 + 定义**两处（或只在头文件里出现一次的内联体），`src/passes/test/*` 的调用**不算**生产调用。

| 函数 | 位置 | 我怎么判定它零调用 |
| --- | --- | --- |
| `GraphDraft::replaceAllSyncUses` | `include/camel/runtime/draft.h:739` / `src/runtime/draft.cpp:2028` | 全树只有声明与定义；它的函数体里那句 `requireOneWriting("replaceAllSyncUses")` 是**自报名字**，不是调用 |
| `GraphDraft::replaceSyncStructuralRefs` | `src/runtime/draft.cpp:1989` | **只被上一行那个函数调用**（`draft.cpp:2033`），所以随它一起悬空——删上游就自动没了 |
| `GraphDraft::retireRegion` | `draft.h:831` / `src/runtime/draft_regions.cpp:161` | 只有声明与定义 |
| `GraphDraft::flattenRegion` | `draft.h:857` / `src/runtime/draft_regions.cpp:200` | 只有声明与定义 |
| `GraphDraft::hasWaiters` | `draft.h:535` / `src/runtime/draft.cpp:576` | 只有声明与定义 |
| `GraphDraft::dropUncalledGraphs` | `draft.h:489` / `src/runtime/draft.cpp:2043` | 只有声明与定义 |
| `GraphDraft::setCaptureNames`、`GraphDraft::setOuterGraphId` | `draft.h:560`、`draft.h:345`（内联） | 头文件里各出现一次（内联定义），全树无调用点。这两个是**设置器**，可能是留给外部调用的接口，删前要问一句“API 面还是死代码” |

## 回滚残留那一条（特别标注）

作者/前任提到的 `regionOwnerOf`、`placeNowait`、`lastChainStatement_`、`chainTailOf`、`syncPredecessorOf`——今天 **`git grep` 全部 0 命中**：P0-2 第二步回退时它们已经被清掉了，**没有残留可删**。

表二里那六个函数**不是今晚回滚的残留**：`retireRegion`/`hasWaiters`/`dropUncalledGraphs` 出自 `d3287b6d8`（2026-10-30 的直连账本），`replaceAllSyncUses` 最后一次被改是 `82d554476`（边种类合并）。**所以删它们是一条独立的决定，不能借“回滚残留、删了安全”这句话推进**——那句只对今晚被撤的那批函数成立，而它们已经不在树上了。

## 下一步（不建议现在做）

删除要有闸门：先删一个（`replaceAllSyncUses` + 随之悬空的 `replaceSyncStructuralRefs`，两处声明+定义，零语义），跑 `feat/compile`、`feat/opt`、`feat/linear` 三族，读数与今天逐名相同再谈第二个。一次删六个，红了分不清是哪个的。