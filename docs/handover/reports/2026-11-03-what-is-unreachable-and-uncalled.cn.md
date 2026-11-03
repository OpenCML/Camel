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
| `SemanticDiag::FailNotExecuted` | `src/compile/gir/builder_diagnostics.cpp#Builder::reportDroppedEffects`（同一函数内，`fail` 那一支） | **未判定** | 三次都没写对语法，逐个记在这里，下一个人从这儿接着试：① `fail("boom")` → `error[SemanticDiag::NoMatchingFunction]`，给出的重载是 `<Kind: string> (message: string) => never`；② `fail(DivisionByZero, "boom")` → `error[SemanticDiag::UnresolvedReference]: Unresolved reference: 'DivisionByZero'`；③ `let e = fail()` → 又是 `NoMatchingFunction`（无参）。`test/cases` 里现成的错误值写法是 `error('Probe', 'always')`，我没把它接到 `fail` 上。**这一条我不写“走不到”**，只写“我没造出触发它的程序” |

结论：**链位/语句/前驱这一族的警告里，今天没有一条是可证明走不到的**——唯一那条（未使用的 `sync` 块）已经在代码里删掉了。剩下要么响（表里前两行），要么我没造出（最后一行）。

## 表二：没人调用的函数（`file#Symbol`）

判据一致：`git grep -n "\b名字\b" -- include src modules` 只出现**声明 + 定义**两处（或只在头文件里出现一次的内联体），`src/passes/test/*` 的调用**不算**生产调用。

| 函数 | 位置 | 我怎么判定它零调用 |
| --- | --- | --- |
| ~~`GraphDraft::replaceAllSyncUses`~~ | ~~`draft.h:739` / `draft.cpp:2028`~~ | **2026-11-03 已删**（见 §删除那一笔）。删前：全树只有声明与定义；函数体里那句 `requireOneWriting("replaceAllSyncUses")` 是**自报名字**，不是调用 |
| ~~`GraphDraft::replaceSyncStructuralRefs`~~ | ~~`draft.cpp:1989`~~ | **2026-11-03 一起删掉**：它只被 `replaceAllSyncUses` 调用（`draft.cpp:2033`），是随它悬空的那一个 |
| `GraphDraft::retireRegion` | `draft.h:831` / `src/runtime/draft_regions.cpp:161` | 只有声明与定义 |
| `GraphDraft::flattenRegion` | `draft.h:857` / `src/runtime/draft_regions.cpp:200` | 只有声明与定义 |
| `GraphDraft::hasWaiters` | `draft.h:535` / `src/runtime/draft.cpp:576` | 只有声明与定义 |
| `GraphDraft::dropUncalledGraphs` | `draft.h:489` / `src/runtime/draft.cpp:2043` | 只有声明与定义 |
| `GraphDraft::setCaptureNames`、`GraphDraft::setOuterGraphId` | `draft.h:560`、`draft.h:345`（内联） | 头文件里各出现一次（内联定义），全树无调用点。这两个是**设置器**，可能是留给外部调用的接口，删前要问一句“API 面还是死代码” |

## 回滚残留那一条（特别标注）

作者/前任提到的 `regionOwnerOf`、`placeNowait`、`lastChainStatement_`、`chainTailOf`、`syncPredecessorOf`——今天 **`git grep` 全部 0 命中**：P0-2 第二步回退时它们已经被清掉了，**没有残留可删**。

表二里剩下的六个函数**不是今晚回滚的残留**：`retireRegion`/`hasWaiters`/`dropUncalledGraphs` 出自 `d3287b6d8`（2026-10-30 的直连账本），`replaceAllSyncUses`（已删）最后一次被改是 `82d554476`（边种类合并）。**所以删它们是各自独立的决定，不能借“回滚残留、删了安全”这句话推进**——那句只对今晚被撤的那批函数成立，而它们已经不在树上了。

## 删除那一笔（2026-11-03）

删的是 `replaceAllSyncUses` 与 `replaceSyncStructuralRefs`：`draft.h` 两行声明、`draft.cpp` 十六行定义，共 −18 行，零语义。两项判据：

1. **零调用怎么证明**：`git grep -n "\b名字\b" -- include src modules` 只剩声明与定义两处；`src/passes/test/` 里的调用不算生产调用（这两个函数在 `src/passes/test/` 里也没有调用者）。`replaceSyncStructuralRefs` 的唯一调用点在 `replaceAllSyncUses` 之内，所以它是被删那一个的下游。
2. **为什么现在删安全**：删前删后各跑一遍六族，**红的名字逐条相同**：

| 族 | 删前 | 删后 |
| --- | --- | --- |
| `feat/compile` | 2217 = 2212 过 / **2 红** / 3 known | 2217 = 2212 过 / **2 红** / 3 known |
| `feat/opt` | 1174 = 1174 过 / 0 红 | 1174 = 1174 过 / 0 红 |
| `feat/linear` | 875 = 871 过 / 0 红 / 2 known / 2 skip | 875 = 871 过 / 0 红 / 2 known / 2 skip |
| `feat/parse` | 115 = 115 过 | 115 = 115 过 |
| `feat/transforms` | 29 = 21 过 / 0 红 / 8 known | 29 = 21 过 / 0 红 / 8 known |
| `transforms/split` | 19 = 19 过 | 19 = 19 过 |

那两条红是 `transpose_loop_escape.two_values_of_two_kinds.nvm` 与 `transpose_loop_escape.second_state_of_the_same_loop.nvm`，删前删后**同名**，与本次删除无关。

**剩下的六个一个没碰**（`retireRegion`、`flattenRegion`、`hasWaiters`、`dropUncalledGraphs`、`setCaptureNames`、`setOuterGraphId`）。它们各自要独立的一笔、独立的闸门；两个内联设置器还可能是留给外部调用的 API 面，要先问。

## 下一步（不建议现在做）

一次删一个，红了才知道是哪一个的。下一个候选按此顺序：`hasWaiters`（只是读，删错最多是没人等）、然后 `retireRegion`/`flattenRegion`（区域，风险高一档）、最后 `dropUncalledGraphs` 与两个设置器。