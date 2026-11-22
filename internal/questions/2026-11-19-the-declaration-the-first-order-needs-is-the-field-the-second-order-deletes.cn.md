# ① 要的那份声明，正是 ② 要删的那个字段

**日期**：2026-11-19
**提出者**：打包位下线车道（工作树 `D:\Projects\Camel-wt\ssa2`，分支 `lane-ssa`；基线 `origin/develop` = `3217f0312`，合完 `4d308fc72`）
**状态**：**给作者的问题页**。第 5 节写死一条默认（甲），**在作者回答之前这条车道停手**：位没删，树停在 `4d308fc72`。
**触发**：按作者 `d5a5c1839` 在 `lane-ssa` 上把打包位删掉、四处调用改问声明，一次落完再量（第 2 节）。

## 1. 两条裁定，与它们互斥的那一点

**①（作者 `d5a5c1839`，2026-11-19 21:45）**：

> 打包是全称的，"这张图打包了没有"永远等于"声明里有没有 `var` 形参"，由 `graphExitTypeOf(声明)` 回答。

**②（作者裁定，由合并车道转述；落点 `6d2da50e1`「the declared function type goes, and its hook with it」，在车道 `lane-drop-declared-functype` 上）**：

> `declaredFuncType` 零读者，删掉它与它的钩子，一笔。

② 的前提在树里也写着（`include/camel/runtime/draft.h:468-469` 的注释原文）：

> **Nothing reads this yet**; the readers that need the declaration move over in the following change, and the bit goes after them.

**⇒ ① 说「问声明」，② 说「声明这个字段零读者，删掉」。两者只在「声明有没有读者」这一句上冲突，而 ① 一落地，② 的前提就不成立了。**

## 2. 现象（可验）

照 ① 把那四处改完（删两个访问器、删字段、删 `builder_control.cpp` 的写、把 `builder_helpers.h:141` 的读者改成问声明），`feat/linear` 同一台主机上：

```
4d308fc72（位仍在）      [SUMMARY] total: 869, pass: 771, fail:  93, skip: 2, known failures: 3
位删掉、改问声明          [SUMMARY] total: 869, pass: 680, fail: 184, skip: 2, known failures: 3
```

第一条红是 `var_params.nvm`：

```
[INFO] verification output: Error: unexpected output: 1 3 13 (6, 13) 15 8 113 20001
```

期望的第三、四行是 `13` 与 `6`；实际打出了 `(6, 13)`。`test/cases/linear/nvm/var_params.cml:52` 是 `let r = bump_then(c, 5)`，`bump_then` 是 `z: var int` —— **该答「交回形参」的地方答了「不交回」，投影没做，整个元组被当成了结果**。

这与 `internal/questions/2026-11-19-removing-the-packing-bit-and-computing-from-the-declaration-presuppose-each-other.cn.md` §7 那次（`pass 683 / fail 181`）是同一个缺口的**另一半**：那次是「派生对所有带 `var` 形参的签名都为真」⇒ true 得太早；这次是「声明根本不在」⇒ false 得太晚。

## 3. 声明拿不到：`declaredFuncType_` 的唯一生产写者，只在「签名没写结果」时跑

```
$ git grep -n "declaredFuncType_" -- src include
include/camel/runtime/draft.h:470    camel::core::type::FunctionType *declaredFuncType() const { return declaredFuncType_; }
include/camel/runtime/draft.h:473            declaredFuncType_ = funcType;      ← setDeclaredFuncType 里
include/camel/runtime/draft.h:1247   camel::core::type::FunctionType *declaredFuncType_ = nullptr;
src/runtime/draft_undo.cpp:370,488   （撤销日志的快照与恢复，不是生产者）
```

`setDeclaredFuncType` 在 `src include` 里只有一个**生产**调用点：`DraftGraphBuilder::setExitType`（`src/compile/gir/draft_graph_builder.cpp:92`）。而 `setExitType` 只在签名**没有**出口类型时被调：

```
src/compile/gir/builder_helpers.h:178      if (!graph->funcType()->hasExitType()) {
src/compile/gir/builder_helpers.h:179          graph->setExitType(Type::Void());
src/compile/gir/builder_helpers.h:185      if (!graph->funcType()->hasExitType()) {
src/compile/gir/builder_helpers.h:186          graph->setExitType(resultType);
```

⇒ **`func bump_then(z: var int, k: int): int` 这类自己写了结果的函数，`declaredFuncType_` 永远是 `nullptr`，没有声明可问。** 回落到 `funcType()` 也不行：打包已经在 `builder_control.cpp:4629`（`graph->setFuncType(funcType->withExitType(exitType))`）把它换成了元组，而 `graphExitTypeOf` 对已打包的输入逐字幂等（`builder_helpers.h:115` 的第一支，元组进、元组出）⇒ **答案恒为 false**。

**这就是 +91 条的来源**（184 − 93 条车道本来就红的，见第 7 节）。

## 4. 次生：录下来的那一份，是打包之后的

`Builder::setBodyResult`（`src/compile/gir/builder_control.cpp:4656`）先打包、后录声明：

```
:4682   const node_handle_t handed = handBackVariableParameters(graph, value);   ← 打包，:4629 换掉 exit
:4685   setGraphReturnFromResult(...)                                          ← 这里才 setExitType → 录声明
```

⇒ 在「签名没写结果」那一支，录下来的「声明」是**打包后**的类型再把 exit 换成体推断的结果。它对**每一个**带 `var` 形参的签名都答「打包了」，**包括体根本没有打包的图**——`:4558` 的 `!packsVariableParameters` 早退与 `:4597` 的 `parts.empty()` 早退都发生在打包之前。

**同一个字段，在两个方向上各错一半：拿不到时恒 false，拿得到时恒 true。** 所以问题不是「再算一次」，而是**这份声明今天没有一处是「打包之前的那个签名」**。

## 5. 默认（本页写死：甲）

**甲：保留 `declaredFuncType`，把它的调用时机改成「每个图在打包**之前**录一份声明」。** ⇒ ① 成立，② 撤回。

改动面是 `setDeclaredFuncType` 的**调用时机**（在打包前录，且对每个图都录，不只看 `hasExitType()`），不是新增字段、不是新增读者规则：图的 `funcType` 照旧是打包后的出口，声明照旧只回答「本来是什么」。

**乙：② 照做删掉它，① 改成「打包由签名回答」。** ⇒ 需要一个在**打包之后**仍能回答「这个签名本来就带 `var` 形参」的载体；`funcType()` 已被换掉、`graphExitTypeOf` 对已打包的输入幂等、`packsVariableParameters(funcType())` 因此恒 false，**目前没有这样的载体** ⇒ 等于 ① 做不成，代价就是第 2 节那 91 条。

**选甲的理由**：① 是「把位删掉」这件事的唯一依据（`af2cab221`「packing is total, so no bit」→ `d5a5c1839` 落地成「问声明」）；② 的前提是「零读者」，而**① 在落地的那一刻给了它第一个读者**。甲不是驳回 ②，而是把两份事实的先后写对：**先有声明，再删位**；乙则是把 ① 撤回。

## 6. 要作者回答的一句

**① 与 ② 现在只能留一条：让 `declaredFuncType` 活下来、在打包之前录（甲），还是把 ① 撤回、承认「打包由声明回答」还需要一个今天不存在的载体（乙）？**

## 7. 两条相邻读数（不是本页的问题，但决定这条车道的下一步）

- **`lane-ssa` 相对基线仍差 104 条**：`feat/linear` −93、`feat/opt` −4、`feat/tools` −1、`feat/modules` −5、`feat/trans` −1（`compile` 2622/0、`parse` 126/0、`para` 104/0 与基线逐字相同）。这 93 条**不是合 `develop` 带来的**：`5559d3c6e`（合之前）与 `4d308fc72`（合之后）的 `feat/linear` 都是 `869 / 771 / 93 / 2 / 3`，逐字相同；合 `develop` 反而把 `modules` 从 13 条红降到 7、`trans` 从 6 降到 2。
- **`origin/develop` 已经撤过打包**：`782007912`（作者 `a1b57552a`）revert 了 `9997910f4` 与 `c049f90ec`，理由是 `opt / loop_wait_after_inline.non_contiguous_loop.refused` 与 `tools / lsp.navigation` 两条新红，并写明「the standard they violate is that `develop` carries no new red at all」。develop 的树因此等于 `2d7c5c3a1`，而 `2d7c5c3a1` 正是 `lane-ssa` 的祖先（`git merge-base --is-ancestor 2d7c5c3a1 lane-ssa` 为真）。⇒ 这条车道要合的是 develop 刚撤掉的那批，合入条件不是「不差于基线」，而是**先把第 7 节第一条那 104 条红做完**。

## 八、补一条更好的路（量出它的人给的，使两条裁定**同时成立**）

前面写甲（保留字段、挪时机）与乙（删字段、① 做不成）是二选一，但还有第三条：

`
**对每个签名后来没被补过/换过的图，uncType() **本身就是那份声明**，graphExitTypeOf(funcType()) 直接能答，不需要另存**
**真要给「后来补过/换过」的图存一份，**冻结图 GCGraph**（include/camel/runtime/graph.h:1279）是更贴切的位置：
   在 encode 时把声明签名写进去一次 —— 它**按图持久**，builder 与 draft 都不必活着
⇒ **⇒ 所以两条裁定不必互斥**：把「每图打包前的声明」记在**声明还完整可读的时候**（创建 / encode），
   而不是记在一个**只在 open 签名被补完时才跑一次**的 setter 里；那个位恰恰对完整签名的图是空的
`

**⇒ 甲'（默认建议，取代前面的甲）**：把声明记到 GCGraph（创建 / encode 时一次），declaredFuncType 与它的钩子**照 ② 删掉**，① 成立。这样两条裁定同时成立，且字段「零读者」的判定也不再有例外。

**⇒ 请作者确认甲'；若不受，则退回甲。**
