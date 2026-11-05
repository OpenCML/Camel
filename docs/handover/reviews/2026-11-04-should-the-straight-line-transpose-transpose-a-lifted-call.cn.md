# 直线路径的转置要不要像分支臂那样，把一个抬升调用穿过它的原函数转置？

状态：待裁（默认建议在第五节，按默认先记缺陷、不改代码）。
发现：2026-11-04，`lane-withgrad`，树 `ce2691f54` + `66a496134` + `036960b18`。

## 一、读数

`split` 通了之后，库层 `with_grad` 的组合第一次走到了 `transpose`，报文（exit 1）：

```
transforms: transpose has no transpose for this function: this call's callee has no transpose of its own, so the call cannot be turned around
TRANSPOSE function=jvp_of_weighted skip=signature
TRANSPOSE function=split_of_jvp_of___g_247___backward skip=call-no-transpose node=5 callee=jvp_of_weighted
```

表达式是 `vjp((ww: float) => weighted<ww>(2.0))(3.0)`，其中
`with <x: float> func weighted(y: float): float { return x * y + x }`（`test/cases/modules/autodiff/grad_first_with_parameter.cml` 的第一组形状）。

两行 stderr 是同一件事的两个层次，读的时候别混：

- **第二行**是调用点：直线体里的 `node=5` 调用 `jvp_of_weighted`，调用点拒绝（`TransposeRefusalCause::CallNoTranspose`）。
- **第一行**是被调方：`transpose` 试着为 `jvp_of_weighted` **自己**建一份转置，`transposeSignature` 给不出签名（`skip=signature`），于是成员的构建失败，调用点才拒绝。

## 二、机制：两条路径对同一件事给出两个答案

`transpose.cpp#buildTransposeMember` 建一个函数的转置成员。它里面**两处**处理"调用一个被抬起的图"：

1. **分支臂的路径**已经认出这件事，并且**穿过它被抬起的原函数**转置，不去建抬升图自己的转置。那里的注释是这一句：

   > A lifted call is transposed **through the graph it was lifted from**. The tangent half of a lift is linear in the tangents, so the cotangent of tangent i is the primal's cotangent of value i -- and that is computed from the values only.
   > `jvp` records the graph a lift came from (`kGraphRefLiftedFrom`), which is the same record `split` reads to retarget a call.

   判据就落在 `callee->liftedFrom()` 上：有效就取原函数的 id，按原函数建成员。

2. **直线体的路径**没有这一步。它拿到 `calleeId` 之后**直接为这个被调方建成员**：

   ```
   transposed = group.reserve("transpose_of_" + callee->name(), ...);
   if (!buildTransposeMember(state, callee, transposed, os, participates)) {
       refuse(TransposeRefusalCause::CallNoTranspose);
   ```

   而被调方是 `jvp_of_weighted`——一个**答案是 `(value, tangent)` 对**的图。`transposeSignature` 是按"函数答一个值"写的，对一个答对的图给不出签名，于是 `skip=signature`，`buildTransposeMember` 返回 false，调用点按名拒绝。

**所以缺陷不是"没有规则"，是同一份代码里，分支臂认得抬升调用、直线体不认得。** 分支臂能做到的（穿过原函数转置），直线体没有理由做不到——它只是没写那一步。这也解释了为什么以前没撞上：引擎那条路的后一半里没有"直接调用一个抬升图"的直呼；`split` 把一个被抬起的规则调用留在后一半里是本设计有意为之（`split.cpp` 里那条注："the tangent half rebuilds the pair from the values it is handed"），于是这条直呼第一次出现在直线体里。

## 三、这条直呼是故意的，不是缺陷

要说清，免得裁的人以为是 `split` 留错了东西：`split` 的后一半保留对抬升图的调用，是 `pullback`/`split` 既有设计的一部分——抬升图的答案是 `(value, tangent)`，值的一半不能读切线，所以值的那一半改调原函数（`split` 已经把这种调用重定向到原函数上），而切线的一半则**用调用方交给它的值把那个对重建出来**，这正是它需要那些值当残差的理由。因此"库层组合不该让后一半留一个抬升调用的直呼"这条替代路线，不是把一行改掉就行，它会改变切点对"后一半算什么"的定义，需要单独裁定。

## 四、两个候选

- **甲：直线路径补上分支臂已有的一步**（认 `callee->liftedFrom()`，穿过原函数转置）。改动是 `transpose.cpp#buildTransposeMember` 里直线体那一段加一个分支，判据与分支臂同一句。被调方是一个普通函数（没有 `liftedFrom`）时行为一字不变。
- **乙：库层组合不让后一半留下对抬升图的直呼**（例如 `vjp` 对"体里含 `with` 调用"的形状换一种拆法）。这不是一行；它要作者说清切点的定义要不要改。

## 五、可裁的一句话与默认建议

**问：直线路径要不要补上分支臂那条规则（一个抬升调用穿过它被抬起的原函数转置），还是库层的组合不该让后一半留下一个抬升调用的直呼？**

**默认建议：甲（补规则）。** 理由只有一条，但够硬：**同一份代码里两条路径对同一件事给出不同答案，那本身就是缺陷**；分支臂能做的事，直线路径没有理由做不到。乙要改的是切点的定义，代价比补一步大得多，而且今天没有任何读数说明切点的定义错了。

## 六、代价与哨兵

- **放宽的范围要钉住**：补的那一步只对 `callee->liftedFrom()` 有效的被调方生效，普通被调方仍走原来那条"自己有没有转置"的路。若写成"任何被调方都先试穿过原函数"，那就是把真拒绝变成错数的口子。
- **哨兵是那两条红**：`transpose_loop_escape.two_values_of_two_kinds` 与 `transpose_loop_escape.second_state_of_the_same_loop` 今天是红的（`feat/transforms` 与 `feat/compile` 里各一次）。补完规则后它们**如果变绿且数对**，是好事；**如果变成"静默错数"（不再红、但断言改成能过的样子）**，那就是拿一条红换一条更坏的绿——按 **对 > 明确拒绝 > 错数 > 崩** 的次序，这是往下走了一格，必须回滚。判据里要把 `transpose` 那两条的数**逐元素**钉住再改，否则分不出"绿了"和"错得不报了"。
- **`transpose` 的影子判据**：`feat/transforms/split` 19/19、`feat/compile/transforms` 87 = 84/2/1（红仍是那两条）在这条改动前后都该一样；不一样就说明这一步动到了不该动的地方。

## 七、谁在等它

- 库层 `with_grad`：float 形状已经在 `split` 之后卡在这里，这是它剩下的第一个站。
- tensor 形状还卡在更早的一站（`split` 的闭包端口归类，本页不涉），结构体与两个 `with` 参数形状卡在 `jvp`。四站的顺序记在同目录
  `2026-11-03-when-a-lambda-ends-with-a-with-call-what-does-split-cut.cn.md` 的第七、八节。