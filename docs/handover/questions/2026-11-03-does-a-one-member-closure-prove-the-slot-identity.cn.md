# 闭包只有一个成员时，「槽的类型等于参数类型」能证明它是这个参数的值吗？

## 问什么

按类型找家在 `07466c5e2` 里删掉了（`git grep carriedPortOf` 零命中），现在一个参数的值在反传那半里
**按身份**取：参数序号 `at` 就是闭包成员的序号，取到的端口就是那个值
（`modules/transforms/transpose.cpp:886-903`）。取之前有一道校验：

```cpp
const size_t slot   = at;
Type *const askType = function->funcType()->normTypeAt(at);
const camel::runtime::GCNode *slotMember =
    slot < closureOf.size() ? function->node(closureOf[slot]) : nullptr;
if (askType != nullptr && slotMember != nullptr &&
    slotMember->dataType() != nullptr && slot < closurePorts.size() &&
    camel::core::type::typeEquals(slotMember->dataType(), askType)) {
    forwardOf[ref] = closurePorts[slot];
    return forwardOf[ref];
}
```

**校验的是类型，不是身份。** 当闭包只有一个成员（`residuals=1 invariants=0`，loop 那一半今天就是这个
形状）而这个成员的类型恰好等于第 `at` 个参数的类型时，这个条件**必然成立**，哪怕那个成员根本不是这个
参数的值。要裁定的是：这道校验够不够，还是要把它换成身份的比较。

## 1. 为什么它今天不吐错数

因为后面还有一处拒绝挡着。`vjp_loop.rounds_are_undone_in_reverse` 的原文：

```
REINTERP split function=... skip=call-value-missing param=0 node=23 arg=2
transforms: the callee's transpose was built, but the parameter at position 1 reads a value this call does not carry
```

也就是说：误过的槽今天**没有走到出口**，数字没有产生。这不是"校验对"，是"校验之后还有一道墙"。

## 2. 危险面在哪

这道墙属于"还欠的那一种形状"（端口次序覆盖了数组那一种，loop 那一种还欠残差）。一旦那一半的残差补齐、
`skip=call-value-missing` 不再触发，误过的槽就会**直接进闭包读**，也就是把 `carriedPortOf` 删掉的错误
换一种写法请回来：那次是"按类型搜"，这次是"按类型校验，而校验恰好真"。

两者的错法相同：`xs[0] + ys[0]` 这种"两个同类型参数、答案要按**这个**参数的长度写"的情形，会让答案按
另一个参数的长度写。`07466c5e2` 的提交信息里记的正是这个读数：`gy` 长为 3、静默错数。区别只在于：表被
删了，校验还在，而校验在单成员闭包里没有分辨力。

## 3. 可选的三条路

- **A：把校验换成身份。** `Residual` 已经带 `parameter` 字段（`split.cpp` 的 `one.parameter =
  parameterValueOf(ref)`），所以"这个成员是第几个参数的值"是已知的；校验可以变成 `residuals[slot].parameter
  == at`，类型只作附注。
- **B：身份只走 `residualsOf` 的编号。** 现在 identity 这一段直接拿 `at` 当槽号，跳过了编号；改成从
  `residualsOf` 的编号里查，单成员闭包就不会"恰好"成立。
- **C：不动，只记风险**，等 loop 那一半的残差补齐时一起改。代价是那一次补齐必须同时收紧这里，否则
  补齐本身就会打开出口。

本页不替作者选；选 C 时请把"补齐残差"与"收紧校验"写成同一个动作，不要分两次。

## 4. 测量与文件

- 校验点：`modules/transforms/transpose.cpp:886-903`（`typeEquals` 那一句）。
- 案例：`test/plans/feat/compile/transforms.plan.toml#vjp_loop.rounds_are_undone_in_reverse`（`[XFAIL]`，原文见第 1 节）。
- `git grep carriedPortOf` → 零命中；`split.plan.toml` 与 `remat.plan.toml` 相对 `origin/develop`
  的 `git diff --stat` 为空（四处 `residuals=` 断言一字节未动）。
- 车道读数：`feat/compile/transforms` 80 = 77 过 / 2 红 / 1 known，`split` 19/19，`remat` 6/6。