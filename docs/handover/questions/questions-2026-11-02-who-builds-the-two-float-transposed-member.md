# 那张"端口是两个 `float`"的被转置成员，是谁建的？

**状态：`lane-m4b` 卡在这里，已排除五条假设，其中两条是我自己的。留给作者一问。**

## 现象（读数）

`grad_of_an_array_parameter` 在 `feat.compile.transforms` 里是唯一红的用例，4 条边不变量：

```
node 14 (10): passes a float[] as ordinary argument 0 of 'transpose_of_rulesets.jvp::index_into<float>', which takes a float
node 14 (10): passes a int     as ordinary argument 1 of 'transpose_of_rulesets.jvp::index_into<float>', which takes a float
node 16 (10): 同上两条
```

## 已排除（每条都带原文）

1. **`state.callees` 的键**：`rt::toValue(calleeToTranspose->id())`，而 `reserve` 每次 `context_->graphIds().allocate()` 取新 id ⇒ 不会撞。
2. **`mangledName` 不是构造出来的**：`src/compile/gir` 里没有一处生成它；给成员传的是我们手写的 `"transpose_of_" + name()`，**里面没有类型**。
3. **`reserve` 不按任何名字去重**：`const GraphId id = context_->graphIds().allocate();` 之后直接 `members_.emplace(id, …)`。
4. **`encode` 不按名字认图**：`auto *debugRecord = createGraphDebugRecord(mangledName, name);` 之后 `GCGraphBuildAccess::create(id_, …)`——名字只进调试记录。
5. **边检查读的是被调图自己的端口**：`const GCNode *port = callee->node(ports[i]); Type *want = port ? port->dataType() : nullptr;` ⇒ 它说 `(float, float)`，就是那张图的端口真的两个 `float`。

## 被我自己撤回的两条推断

- "登记/编码那一步只认名字"——`encode` 读完后**撤回**（见 4）；
- "`transpose_operator.cpp:43` 是第二处造形参的地方"——读完后**撤回**：那一行在 `transposeOperatorSignature` 里，算的是**操作符 `transpose(f)` 的结果类型**（注释原文：`transpose(f)`: `(f: (float) => float) => (float, float) => (float)`），**它不建图、不 reserve 任何成员**。它给出 `(float, float)` 只是因为**那个形状就是标量 `f` 的转置类型**，与病灶无关。

## 还剩的读数

本轮仪表在 `buildTransposeMember` 入口打出三行，**三个构建点全部走到**：

```
BUILD_MEMBER name=split_of_jvp_of_g_backward member=90 participates=0
BUILD_MEMBER name=autodiff::mul_tangent<float> member=91 participates=0
BUILD_MEMBER name=rulesets.jvp::index_into<float> member=118 participates=0 type=(x: float[], i: int) => float
```

⇒ `index_into` 的成员**只建了一次**，形参正确。所以 14/16 指着的那张 `(float, float)` **不是本次运行里 `buildTransposeMember` 建的**——它的来历还没拿到。

## 要作者答的一句

**一张被转置成员的形参，今天可以由几处决定？** 我的读数是"只有 `buildTransposeMember` 一处"，而图上确实存在一张不是它建的同名图。若答案是"只能有一处"，那这张图的来历就是缺陷本身；若答案是"可以有多处"，那它们之间的先后必须写下来，否则下一个读这段的人还会从头排除一遍。