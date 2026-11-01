# M2 的 `split`：穿过调用（有数）与穿过分支（有读数）

写这一页的原因只有一个：**这两件事很容易被下一个人混成一件**。`grad` 走分支今天是通的、有数；
`split(jvp(...))` 直接切一个带分支的函数今天不通、只有读数。说清哪一条是哪一条，比再修一次更值。

## 0. 一句话的分界

- **能跑的，钉数**：`control_flow_grad`（`feat.modules.autodiff`）穿分支、两条臂读不同的值，
  12 条入口全绿，读数是 `control_flow_grad a=6.0 b=0.5 c=80.0 d=12.0 e=0.0 w=-10.0`。
  其中 `a=6.0` 与 `b=0.5` 正是 `if x > 0.0 then x * x else 0.5 * x` 两条臂的导数。
- **不能跑的，钉读数**：`split(jvp(leaky))`（`split_branch_arms`）今天拒绝建图，
  `expect_exit = 1`，钉的是拒绝原文和 `WALK` 读数。**拒绝也是读数**，不是"没有结果"。

这不是"同一个功能的两半"，是**两条不同的路**：`grad` 的管线里 `split` 拿到的图与直接
`split(jvp(f))` 拿到的图不是同一张（一条经过 `grad` 自己的提升与收尾，一条是命令行式的直接组合）。
所以一条通了不代表另一条通了，反之亦然。

## 1. 穿过调用：`split` 的值半边改调原始函数（有数）

`g(x) = sq(x) + 1.0`，x = 3，`split(jvp(g))`：

```
REINTERP split function=jvp_of_g forward=82 backward=83 type=(float) => (float, (float, float, float)) pure residuals=3
g value=10.0
g dgdx=6.0
```

- 改前（原文）：`construct=a tangent port uri=- why=the value half reads the tangent of parameter 0`
  —— 一次提升调用的实参是"每个参数一个值 + 一个种子"，值半边要它就得读种子，而值半边不读种子。
- 改后：提升图记下"我是从哪张图提出来的"（第五个引用角色 `kGraphRefLiftedFrom`），值半边据此
  **把调用改叫原始函数**、只连偶数端口（值）；`accs 0` 就是那次调用本身。
- **一条不能省的前提**：提升调用的 pair **不是残差**。一个半边建的 pair 不能被另一个半边读；
  前一半交浮点、元组说 pair，是运行期类型错（第一版就崩在这里）。穿过提升调用只取它的**值参数**，
  `x` 才回到元组里 —— 于是 `residuals=3`，与手写内联的 `x*x + 1.0` **相等**。

钉在 `split_call_one`（nvm + fvm，`backends_agree`）。

## 2. 穿过分支：进得去，还没建（有读数）

`leaky(x) = if x > 0.0 then x*x else 0.5*x`，`split(jvp(leaky))`：

```
WALK half=backward region=1 arm=0 branch=6 members=3 ports=0 nested=0 outside=2
WALK half=backward region=2 arm=1 branch=6 members=3 ports=0 nested=0 outside=2
REINTERP split function=jvp_of_leaky skip=branch exit=Join condition=5 why=this graph's exit is the Join of a branch (the arm the condition picks writes the pair), so the cut has to happen inside the arms, with the condition carried as a residual -- the backward half then picks the arm the forward half took by reading the same condition -- and a half's walk enters an arm's region now but does not build the arm it finds
```

逐项读：

| 读数 | 是什么 | 为什么是这几项 |
|---|---|---|
| `region=1` / `arm=0` | 哪一块、哪条臂 | 区域 id 与 `GCRegion.index`，从表里读，不猜 |
| `branch=6` | 拥有这条臂的 `Brch` | `GCRegion.owner`，与命名 Join 的那个 `Brch` 一致 |
| `members=3` | 区域里几个节点 | 走查真的进了区域（`nodeRegion(ref) == region`） |
| `ports=0` | 几条 form port | 这两条臂从父区域直接读，不走 form port —— 所以"进得去"不等于"接得上" |
| `nested=0` | 区域里还有几个分支 | 嵌套分支要递归，先记数 |
| `outside=2` | 区域**从外面读**几个值（去重、不含常量） | 这就是切一刀要**带过去**的集合：`x` 与**条件**。作者 4.2 的那半句正好落在其中 |

改前（原文，`skip=exit`）：`REINTERP split function=jvp_of_leaky skip=exit (a lifted function answers with the pair (result, tangent); this graph's exit is not a two-member tuple fill, so there is no pair to cut)`

## 3. A/B：入口关掉再打开

`enterArmRegions`（`modules/transforms/split.cpp`）就是这个入口。关掉 → 只剩 `skip=branch` 那一行
（下面贴原文）；打开 → 两行 `WALK` 出现，其余 8 条 `split` 入口和别的计划都不动。

关掉时（原文）：

```
REINTERP split function=jvp_of_leaky skip=branch exit=Join condition=5 why=this graph's exit is the Join of a branch (the arm the condition picks writes the pair), so the cut has to happen inside the arms, with the condition carried as a residual -- the backward half then picks the arm the forward half took by reading the same condition -- and a half's walk enters an arm's region now but does not build the arm it finds
```

打开时：上面第 2 节的两行 `WALK` 在它前面。

## 4. 下一步（写清，免得下一个人以为"已经做完了"）

区域走查现在是**能进、能说**；还差的是**进了之后建**：每条臂在各自半边里建自己的 Join
（值半边落 `(float)`、切半边落 `(float)`），条件按 4.2 作为残差带过去，后半边按同一个条件选臂。
这一笔要等父级点名，不和现在这笔一起做。

## 5. 三条不许动的数（每次复验）

- `modules.remat`：`residuals=3` 与 `residuals=2 recompute=1` 两条读数；
- `split_two_params`：`(12.0, 9.0)`；
- `compositions.vjp_single`：`vjp_single y=27.0 dfdx=27.0`（`vjp` 是 `transpose . split . reinterp<jvp>`，
  所以这一刀就是它那一刀）。