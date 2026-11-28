# 问题页：被抹除的节点，它的使用者该读什么？

2026-11-28 · `lane/progen` · 由 `task-444` C 的 seed 71 引出 · **不是结论，是提问**

## 事实（读数，不是判断）

`test/plans/probe` 上的一次性打印（环境变量门控，已删净、未提交）给出 seed 71 最小程序的 draft：

```
BRCH 2 | region the body (region 0) | norm[0] with[] wait[] out[1] | BRCH join 1 arms[1 4]
OPER 4 (:op/copy) | region arm 0 of BRCH 2 (region 1) | norm[3] with[] wait[2] out[5 16]
OPER 13 (:io/println) | region arm 1 of BRCH 2 (region 4) | norm[12] with[] wait[2] out[16]
GATE 16 | region arm 1 of BRCH 2 (region 4) | norm[4 13] with[] wait[] out[]
JOIN 1 | region the body (region 0) | norm[2] with[9 16] wait[11] out[]
```

else 臂的值门（`GATE 16`，region 4）的第一个操作数是节点 `4`，而 `4` 在 region 1（arm 0）。else 臂
自己没有任何格子（`:var/new` 在 arm 0），所以它也没有自己的 `:var/get`；它的值门读到的是**另一个臂
的拷贝**。

## 设计页覆盖到哪

`docs/design-principles.cn.md` 9.7（第 694 行）说的是一件相邻的事：

> 外部访问与对句柄的写（3.3），不会被隐式删除：它们由所在区域出口的 `GATE` 用到（3.1），提交时的清理
> 只清没有任何用处的节点，碰不到它们；只有点名它的编辑（`replace` 把它的读者连同出口的 `GATE` 一起
> 改接，或 `erase`）才让它消失

这句管的是**效应**（外部访问、对句柄的写）不会被隐式删掉，以及"点名它的编辑可以把它和它的读者一起改接"。
它没有说：**一个被隐式抹除（`eraseUnreadNodes`）的节点，它原来的使用者在抹除之后该读什么** ——
尤其是"那个使用者住在另一个 region"的时候。`builder_variables.cpp:899` 的注释把这件事交给了接口
（`eraseUnreadNodes` 的许可），把"拼接"交给 `docs/design/effects/edges.cn.md` 1.5、5.3；那两节讲的是
顺序边的拼接，不讲**值来自哪个 region**。

## 问题

1. 一个节点被隐式抹除、而它的使用者还在时，使用者应该读**被抹节点原来读的那个值**，还是**被抹节点自己**
   （"the erase hands the place on" 的字面读法）？
2. 若使用者与被抹节点**不在同一个 region**（本例：使用者 region 4、被抹/被接的节点 region 1），是否允许
   把值跨 region 交过去？还是应当拒绝这次抹除（`eraseUnreadNodes` 已有"集合外的节点还读它就保住整个单元"
   的许可，但那是**读**，本例的边在抹除之后才出现）？
3. 若"臂的值必须来自它自己的 region"是硬边界（本车道按此边界在做），那么 **else 臂在没有格子、值又被
   抹掉之后，它的值该是哪个节点** —— 它自己的出口 `GATE`？一个 `Data::null()`？还是这次 `if` 本来就不
   该有值（`joinsNoValue`）？

## 需要的裁决

一句口径即可：**被抹节点跨 region 的使用者该读什么**，或者"这不归建图管，归 `eraseUnreadNodes` 的许可
管"。在拿到这句之前，本车道**不改** `builder_variables.cpp` 的抹除段，也不改 `eraseUnreadNodes`。