# 残差用什么承载？

## 一句话

作者 05:10 的读法是「`split` 的前半交出 `(y, lin)`，残差成为 `lin` 的 `with` 参数」。这句话在**源码层**和**运行期内核层**遇到了同一堵墙：**两者都造不出"带运行期值的闭包"**。

## 三条死路（都量过，不是推的）

1. **图里造不出带运行期闭包的函数值。**
   - `Func` 节点的 `type` 是**这次调用的结果类型**，不是函数类型（`include/camel/runtime/graph.h:107`；边不变量原话：`calls 'X', whose value is a float, and is a <(float, float)> (float) => float pure`）。
   - lambda 是**编译期展开的嵌套图 + 捕获端口**（`graph.h:342` `kGraphRefNested // written inside this graph (a lambda, a branch arm)`，`graph.cpp:1773`）。
   - 图里的函数值只以**静态值**出现：`draft.h:856` `materializeStaticValue(slot_t, Type*, …)`，闭包随静态值落盘（`pullback.cpp:369` 就是这么用的）。
2. **源码侧 `f<values>` 绑不了运行期值。** `let lin = linear<residuals>` 之后 `linear` 仍是 `(float, float) => float`，报
   `Function of type '(float, float) => float' cannot be called with argument types: <> (float)`。
3. **静态闭包的类型是空元组**（`TupleType::create({})`，`transpose_operator.cpp` 造静态函数值时如此），放不进运行期残差。

## 三条路与代价

- **A：`split` 变成建图期算子。** 类型由建图结果回填（调用点先建图、再类型化）。代价：`split` 从运行期内核变成图变换，1b 提前。
- **B：给残差一个"按值的实际成员数追踪"的类型。** 代价：GC 侧要接受"声明成员数可小于实际"，等于放宽 `reportTupleHasNoTupleType` 那条守卫（`include/camel/core/rtdata/tuple.h:63`）。
- **C：闭包能携带运行期值。** 代价：新机制，图模型多一种节点，所有遍要看它。

## 默认

**A**：与作者「残差藏进闭包」的方向一致，且不需要放宽守卫。

## 附带的一条边界知识

`split.cpp` 的两个空成员表（`split.cpp:858`、`split_operator.cpp:59`）不是这个问题的原因，但它们让"值有 N 个成员、类型声明 0 个"这条 GC 读数成立——**在 A 落地前不要删它们**，否则读数会从"类型说 0"变成"根本没有类型"，更难读。