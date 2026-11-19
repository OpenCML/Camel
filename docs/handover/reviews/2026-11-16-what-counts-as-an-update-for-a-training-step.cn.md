# 案例训练步里，什么算一次更新？

> 一页一问。**默认建议写死在下面，本轮按默认走，不另等。**

## 这一页要的答复

作者给 S3b 的判据是：SSA 的值在最后一次使用处交给 `append`、路径赋值、`sgd` 一类更新时复用入参存储，
判据是 `camel inspect --copies` 对 `v = v->append(x)` 循环与案例训练步报「原地」。尺子已经做出来了
(`include/camel/runtime/storage_reuse.h#dumpStorageReuse`)，两条判据量出来的差得很远：

```
v = v->append(x) 循环 (test/cases/consistency/new_values/rebinding_loop.cml)
COPIES collect   updates 1 in-place 0 by-count 1 copied 0
COPIES push_all  updates 1 in-place 0 by-count 0 copied 1
COPIES snapshots updates 2 in-place 0 by-count 0 copied 2
COPIES boxes     updates 1 in-place 0 by-count 1 copied 0

案例训练步 (examples/case-study/train.cml)
COPIES __root__ updates 0 in-place 0 by-count 0 copied 0
COPIES loss     updates 0 in-place 0 by-count 0 copied 0
COPIES step     updates 0 in-place 0 by-count 0 copied 0
COPIES train    updates 0 in-place 0 by-count 0 copied 0
COPIES main     updates 0 in-place 0 by-count 0 copied 0
```

`train.cml` 的 `main` 里那次更新是 `sgd(layer, g, rate)`，而 `nn:sgd` 在
`modules/nn/module.cpp#NnModule::load` 里只声明成 `{.pure = true, .elementwise = false}`，注释写着
「The optimizers are functions of their arguments: they return new trees and states」；
`modules/nn/optim.cpp#sgdLeaf` 也确实用 `newLike(p)` 造一棵新树交回去。它没有原地形式
(`OperatorTable::setInPlaceForm`)、没有 `include/camel/core/resource_semantics.h#ResultAlias`、
没有 `include/camel/core/operator_table.h#declareReusesOperand`，所以尺子认不出它是一次更新。

**这不是尺子量不到，是被量的东西不在图里。**

## 默认建议（按此开工，不另等）

1. **S3b 的判据拆成两条。**
   **(a) `v = v->append(x)` 循环**：由 `copied`（今天是 3 处 `copied` + 2 处 `by-count`，
   见上面的 `rebinding_loop.cml`）变为 `by-count` 或 `in-place`。
   **(b) 案例训练步**：**先给 `nn:sgd` 声明「哪些写是原地更新」**（`ResultAlias` 或
   `declareReusesOperand`，二选一是 S3b 的设计），**再**要求它报 `by-count`。**在声明落地之前，(b) 记
   「判据待建」，不算 S3b 未完成。**
   ——(b) 写死成这个顺序，是因为作者上一轮已经明确「S3b 不过，第一件不算完成」：如果 (b) 一直不可达，
   第一件就永远不算完成，而卡住它的原因是缺一条声明，不是缺一个优化。
2. **「原地」是两句话，不是一个词。** `COPIES` 的「原地」包含两个答案：`in-place`（编译期决定复用）与
   `by-count`（运行时按计数决定复用 —— 设计原则 10.5 那条，计数为 1 时复用入参存储；**它就是原地，
   只是判定时机在运行时**）。**S3b 的判据是「不能是 `copied`」；`in-place` 与 `by-count` 都算过。**
   把判据写成「只接受 `in-place`」，会让今天每一处都必然失败，包括那些本来就在原地工作的，而卡住它的
   原因是措辞。
3. **判据 (a) 已经有一个过线样例可以对着改**：`test/cases/modules/optim/training.cml`（案例训练循环的
   缩减版）在 `main` 里 `curve->append<value>`，今天报一行 `COPIES training.cml:89:78 append by-count`
   与 `COPIES main updates 1 in-place 0 by-count 1 copied 0` —— 那条 append 是循环状态形式，
   运行时问数组，属「已过线」；`rebinding_loop.cml` 里那 3 处 `copied` 才是 S3b 要动的。

## 若作者另有裁定

- **若 (b) 说「不必等 `nn:sgd` 的声明」**：请指出案例训练步里「更新」的判据落在哪个节点上 —— 今天
  `train.cml` 的五张图里一个更新节点都没有，尺子无从报，只能报 `updates 0`。
- **若 (b) 说「`sgd` 不算更新，判据只算 `append`」**：请把判据里的「`sgd` 一类更新」删掉，我照改，
  并把计划注释与 `docs/cli.md` 一起改，让三处说的是同一件事。
- **若 (a) 说「只接受 `in-place`」**：见默认建议第 2 条。我按裁定改注释，但读数不会变，改的是判据的措辞。
- **若 (b) 说「先给 `nn:sgd` 声明 `ResultAlias`，再单独量」**：那 S3b 的完成时点变成「声明 + 报 `by-count`」，
  我按这条记，不额外等。

## 这一页不改变的事

本页不改任何代码，也不改 `nn:sgd` 的语义：那条声明是 S3b 的决定，动它就是改语义，不是修尺子。
尺子只报读数，与 `include/camel/runtime/exec_plan.h#StorageReuse` 一致。
## 决策者答复（2026-11-16）

- **第 2 条同意**：`in-place` 与 `by-count` 都算原地（10.5：计数为 1 时复用入参存储，判定时机在运行时也是原地），S3b 的判据是"不能是 `copied`"。
- **第 1 条 (a) 同意。**
- **第 1 条 (b) 不同意"判据待建、不算未完成"**：给 `nn:sgd` 声明"复用参数树的存储"（`declareReusesOperand`，一条声明，不改语义，仍是纯函数）就是 S3b 的一部分，而且很小。负责人定 `var` 方案的前提就是"`params.w -= …` 与 `sgd(params, g, lr)` 原地进行"，这一条不能挂起。同一笔把 `nn` 模块里其余优化器（带状态的那些，状态也一样）一起声明。验收：案例训练步与 `training.cml` 的 `COPIES` 对 `sgd` 报 `by-count` 或 `in-place`，参数树没有 `copied`。
