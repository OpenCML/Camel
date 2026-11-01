# T+24 的状态，以及中午那一小时去哪了（状态页，无需裁定）

> **这一页回答作者 13:50 催办里的两句**：M2 的下一笔、M4 第二步从哪个文件起刀，以及"中午那次一小时停顿的原因"。
> 一页一问的规矩不适用于状态通报：这里没有问题要答，作者读完可以整页划掉。

## 1. M2：分支的 `split`，读数形状已经建好，出数这一步正在做

作者 13:50 给的验收形状是两条，第二条更小：

```
f(x) = if x > 0 then x*x else 3*x     vjp 在 x = 2.0 / -2.0 上得 4.0 / 3.0
```

今天在做的正是它，只是验收数先按 `0.5*x` 那条臂定的（`x = -1.0 → 0.5`）。**两条都留**：`0.5*x` 验"不靠条件也能算对"，`3*x` 是作者给的形状。已落的读数：

```
WALK half=backward region=1 arm=0 branch=6 members=3 ports=0 nested=0 outside=2
ARM  half=backward arm=0 region=1 yield=13 members=2 value=7 tangent=8 probeRegion=1 probeMembers=3 probeJoin=16 wired=1 inputs=2 from=[p0 p1 ] condition=5 conditionRegion=0 conditionCopied=5
REINTERP split function=jvp_of_leaky skip=branch exit=Join condition=5 why=… the arm is not *chosen* yet: the condition picks it, and reading that condition to pick is the next cut, not this one
```

一处需要更正的读数（我们自己上一轮说错的）：**`outside=2` 是 `x` 与种子 `dx`，不是 `x` 与条件**——`conditionRegion=0`，条件在**父区域**。这恰好解释了 4.2 为什么成立：条件不属于任何一条臂，两个半边读**同一个**父区域的值，才可能选中同一条臂。

## 2. M4 第二步：从哪个文件起刀

`modules/transforms/vjp.cpp`（新文件，登记 `vjp:vjp` 算子 + 建图）。已量到的硬结果：`jvp` → `split` → `transpose` 三步走本模块自己的 kernel，两张包装图，四后端**逐位**相同（`27.0/27.0`、`18.0/12.0/9.0`）；`grad(cube)(3.0) = 27.0` 也走算子。

卡住的一处已立页问作者（`Vjp<F>` 的拉回在一元时是 `float` 还是 `(float,)`）；**今天的落法是保留按元数分写的两条声明**，因为 (a) 会改掉作者写明的 `grad(f)(x) = vjp(f)(x).1(1.0)`，(b) 要加一个"只有一个时取元素"的类型函数——这两条都不该由我们改。

## 3. 中午那一小时去哪了（作者点名要如实写）

不是主机、不是额度，是**我自己把协调者当成了跑闸门的人**。四条车道的改动并进 `m-train` 后，我连续跑了三次全量闸门（每次十几分钟），期间不派工、不看读数；而 32 核主机上同时还有车道的构建在跑，`load_class.explain_under_load` 单跑 `4.40 s`、8 路并行时读到 `184.50 s`，于是同一件事又被重跑了一遍。

已经改掉：**合并且跑闸门、推送全部交给常驻的合并车道代理**（一轮只允许一个进程写 `m-train`），协调者只做派工、审查读数、裁定默认建议、写这一页这样的问题页。这条已写进 30 分钟例行提醒的第一条，**以后同类停顿若再出现，原因会一行写在这里**。

## 4. 今天已落、可核的数

| 项 | 读数 |
|---|---|
| `transpose` 穿过带逃逸的循环 | `read_one = 15.0`、`read_twice = 22.0`、`read_outer = 29.0`（`answer=summed` / `answer=per-target`） |
| `split` 穿过调用 | `REINTERP split function=jvp_of_g … residuals=3`、`g value=10.0 dgdx=6.0` |
| 残差不可见 | `SemanticDiag::ResidualsAreNotVisible`，诊断表 306 → **307** |
| 迁移表 | **127 例 / 5 过**，其中库层真正走通 **1** 条 |
| `vjp` 建图 | 四后端 `27.0/27.0`、`18.0/12.0/9.0` 逐位相同 |
| 分支 `split` | 臂的 region + Join 已建、臂体已复制、`wired=1`；**还差"按同一个条件选臂"** |