# 打包把声明说的那一个盖掉了

**日期**：2026-11-19
**提出者**：车道 N（`lane-ssa`，基线 `abf953cb3`）
**状态**：待裁（本页写死一条默认建议）
**触发**：上一页把「元组流进算子实参」记成第三类读者；本页顺着它走到源头——不是第三个读者读错了，是**声明说的那个类型在图身上不再存在**。

## 1. 现象

```
test/cases/linear/byref/var_rule_subpasses.cml:60:43:
No matching function found for argument types: <> ((int, int), int);
operator '*' is not defined for these operand types
```

`bump(var x: int)` 声明的结果是 `void`，`let r1 = f(c, 2)` 里的 `r1` **本来没有值**，`r1 * 0` 本该是「拿一个没有值的东西去乘」。

## 2. 源头（函数 + 那一行）

`src/compile/gir/builder_control.cpp`，`Builder::handBackVariableParameters`：

```cpp
node_handle_t packed = currGraph_->addFillNode(
    exitType, runtime::makeFillPayload(runtime::GCFillKind::Tuple, slots));
```

同一函数里 `hasResult` 为假（`bump` 声明 `void`）时，`held` 只剩各 `var` 形参的终值，`exitType` 是一个**只有形参**的元组 ⇒ 这个 FILL 把一个「没有值」的调用变成「一个 1 元组」。而调用点的判据 `callHandsBackResult(targetGraph)` **只问位、不问 `void`**：`!isVoidGraphExitType(...)` 那个守卫只留在**结果那一份**上（补丁 211–213），在「这次调用有没有值」这一处，它在我把判据从**按签名判**改成**按位判**时丢了（补丁 398）。

## 3. 为什么下游读不到

`builder_operators.cpp:3957` 读的是 `valueTypeOf(nodeTypeOf(input.node))` —— **节点类型**。而 `graph->setFuncType(funcType->withExitType(exitType))` 已经把图身上的类型换成元组，声明里那个 `void` **不再存在**。所以那一处不是「少一个判断」，是**判断所需的事实被销毁了**。

## 4. 两条路

- **(甲) 图身上同时留着两样**：「声明说它是什么」与「打包后它是什么」（声明的出口类型 + 位并排）。代价：`GraphExitType`/`graphExitTypeOf` 这一族是今晚几十笔的公共地面，动它要把所有读者一起过一遍。
- **(乙) 在调用点再记一位**：「这次调用没有值」，由 `handBackVariableParameters` 之外另写。代价：两位可以互相矛盾（声明 `void` 但位说「有结果」），而作者已经否掉「按形状判」，再添一位等于把同一个事实分两处保管。

## 5. 默认（写死：走甲），以及它与作者 `c9e1ca009` 的关系

**默认走甲**，且限定：**只加「声明的那一个」这一份信息，不加新的读者规则**——`graphExitTypeOf` 的产出带上「打包前它是什么」，读语言类型的地方（算子实参、分支诊断、`inspect`）读它，读「调用交出什么」的地方仍读打包后的那一个。乙不取，因为两个位会互相矛盾，而矛盾正是今晚反复出现的形状。

**与作者 `c9e1ca009` 的关系，一句**：作者说「**语言类型与图的出口类型被当成了一个**」。本页量到的是：在这个源头上，**「当成一个」的字面形态就是「一个把另一个盖掉」**——`withExitType` 不是并存了一个第二类型，它是**替换**了声明的那一个，于是一切原本读声明的地方都改读了打包结果。这句话把那条抽象的裁定，落到了一处 `addFillNode` 与一处 `setFuncType` 上。