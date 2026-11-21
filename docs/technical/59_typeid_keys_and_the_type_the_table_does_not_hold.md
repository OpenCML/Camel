# 59. `TypeId` 作键：`kNoTypeId`（0）是通配符

**一句话。** 类型在本项目里"有结构、没有身份"（`include/camel/core/type/intern.h`）：一个类型只有走 `TypeTable` 的入表路径才拿到 `TypeId`，没入表的类型 `typeId()` 恒为 `kNoTypeId`——也就是 **0**。于是**任何**拿 `TypeId` 当键或相等判据的地方，都会把"未入表类型 A"、"未入表类型 B"和"没有类型"混成同一个键：0 成了通配符。release 构建下没有任何运行时提示，唯一的守卫是 debug 构建里 `src/runtime/graph.cpp#typeIdOf` 的那条 `ASSERT`。

本页记录这一类缺陷、本次实测到的 30 处下游站点（8 个符号）、反向的一格（同一个未入表类型永不相等）、0 的两个来源，以及本轮修的**源头一处**。按仓库规则，本页只用 `path#Symbol` 指代码，不给行号。

## 1. 入口与实测读数

被导入模块里的 `rule`，若右值是**函数值（闭包）**，其 `__rhs` 图的签名（`GCGraph::funcType()`）是一个 **open** `FunctionType`（`intern.h` 的 "What is not"：open 签名是前端正在构造的工作对象，表永不持有），而这个签名的**退出类型**——闭包自己的签名——从未入表，`typeId() == 0`。

探针是 `std::rulesets sig=true`：对程序里每个 `<rule>__rhs` 图，逐个分量（`exit` / `with i` / `norm i`）打印 `interned=`、类型文本，以及（分量是签名时）`open=` 与 `exit=`。修前读数（`jvp` 全部规则）：

```
SIG rulesets.jvp::jvp_pow_d__rhs exit interned=no type=(dx: float, dy: float) => float pure open=yes exit=yes
SIG rulesets.jvp::jvp_float_from_int__rhs exit interned=no type=() => float pure open=yes exit=yes
SIG rulesets.jvp::jvp_float_from_i32__rhs exit interned=no type=() => float pure open=yes exit=yes
SIG rulesets.jvp::jvp_float_from_string__rhs exit interned=no type=() => float pure open=yes exit=yes
```

用例逐分量统计的结论是 **13 个分量** `interned=no`（全是 `exit`，全是"右值是闭包"的那些规则；参数分量都已入表）。修后同一条 `exit` 分量变为 `interned=yes`，`open=` 由 `yes` 变 `no`（分量已是表里的闭签名），`type=` 文本不变；用例覆盖的 27 个分量全部入表。

## 2. 本缺陷类：30 处下游站点（8 个符号）

口径：`TypeId`（或它的 canonical 形式）被当作**键或相等判据**、且该 id 可能是 0 的位置。每处用它的代码表达式指认。

### 2.1 `src/passes/opt/defun/defunctionalize.cpp#planOf` —— 11 处

`plans[type->typeId()]`（`planOf` 自身）· 五处把类型交给同一张表的 `planOf` 调用（端口节点、普通节点、调用点、两处加宽）· `plans.find(inputType->typeId())` · `wrapsInto[written->typeId()]` 与 `insert(planned->second.type->typeId())` · `mergedInto[written->typeId()]` 与右侧的 `planned->second.type->typeId()`。

**0 与谁混淆**：所有未入表的函数类型共用同一个 `TypePlan`，`plan.type` 被后一个类型覆盖；两个毫不相干的闭包因此被并进同一个 enum family——变体集合、捕获、构造点、调用点全部混在一起，两张加宽映射还会把不相干的两族记成同一条加宽边。

### 2.2 `src/passes/opt/defun/defunctionalize.cpp#mentions` —— 1 处

`type == target || type->typeId() == target->typeId() || type->equals(...)`。

**0 与谁混淆**：两个未入表的类型互判相等，于是"签名里含有目标类型"对**任意**两个未入表的签名都成立——本该放行的族被当作含有函数值而拒绝，或本该拒绝的被放行（读者检查失效）。

### 2.3 `src/passes/opt/generic/screen.cpp#mayMerge` —— 1 处

`classes.try_emplace({canonicalId(value.type), value.slot}, ...)`。

**0 与谁混淆**：两个**类型不同、位相同**的常量落进同一个 `(canonicalId, bits)` 类，被判成"同一个常量"，`mayMerge` 于是说这图可合——常量类与表达式类混成一类。

### 2.4 `src/passes/opt/generic/rewrite.cpp#contentHash` —— 1 处

`mix(canonicalIdOf(record.typeId))`。

**0 与谁混淆**：所有未入表节点的类型字都 hash 成同一个 0，两张类型不同的图得到相同 `contentHash`，e-graph 把两份内容认成同一份（一类里出现两个不该相等的表达式）。

### 2.5 `src/runtime/writings.cpp#NodeKey` 与 `src/runtime/writings.cpp#DraftWritings::Index` —— 2 处

`NodeKey::typeId` 字段（`operator==` 逐字段比较）与 `Index::byKey`（以 `NodeKey` 为键的哈希表）。

**0 与谁混淆**：两个类型不同的节点（`kind`/`payload`/端口相同）成一个键，被当成同一份写作，`writings` 把不同型别的节点并成一个值。

### 2.6 `src/runtime/graph_edges.cpp#EdgeChecker::checkWritings` —— 1 处

等价类两端的 `canonicalIdOf(a->typeId) != canonicalIdOf(b->typeId)`。

**0 与谁混淆**：两端都未入表时"同类型"恒成立，等价类两端**必须同类型**这条图不变量被放行——两个不同类型的节点被记成同一个值的两种写法，而检查器说没问题。

### 2.7 `src/runtime/draft_session.cpp#sameContent` 与 rebase/commit 的图类型比较 —— 6 处

`canonicalIdOf(x[i].typeId) != canonicalIdOf(y[i].typeId)`（节点记录）、`canonicalId(before.funcType()) != canonicalId(after.funcType())`、`closureType()` 的同款比较、`staticDataType()` 的同款比较，以及两处调用：rebase 时"改写回去内容没变"就复用旧图、commit 时决定是否算新版本。

**0 与谁混淆**：两张图类型不同、但差异都落在 0 上时被判为"内容相同"——rebase 丢掉一个真的改了类型的版本，或 commit 把没有变的图记成新版本（两个方向都会让事实层拿到错的 revision）。

### 2.8 `src/passes/test/retype.cpp#RetypeEnumTestPass::apply` —— 7 处

`pair->typeId()`、两处 `countNodesTyped(..., pairId)`、`graphMentions(..., pairId, ...)`、`boxed->typeId()`、JOIN 的 `canonicalIdOf((*it)->typeId)`、臂与 JOIN 的 `canonicalIdOf(result->typeId) == joinType`。

**0 与谁混淆**：臂与 JOIN 的"同类型"判据在 0 上恒成立，这条测试遍的 `arms_ok` 自证成立；`countNodesTyped`/`graphMentions` 也会把别的未入表类型算成 pair 类型的出现。

**合计：11 + 1 + 1 + 1 + 2 + 1 + 6 + 7 = 30 处，8 个符号。**

## 3. 反向的一格：`sameCanonicalType` 让"同一个"未入表类型永不相等

`include/camel/runtime/specializations.h#RuntimeSpecializationBindingKey` 的 `operator==` 用 `sameCanonicalType(type, other.type)` 比较绑定类型，而 `src/core/type/text.cpp#sameCanonicalType` 对未入表类型**返回 false**：

```cpp
TypeTable &table  = TypeTable::instance();
const TypeId left = table.canonicalId(a);
// kNoTypeId is the id of every type the table does not hold, so it says nothing about a
// structure; two such types are the same type only by being the same object, above.
return left != kNoTypeId && left == table.canonicalId(b);
```

`intern.h` 的注释解释过为什么：0 不携带结构信息，所以不能拿它说"相等"。于是同一个结构、每次重建都是新对象（都是 0）时，specialization 的键**永不命中**——每一圈都给同一个被调用者造一份新副本（`src/runtime/draft_inline.cpp#instanceParameterType` 的注释正是为这件事写的）。

这一格是"0 当通配符"的**反面**：`sameCanonicalType` 选择"永不相等"，`NodeKey`/`contentHash`/`mayMerge` 选择"永远相等"。两者都错，因为 0 既不是"相同"也不是"不同"，它只是"不知道"。**正确做法是别让 0 进键**：分量先入表，`canonicalId` 才有值可说。

## 4. 0 的两个来源

1. `src/runtime/graph.cpp#typeIdOf`：冻结图记录节点类型时取 `type->closed()->typeId()`。它有一条 `ASSERT(frozen->isInterned(), ...)`，但 `ASSERT` 只在 debug 构建生效；release 下未入表类型**静默落成 0**，图照常编码。
2. `include/camel/core/type/intern.h` 的 `TypeTable::canonicalId`：直接读 `type->canonicalId_`，**不检查** `holds(id)`。未入表的对象 `canonicalId_` 就是 `kNoTypeId`；同文件的 `TypeTable::canonicalIdOf` 对 0 也返回 0，于是"0 的 canonical id 还是 0"，一路传播。

要堵住这个类，这两处都不能当"0 是合法答案"：`canonicalId` 若改成对未入表类型断言/报错，就要求**所有**进键的分量先入表——这正是本轮修源头做的事。

## 5. 这一轮修的是源头（1 处）

`src/core/type/composite/func.cpp` 里函数类型的入表路径：

- 新增 `FunctionType::internComponent` 与 `FunctionType::frozenParts`：签名在**造键之前**先把自己的**退出分量与每个参数分量**入表——分量是签名（`TypeCode::Function`）且不是表里对象时，取它自己的 `parts()` 走 `FunctionType::intern`，得到闭签名（这正是 `Type::closed` 对 open 签名做的事，这里补上"closed 但从来没入过表"的情形）。
- `FunctionType::intern`（closed 签名）、`FunctionTypeFactory::buildOpen`（open 签名，**图真正持有的那个**）、`OpenFunctionType::setExitType`（编译期把体的值类型写进签名的地方）三条路径都走同一规则。
- `FunctionTypeFactory::buildOpen` 的结果本身仍**不入表**——open 签名是构造中的对象（`intern.h` 的 "What is not" 不变）；入表的只是它的分量。

**为什么"给 id"不等于"改语义"（一句话）**：类型的结构、`equals`/`sameType`/`sameCanonicalType` 的判据、哈希算法一个都没动，变的只是"同一个结构现在有同一个 `TypeId`"——而"一个类型一个 id"正是 `TypeTable` 存在的定义；键里原本用分量**地址**当字（同一结构两个键），现在用 **id** 当字（同一结构一个键），这是把键修对，不是把类型改掉。

**做不到 intern 的一类分量（本轮的边界）**：`open` 且**还没有退出类型**的签名——它是前端正在构造的工作对象，此刻还不是类型（`hasExitType() == false`），`internComponent` 原样返回，`typeId()` 保持 0，由 `typeIdOf` 的 debug 断言报出。这里**没有**塞一个假 id；如果将来这类签名也要进键，正确做法是等 `setExitType` 决定后再入表，而不是给它一个数。

## 6. 待办

本页只修源头（1 处）。清单里的 30 处下游**一处未改**，仍靠 debug 断言挡着，属待办：其中 29 处是运行期与优化器路径（第 1–7 项），应逐步改成 `sameCanonicalType`/`canonicalId`+`holds` 的显式写法，或干脆要求"进键的类型必须 `isInterned()`"并在 debug 下断言；第 8 项是测试遍（`test::retype_enum`），随它自己的测试一起处理。

在这 29 处改完之前，**release 构建里仍可能出现 0 与 0 混键**——本轮只是让"规则右值是闭包"这一批类型不再产生 0。