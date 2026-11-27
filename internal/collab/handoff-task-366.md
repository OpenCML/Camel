# 交接：`task-366`（edit/view 的 `d: Graph` 根句柄）—— 剩下三处 edit + 一次 build

接手第一件事：读本页，再读 `internal/collab/constructed-operator-needs-a-group.md`。树在 `lane/blockroot @ bbff2c9703` + 7 个未提交文件，**可编译**。

## 已经确证的三条（不要重测）

1. **块头的 `import` 只作用于块体（闭包体内），块表达式的调用点对它完全不可见** —— build12 用 `graph_of`（`graph` 模块真实导出、块体里随处可用）发名字 dref，**同样 `Unresolved reference: 'graph_of'`**。与 `gir/names.cpp:232-236`「the module is a layer of the body」逐字一致。
2. **别名只能按算子身份改投，不能按名字引用** —— 没有名字可用（第 1 条）。
3. **改投与造值不同**：`attempt` 那条（`builder_call.cpp:2368-2381` → `attemptFunctionValue`）是"造值"；我们要的是"改投"，位置在 `builder_call.cpp:1343` 与 `:2443` 的 `currGraph_->addOperNode(targetOperator, 0)` **之前**。

## 形参侧已经成立（不要动）

闭包形参 = `(d: Graph)`，build9 原文 `Function of type '(d: Graph) => Node[] pure'`；`root_observable_as_function` 已变成 `Invalid call to 'd' with argument types <> (…)`（"`d` 不是函数"成立，措辞待按产品实际更新断言）。

## 缺的那一块（下一格要补）

**"模块 → 该名字导出对应的 `oper_group_ptr_t`"的公开入口读不到**：模块侧只有

```
src/compile/src/gir/builder.h:199   std::unordered_map<std::string, oper_group_ptr_t> nativeOperatorGroups;
                                    // the groups those declarations make, by name: the ones the module exports and the private ones
```

而 `Builder::internalOperatorGroup(OperId)`（`builder_variables.cpp:71-78`）**只查 builtin**。⇒ 需要加一个只读访问器（形如 `operatorGroupNamed(const std::string&) const`），与既有的 `nativeOperatorGroup(OperId)` 并列，**不改行为**。

## 剩下的四处（一次改完，一次 build）

```
a) 只读访问器（上一节）
b) builder_call.cpp:1343 与 :2443 统一走一个 helper：targetOperator->uri() == ":block/root_graph"
   ⇒ 换成 graph:graph_of 的组（按 uri 查，不按名字）
c) builtin 组 OperatorGroup::create(kBlockRootGraphName, {{":block/root_graph", DynamicFuncTypeResolver…}})（只给类型）
   + 特性表 registry.set(":block/root_graph", {.pure = true})（照 attempt 在 builtin/executors/builtin.cpp:886-895 的口吻）
d) GCT 撤回临时探针 `makeDRef("graph_of")`（gct/builder_expr.cpp:234），改用
   makeDRef(constructedOperators().blockRootGraph, kBlockRootGraphName)
```

`graph.cml` 的 `export func block_root_graph …` 与 `graph/txn.cpp` 的 `txnKernels` 行**先留着**，删是最后一刀（等判据全绿）。

## 判据六条

```
① root_observable_as_graph 绿（1 实参）
② root_observable_as_function 红 + 产品实际措辞（断言文本更新，注释写明"同一拒绝的措辞更新"）
③ 其余 15 条绿
④ 形参仍是 (d: Graph)（贴类型原文）
⑤ std::gir 里出现 graph_of 节点、无 :block/root_graph（贴一行原文）
⑥ numstat + 新 tip
```

若 ⑤ 显示节点已是 `graph_of` 而 ① 仍红：贴两条用例原文停手报协调者（"改投对了，下游还有一处"，与名字无关）。

## 树里那 7 个文件的来路

```
operator_names.h          kBlockRootGraphName
operator_table.h / .cpp   ConstructedOperators::blockRootGraph + intern("graph:block_root_graph")
gct/builder_expr.cpp      形参 (d: Graph) + 调用点 1 实参（临时 makeDRef("graph_of") 探针）
parse/ast/builder_expr.cpp  删 `let d = f`
stdlib/graph.cml          export func block_root_graph … { return graph_of(f) }（待删）
stdlib/graph/txn.cpp      txnKernels: block_root_graph → graphOfKernel（待删）
```