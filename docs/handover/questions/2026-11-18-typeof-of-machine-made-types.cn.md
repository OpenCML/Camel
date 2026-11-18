# 问：`typeof` 对「机器造的型」（tuple / `T?`）要不要放开，只限生成器那条路？

`2026-11-18`｜基线 `origin/develop = c47b7e99a`｜作者已批「改掉过期的 stream 断言 + 排下一批」（`9da293e42`）

## 一、为什么会有这一问

`lane-girstep2` 在实现「第一步」时零行代码停手，量出 10.1 的措辞还不够：

```
`createCompileGraph`（`builder_helpers.h:417-444`）按 funcType **一次性**建好端口，
  同时 `addPortNode` → `addRuntimeSlot(type)` **把 runtime slot 的型也定了**
`DraftGraphBuilder::setFuncType`（`draft_graph_builder.cpp:82-84`）只改签名，**不碰端口与 slot**
`visitFuncNode` 读签名是 `typeLoad->dataType()` **原样**（`builder.cpp:1080`），不走 `resolveTypeNode`
**GCT 侧没有类型推断**：`typeOfPlaceholder` 只留 `typeof#n`（`builder.h:411`「Nothing of the expression reaches the type」）
⇒ **槽型在 GCT 里永远拿不到，只在 GIR 拿得到。**
```

而状态元组、pack 的 cast、tuple data 节点的型**都被烤在构造时**（`TupleDataFactory` / `makeData`）。事后 retype 会与端口、slot、已按旧签名检查过的 call 点**三方不一致**。

## 二、两条可行的路（只有这两条）

```
**(a) 把生成器体的建搬到 GIR**
    几百行下移；GIR 里类型已知，一出生就是具体型。**代价大、但不需要任何放宽。**

**(b) GIR 之前，对 step 子树做一次「占位符改写」**
    重建 TYPE 节点与那些被烤死的 tuple data 节点，让端口 / accs / pack / call
    **一出生就是具体型**。明显更小。
    **⇒ 但它要求 `resolveTypeOfs`（`builder_generics.cpp:2767-2854`）有 tuple / `T?` 分支，**
      而 9.4① 明写「别放宽」，因为 `typeof_inside_tuple_is_refused` 是一条有测试的拒绝。
```

## 三、这一问具体是什么

**不是「要不要放宽 `typeof`」，是：**

> **能不能把 `typeof` 对 tuple / `T?` 的解析**限定在「机器造的型」上**——
> 即只有生成器 / 状态槽那条路造出来的 tuple 与 `T?` 才允许被解析，
> 而写出来的 `typeof_inside_tuple_is_refused` 判据保持绿。**

**⇒ 判据必须能同时给出两个读数**：生成器那条路通了，而那条拒绝**仍然红着**（即仍然按原样拒绝写出来的那种写法）。

## 四、默认建议（按这一条开工，作者改口再退）

```
**走 (b)，并按上面「只限机器造的型」实现。**
⇒ 理由：它小得多；(a) 是几百行的整体下移，而这一轮的全部目的只是**让一个探针从红转绿**。
⇒ 而「机器造的型」有一个可操作的判据面：那些 tuple / `T?` 节点是**别处造出来、带标记**的，
   不是解析源码里的 `typeof` 得到的 ⇒ 两者的来源在节点上可分。
⇒ **若发现「机器造」在节点上不可分，就退回 (a)，并把这一发现写进这一页第 6 节。**
```

## 五、开工时不许忘的三条（已量过的读数）

```
① **探针 `cell_holds_ref.cml`（`--gc-check foreign`）必须从红转绿**：`InvalidArgument@:46` 消失
  ⇒ 今天逐字：`5/5/5/9` + `InvalidArgument of 'state.set': the value is not a reference`，`RUN_EXIT=1`
② **那条 stream 断言要改，但它不能单独落**：`stream.generator_cell_type_from_first_write` 的 jgir 实测
  `labels[n=3 s=1:var string] | __gen_step__[n=78 s=0]` ⇒ 名为 `s` 的只有外层格子
  ⇒ 改后要钉同一件事（有一个 `s` 且它是 string）在 `__gen_step__`；**这不是删判据**
③ 六计划今天：`compile 2627/2610/0/17`｜`linear 869/864/0/3/2`｜`opt 1594/1594/0/0`（e9 268/268）
  ｜`parse 121`｜`para 104`｜`tools 70`｜`[SUMMARY] 5385 / 5363 / 0 / 2 / 20`
```

## 六、`list_sha` 的第三次变化（单独记，不是减法）

```
`4d48be5e4` 那棵树：`72c1a623…`
`86606c969`（第 10 节量的）：`1a01d883…`，files 3891→3892
`c47b7e99a`（本轮量的）：**`f9209b1d…`**，`files=3900 listed=3900 counted=2737`
⇒ **三次语料都动过，而 `var_new=213 / var_new_empty=43` 一字未变**
⇒ 结论：**新进来的文件都不带 var 节点**；`43` 这个数仍可用，但**每次引用都要带上它的 `list_sha`**。
```