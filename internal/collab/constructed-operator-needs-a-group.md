# 编译器自己造的算子引用：id 不查名字层，但要一个 native 组坐那个 id

来由：`task-366`（edit/view 的 `d: Graph` 根句柄）与 `task-205`（`#op` 两条身份合一）两处都撞上同一层。记在这里，省下一次同样的排查。

## 两条路，别混

```
① 程序里写的名字（用户面）        —— 走名字层（模块导出、块头名层），可以 Unresolved
② 编译器自己造的引用（dref + id） —— 不查名字层；它查 internalOperatorGroup(id)
```

`gir/builder_expr.cpp:941-949` 的 `resolveDRefNode` 对"带 id 的 dref"直接走：

```
const oper_group_ptr_t group = internalOperatorGroup(*oper);
// “A DREF the compiler made for an operator it knows by id: the group is the built-in
//  module's, which registered it; no name of the program takes part.”
```

而 `internalOperatorGroup`（`gir/builder_variables.cpp:71-78`）是：

```
context_->importModule("")->nativeOperatorGroup(oper)      // 内置模块（prelude / ""）
GUARD(group != nullptr, "The built-in module did not register the operator the compiler emits: " + uri(oper))
```

⇒ 那条 GUARD 的文案就是这条规律的官方表述：**"编译器发射的算子"必须由内置模块注册过**。id 只是表里的一个位置，位置上还得有组。

## 落地清单（四条，缺一条就是 `Unresolved reference` 或载入期拒绝）

```
1) OperatorNames 里给名字（例如 kBlockRootGraphName）
2) ConstructedOperators 里 intern 出一个 id，登记到 operator_table.cpp
3) 内置模块里 OperatorGroup::create(<name>, {{<uri>, DynamicFuncTypeResolver…}})   ← 样本：std/builtin/operators/tagged.cpp:285-296 的 attempt
4) 内置模块的 kernel 表里加那一行实现
```

GCT 侧用 `tree_->makeDRef(constructedOperators().x, name)` 发射；**用户面不必有这个名字**（`graph.cml` 的 `export func …` 可以删）。

## 两个"看起来是别的问题"的假象

- **`Unresolved reference: 'block_root_graph'`**：看着像名字作用域问题（我们也先按"块头名层没 push"查过），其实是 ③ 缺 —— `nativeOperatorGroup` 返回 null，GUARD 之前就被解析成 unresolved。
- **"块头的名字层是体的层，不是块表达式的层"**：`commit`/`nodes` 能在块体里解析，是因为它们**是用户写在体里的名字**，走 `NameRow::BlockHead`（`gir/names.cpp:244`，层挂在闭包节点上，只在闭包体内生效）。**编译器在调用点发的 dref 不在那一层里**，所以"给它 push 作用域"或"把实参挪进体内"都不是修法：前者位置不对，后者改了求值时点（与设计 :90 冲突）。

## 判据口径

改完不要只看"能不能编过"：**带 id 的引用通了以后，仍要验"用户面确实没有这个名字"**（删掉声明之后程序仍能编、但程序里写这个名字应当 Unresolved）——否则只是把一条名字路径换成了两条。