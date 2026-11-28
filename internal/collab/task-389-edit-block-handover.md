# 交接：`lane/editview-commit @ ce14a0e31a` —— 类型有了、提交语义还没有

`task-389` 接手第一眼要看的就是这一页。枝 `lane/editview-commit` 已经换底到 `origin/integration = 2df5a3d156`，**8 笔保留、2 笔 skip**，`ahead 8 / behind 0`、`status` 空。**判不可合**：`editview` 计划上新增 6 条红，全是"块值 = 提交产物、故障在边界内"那一族。

## 一、为什么不可合

集成侧已经把 `edit` 块的值改成 **`attempt(closure)()` 模型**（`src/compile/src/gct/builder_expr.cpp:267/295-300`：闭包的结果就是提交的产物 `F!`，故障边界由 `attempt` 的 transform 提供，不再手造提交调用）。车道的 GCT 提交模型那笔被 skip 之后，余下 8 笔**只带来了 `draft type`、句柄、文档与诊断这一侧**，没有任何一笔补上 GCT 侧的提交语义。

⇒ **"类型有了、提交语义还没有"**。这六条红不是换底没换好，而是本枝当前的下限；补它是 `task-389` 的活：在 `attempt(closure)()` 模型上重做提交语义，不要去改那六条断言。

## 二、每处冲突的一行判词

```
a9f38f7fb2  src/compile/src/gct/builder_expr.cpp
  ⇒ skip，理由：集成侧已把 edit 块的值改成 attempt(closure)() 模型（builder_expr.cpp:267/295-300），
    车道侧手造 `graph:commit` 调用 + setBlockHeadModule(handFunc) 被整体取代

67133faaf3  test/cases/probe/editview/{body_fault_keeps_function,body_return}.cml
  ⇒ 取集成侧，理由：集成侧注释原文 "The body was rewritten for the block's new reading:
    `d` is the draft's root, so the body no longer calls it as a function"；
    车道侧 `add_constant(d, …)` 是旧读法产物

515a74cca0  src/stdlib/opt.cml
  ⇒ skip，理由：等价补丁已在集成侧 opt.cml:192 与 :246（`return commit(d)`，
    注释 "the draft is the one committed"）
```

## 三、保留的 8 笔

```
ce14a0e31a graph: the draft type is registered after the module's other types
33cefef0a3 gir: the operator group is asked of the module that declares the operator
acd5bd6210 docs: the module header says what a draft is, now that it has a type of its own
24d1f6de1f docs: the graph module writes the draft type, and the reference is regenerated
671a0a46e5 builder_call: an instantiated call reports the argument its type arguments do not fit
450cc5c551 graph: a handle type with a type argument is compared for sameness, not for standing in
6771f075a2 graph: a draft carries the type of the function its commit makes
655d272ea4 graph: a commit hands back the function type the call names, not the one the draft was opened with
```

被 skip 的两笔：`a9f38f7fb2`（GCT 提交模型）、`515a74cca0`（`opt.cml` 等价补丁）。

## 四、两组读数（`[provenance] tip ce14a0e31a`，`binary built` 05:58–05:59，`tier fast`，`jobs 2`）

```
枝   editview.plan            : total 20, pass 13, fail 7
尖端 editview.plan            : total 20, pass 19, fail 1
枝   editview_b.plan          : total 3, pass 1, fail 2
尖端 editview_b.plan          : total 3, pass 1, fail 2      （同名：escapes_accepted, draft_handle_leaves ⇒ 0 新增）
枝   editview_draft_type.plan : total 3, pass 3, fail 0
尖端 editview_draft_type.plan : 计划文件在尖端不存在（本枝新增），无法对跑
```

## 五、6 条甲（枝独有）

```
editview.edit_value_is_the_function
editview.edit_value_is_not_the_body.nvm
editview.edit_value_is_not_the_body_twin.nvm
editview.body_fault_keeps_function.nvm
editview.body_return.nvm
editview.head_evaluated_once.nvm
```

乙（尖端独有）= `editview.sec2_log`（尖端本来就红，与本枝无关）。

## 六、`numstat`（对 `origin/integration`）

```
8	5	docs/stdlib-reference.md
3	3	docs/technical/45_graph_module.md
16	0	src/compile/src/gir/builder_call.cpp
38	5	src/compile/src/gir/builder_variables.cpp
13	5	src/stdlib/graph.cml
125	8	src/stdlib/graph/handle.cpp
18	5	src/stdlib/graph/handle.h
12	0	src/stdlib/graph/module.cpp
14	2	src/stdlib/graph/txn.cpp
21	0	test/cases/probe/editview_b/draft_commits_as_its_own_type.cml
24	0	test/cases/probe/editview_b/draft_of_one_type_is_not_another.cml
31	0	test/cases/probe/editview_b/graph_is_not_a_draft.cml
36	0	test/plans/probe/editview_draft_type.plan.toml
```

两条 `editview/body_*` 用例不在 `numstat` 里：取集成侧后无净改动。

## 七、`task-389` 要做的

1. 在 `attempt(closure)()` 模型上补出"`edit` 块交出 `commit` 做成的函数、故障在边界内"的行为，让那 6 条甲转绿；
2. 不要删这 8 笔、不要 skip 更多、不要为这 6 条红改断言；
3. 复测口径同本页第四节：同一命令、同一棵树，枝与尖端各跑一遍，逐名差集 0 新增红才判可合。