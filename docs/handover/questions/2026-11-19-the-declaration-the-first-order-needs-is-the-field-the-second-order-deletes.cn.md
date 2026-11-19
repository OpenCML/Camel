# 第一条裁定要的声明，就是第二条裁定要删的字段

**问句（一句）**：`d5a5c1839` 同日下的两条裁定——①「打包由 `graphExitTypeOf(声明)` 回答」、②「删掉 `declaredFuncType` 与它的钩子」——在代码现状下互斥，请问撤回哪一条？

**默认建议（本轮按此开工）**：**甲**——保留 `declaredFuncType`，把它的**调用时机**改成「每个图在**打包之前**录一份声明」，于是 ① 成立、② 撤回。

## 一、两条裁定的原文

`d5a5c1839`（2026-11-19 21:45）：

> 那个位（`packsVarParameters`，前身 `exitTypeIsPacked`）不进 develop——06:40、07:00 两条已定：打包是全称的，"这张图打包了没有"永远等于"声明里有没有 `var` 形参"，由 `graphExitTypeOf(声明)` 回答。`lane-ssa` 在分支上把位删掉、四处调用改问声明，再整体合入……

> 同时：develop 上的 `declaredFuncType`（零读者，06:15 与今天每日审查第 11 条两次要求删除）还在，而且多了一个钩子——删掉它与它的钩子，一笔。

## 二、① 需要一个「每个图都有、且在打包之前录好」的声明，而现在没有

```
`declaredFuncType_` 全树只有一个生产写者：`DraftGraphBuilder::setExitType`（`src/compile/gir/draft_graph_builder.cpp:92`）
   而它只在 `!graph->funcType()->hasExitType()` 时被调（`builder_helpers.h:174–187`）
⇒ **凡签名自己写了结果的函数**（`func bump_then(z: var int, k: int): int`，绝大多数）**`declaredFuncType_` 永远是 nullptr**
   ⇒ 没有声明可问；回落到 `funcType()` 又**已经被打包过**（exit 是元组），`graphExitTypeOf` 对它逐字幂等 ⇒ 答案恒为「不交回」
```

次生问题：签名不写结果的那一支，`setExitType` 是在 `handBackVariableParameters`（`builder_control.cpp:4682`）**之后**才跑的，所以录下来的「声明」是**打包后**的类型再换掉 exit —— 它对每个带 `var` 形参的签名都答「打包了」，包括体根本没打包的图。

**⇒ 所以「问声明」要成立，缺的正是「每个图在打包之前录一份声明」。**

## 三、那份声明唯一能放的地方，就是 ② 要删的字段

第四处调用 `src/compile/gir/builder_helpers.h:141`（`callHandsBackResult(callee)`）改成问 `callee` 自己的 `funcType()` 也不行：`funcType()` 已被打包，`graphExitTypeOf` 幂等。改成问 `declaredFuncType_` 则如上，多半是 `nullptr`。

⇒ **⇒ 现状下没有第二个载体。** 所以要 ①，就不能 ②。

## 四、把「问声明」真做下去会怎样（实测）

```
`packsVarParameters` 命中：改前 **9 行 / 4 文件**（`draft_graph_builder.h:85,87,88`、`draft.h:479,480,1254`、
   `builder_control.cpp:4630,4633`、`builder_helpers.h:141`）；`exitTypeIsPacked` **0 命中**
前三处（两处访问器 + `builder_control.cpp:4633` 的写）是**纯删**，编译通过、无需替代
第四处改成问声明后：**`feat/linear` 869 → pass 680 / fail 184**
   第一条红 `var_params.nvm`：`unexpected output: 1 3 13 (6, 13)`
   ⇒ `r = bump_then(c,5)` 拿到的还是整个元组 —— **该问出 true 的地方问出了 false**
```

同一个缺口的另一半：`test/cases/…/var_through_with_grad.cml:26` 报 `Invalid call to 'with_grad' … (x: float[…], var cnt: int) => (float, int) uses var cnt`，而 `builder_control.cpp:4559` 的注释**逐字预言了**这条失败。

## 五、另有一条更要紧的事实：`lane-ssa` 自己就是红的

```
`5559d3c6e`（合 develop 之前，lane-ssa 原样）`feat/linear`：**869 / 771 / 93 / skip 2 / known 3**
`4d308fc72`（合完 277 笔，位仍在）              **869 / 771 / 93 / skip 2 / known 3**  ← 逐字相同
⇒ **合并本身一个数都没变坏**（`feat/modules` 反而 13→7、`feat/trans` 6→2）
⇒ **⇒ 104 条红（linear −93 / opt −4 / tools −1 / modules −5 / trans −1）全是车道自己的**，
   形状集中 `var_*`，且是**编译期**错：`Return type mismatch between branches (in function 'count_up: (var acc: int, n: int) => int'): expected 'int', found 'int'`
```

而 `origin/develop` 已经用 `782007912`（作者 `a1b57552a`）把打包 revert 掉，理由是 `opt/loop_wait_after_inline.non_contiguous_loop.refused` 与 `tools/lsp.navigation` 两条红，并写明「develop 不带任何新红」⇒ **develop 的树因此等于 `2d7c5c3a1`，而那个提交正是 `lane-ssa` 的第 3 笔**。

⇒ **⇒ 所以 `lane-ssa` 要合进来的，是 develop 刚刚撤掉的那一批。合入条件因此不是「不差于基线」，是「先把这 104 条红做完」。**

## 六、所以本轮的处理

```
① `lane-ssa` **挂着：不删、不合**（`4d308fc72` 留在分支上）
② `declaredFuncType` **暂停删除**，等本条裁定
③ `packsVarParameters` 的下线**暂停在第四处**，补丁留档
```

## 七、请裁决（二选一，写死默认）

**甲（默认建议）**：保留 `declaredFuncType`，把 `setDeclaredFuncType` 的**调用时机**改成「每个图、在打包之前，都录一份声明」（即不再只在 `!hasExitType()` 时才录），于是「问声明」成立、打包位可以真删；② 撤回，`noteOriginalTypes` 的钩子保留。
**乙**：② 照做删掉字段与钩子；① 改成「打包由**签名**回答」——但需要另一个能承载「每图打包前声明」的载体，目前没有 ⇒ **等于 ① 做不成**，而 `lane-ssa` 的 104 条红也失去解法。

**⇒ 另有独立一问（`lane-ssa` 本身）**：它自带 104 条红且重建的是 develop 已 revert 的那批，是否按新设计重做、还是废弃这条分支？默认建议：**先挂着，等本条①的裁定落地后再决定重做还是废弃。**