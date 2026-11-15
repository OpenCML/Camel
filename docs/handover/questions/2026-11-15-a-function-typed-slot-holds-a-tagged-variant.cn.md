# 函数类型的槽里装着带 tag 的 Variant：任何一次运行中收集都会崩

- 日期：2026-11-15
- 状态：独立立案，不在本车道修（不阻塞「计数做准」）；会让将来每一个运行中收集器崩
- 相关：defun 降级（`test/plans/feat/opt/defun.plan.toml`）、参考追踪（`[variant]` 诊断）、原则 10.5 的回收方向

## 现象

在 `NodeVMSchedPass::apply` 的入口图返回处请求并到达一次收集（`requestMajorCollection` + `safepoint`）后，
`defun` 计划从 **21 / 21 全绿**变成 **9 红、8 个 `killed by a signal`**。第一个红是 `defun.higher_order.lowered`
的 `std::nvm`，SIGABRT，stderr 原文：

```
[variant] the reference trace cannot use the type of a Variant: the type it was traced with has code
1610612746, which is not an enum's
[variant]   value=0x7518d46ff018 type=0x7518d002b140 tag=2 fields=0
[variant]   the type is the one the scan carried down from the slot the value was found
[variant]   in, not the value's own: a type that reads as no enum means the slot does not
[variant]   hold what it says; a pointer that cannot be read means the value outlived its
[variant]   type. Neither is a shape the value was written with.
[variant]   the type reads as: (x: int) => int pure
```

判读：defun 降级之后，**一个声明为函数类型的槽里放的是带 tag 的 Variant**，而参考追踪按下沉到槽的类型（函数类型）
去读它，于是撞上「这不是 enum 的类型」这条诊断。缺陷不在收集点放在哪里，在**槽的类型与槽里的值不一致**，而追踪
信任槽的类型。

## 判据：这条路径此前从未被走过

基线 `6c64f2042` 在**运行中没有任何收集点**（`nvm` 只在图边界设安全点，且整条链上没人请求收集），所以这条路径
从来没被走到，缺陷一句话也不说。加上一个收集点就立刻崩，说明：

- **任何一次运行中的收集都会撞上它**，不是「这次放错了地方」；
- 原则 10.5 要求 `Ref<T>` 靠计数**立即**释放、不靠收集，所以本车道**不**被它阻塞；
- 但只要有人给运行中加回收点（现实里迟早会有：堆耗尽、`--gc-stress`、将来的 profiler 触发），它就会崩。

## 建议（写死）

1. **单独立项**，不并进「计数做准」这一笔。
2. 修的方向二选一，由作者定：
   - 追踪按**值的 tag** 解释 Variant 槽，不按下沉的槽类型（诊断本身说的是「类型是从槽里下沉下来的，不是值自己的」）；
   - 或让 defun 降级**不与函数类型的槽共用**表示（另开一个槽类型 / 显式标记该槽装 Variant）。
3. 在修好之前，**不要**为任何理由在运行中加收集点；`--gc-stress` 下的 defun 用例目前跑不到收集（堆不够大就
   不收集），一旦堆变小就会暴露。