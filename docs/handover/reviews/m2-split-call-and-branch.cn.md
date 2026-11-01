# M2 的 `split`：穿过调用（有数）与穿过分支（有读数）

写这一页的原因只有一个：**这两件事很容易被下一个人混成一件**。`grad` 走分支今天是通的、有数；
`split(jvp(...))` 直接切一个带分支的函数今天不通、只有读数。说清哪一条是哪一条，比再修一次更值。

## 0. 一句话的分界

- **能跑的，钉数**：`control_flow_grad`（`feat.modules.autodiff`）穿分支、两条臂读不同的值，
  12 条入口全绿，读数是 `control_flow_grad a=6.0 b=0.5 c=80.0 d=12.0 e=0.0 w=-10.0`。
  其中 `a=6.0` 与 `b=0.5` 正是 `if x > 0.0 then x * x else 0.5 * x` 两条臂的导数。
- **不能跑的，钉读数**：`split(jvp(leaky))`（`split_branch_arms`）今天拒绝建图，
  `expect_exit = 1`，钉的是拒绝原文与 `WALK`/`ARM` 读数。**拒绝也是读数**，不是"没有结果"。

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

## 2. 穿过分支：进得去、说得出（有读数）

`leaky(x) = if x > 0.0 then x*x else 0.5*x`，`split(jvp(leaky))` 的源图走查：

```
WALK half=backward region=1 arm=0 branch=6 members=3 ports=0 nested=0 outside=2
WALK half=backward region=2 arm=1 branch=6 members=3 ports=0 nested=0 outside=2
```

| 读数 | 是什么 | 为什么是这几项 |
|---|---|---|
| `region=1` / `arm=0` | 哪一块、哪条臂 | 区域 id 与 `GCRegion.index`，从表里读，不猜 |
| `branch=6` | 拥有这条臂的 `Brch` | `GCRegion.owner`，与命名出口 Join 的那个 `Brch` 一致 |
| `members=3` | 区域里几个节点 | 走查真的进了区域（`nodeRegion(ref) == region`） |
| `ports=0` | 几条 form port | 两条臂从父区域直接读，不走 form port —— "进得去"不等于"接得上" |
| `nested=0` | 区域里还有几个分支 | 嵌套分支要递归，先记数 |
| `outside=2` | 区域**从外面读**几个值（去重、不含常量） | **不是** `x` 与条件，是 `x` 与种子 `dx`（`p0`、`p1`，见 §3 的 `from=[p0 p1]`）。**我此前的说法错了，在此更正**：条件不在臂里读，它在父区域（§3 的 `conditionRegion=0`）——正因为不在臂里，它才是"两个半边都去父区域读的同一个值"（4.2 的残差） |

改前（原文，`skip=exit`）：`REINTERP split function=jvp_of_leaky skip=exit (a lifted function answers with the pair (result, tangent); this graph's exit is not a two-member tuple fill, so there is no pair to cut)`

## 3. 臂的 body 复制进来、臂的那一半接到 Join 上（有读数）

`WALK` 之后再读两层：**臂产出什么**，以及后一半**复制了什么、接了什么**。

```
ARM half=backward arm=0 region=1 yield=13 members=2 value=7 tangent=8 probeRegion=1 probeMembers=3 probeJoin=16 wired=1 inputs=2 from=[p0 p1 ] condition=5 conditionRegion=0 conditionCopied=5
ARM half=backward arm=1 region=2 yield=15 members=2 value=10 tangent=11 probeRegion=2 probeMembers=3 probeJoin=17 wired=1 inputs=2 from=[p0 p1 ] condition=5 conditionRegion=0 conditionCopied=5
```

| 读数 | 是什么 | 为什么这一项是这一步的证据 |
|---|---|---|
| `yield=13` / `yield=15` | 臂里那个 `Fill`：把 pair 写进 Join 槽的节点 | pair 两半就是它的两个 with 输入，所以 `value=7 tangent=8`（`x*x` 臂）、`value=10 tangent=11`（`0.5*x` 臂）是**读出来的**，不是按形状猜的 |
| `probeMembers=3` | 草稿里那条臂的区域**真有几个节点** | 3 = 源图那条臂的 3 个节点。`cloneRuntimeGraphIntoDraft` 连区域一起搬，成员是臂自己的，不是这个文件猜的集合 |
| `wired=1` | 后一半那条 Join **接上了**什么 | 它持有该臂 pair 的切向那一半（`tangent=8` / `11` 的副本）—— **这是一条边**；上一刀这一步是 0，因为什么都没接 |
| `inputs=2 from=[p0 p1]` | 臂里那些成员**从外面读**哪些值 | 参数 0（`x`）与参数 1（种子 `dx`），按源图的参数位命名，不按草稿的 ref 命名 |
| `condition=5 conditionRegion=0 conditionCopied=5` | 条件在哪 | **不在臂里**：在父区域（0），草稿里是 ref 5。这正是 4.2 说的"条件作为残差"——两个半边去父区域读同一个值 |
| `probeRegion=1` / `=2` | 副本落在哪个区域 | 由 `nodeRegion(clone)` 读出，与源图的 1/2 一一对应 |

**这一步接上了什么、没接上什么**（不夸大）：臂的 body 复制进了它自己的区域（`probeMembers=3`），臂的
那一半 pair 接到了后一半的 Join 上（`wired=1`）；**条件还没有被用来选臂**。所以 `skip=branch` 仍然拒绝。

## 4. A/B：把 body 复制关掉

`copyArmBodies`（`modules/transforms/split.cpp`）是这一步的入口。关掉 → 回到上一刀的四行（草稿里是空
区域、没接的 Join，原文）：

```
WALK half=backward region=1 arm=0 branch=6 members=3 ports=0 nested=0 outside=2
WALK half=backward region=2 arm=1 branch=6 members=3 ports=0 nested=0 outside=2
ARM half=backward arm=0 region=1 yield=13 members=2 value=7 tangent=8 probeRegion=1 probeJoin=0
ARM half=backward arm=1 region=2 yield=15 members=2 value=10 tangent=11 probeRegion=2 probeJoin=1
```

打开 → §2 两行 + §3 两行；`split.plan.toml` 9/9 绿。

## 5. A/B：入口关掉

`enterArmRegions` 关掉 → 只剩拒绝那一行（原文，读数是这一刀之后的措辞）：

```
REINTERP split function=jvp_of_leaky skip=branch exit=Join condition=5 why=this graph's exit is the Join of a branch (the arm the condition picks writes the pair), so the cut has to happen inside the arms, with the condition carried as a residual -- the backward half then picks the arm the forward half took by reading the same condition. Each arm is entered and described (`WALK`), its yield is read off the Fill that writes the pair (`ARM`), and the backward half copies the arm's body into its own region and joins that arm's half of the pair -- but the arm is not *chosen* yet: the condition picks it, and reading that condition to pick is the next cut, not this one
```

打开 → 上面四行读数在它前面。

## 6. 选臂完成后的验收数（**先定，后读**）

选臂那一刀做完之后，`split(jvp(leaky))` 必须能读出**每条臂各自**的偏导：

| 输入 | 走哪条臂 | 值 | **偏导（验收数）** |
|---|---|---|---|
| `x = 2.0` | `x > 0.0` 为真 → `x*x` | `4.0` | **`dfdx = 4.0`** |
| `x = -1.0` | 假 → `0.5*x` | `-0.5` | **`dfdx = 0.5`** |

这两个数是手算的，不是从今天的程序里抄的：`d/dx (x*x) = 2x`，在 `2.0` 上是 `4.0`；`d/dx (0.5*x) = 0.5`，
与 `x` 无关。**写在选臂之前**，是为了让那一步有验收线可对，而不是做完再挑一个数当结果。

## 7. 下一步（写清，免得下一个人以为"已经做完了"）

区域走查、body 复制、臂的一半接 Join 都已完成（§2、§3）；还差**选臂**：后一半按**同一个条件**
（父区域里的 `condition=5`）挑出前一半走过的那条臂，并把条件作为残差带过切口。做完之后 §6 的两个
验收数必须读得出来。这一笔等父级点名，不和现在这笔一起做。

## 8. 三条不许动的数（每次复验）

- `modules.remat`：`residuals=3` 与 `residuals=2 recompute=1` 两条读数；
- `split_two_params`：`(12.0, 9.0)`；
- `compositions.vjp_single`：`vjp_single y=27.0 dfdx=27.0`（`vjp` 是 `transpose . split . reinterp<jvp>`，
  所以这一刀就是它那一刀）。