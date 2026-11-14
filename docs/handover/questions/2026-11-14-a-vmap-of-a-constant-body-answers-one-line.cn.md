# 体的整体答案是一个常量时，`vmap` 该答几行？

## 问什么

`vmap` 把一个函数摊到一批行上：批是「行前面多一个长度未知的维度」（`Lift<T> = RowsOf<T>`，
`stdlib/rulesets/vmap.cml`）。体里每一条算子都交给规则集按行答，所以有算术的体答出来就是一批。
但体的**整体答案**如果是一个常量，今天的读法是「常量提到一行，这一行就是答案」，于是调用方的批
有多少行，答案就只有一行：

```
camel probe.cml std::nvm
REINTERP vmap function=k graph=.. type=(float[]) => float[] nodes=2 kinds=2/2 asked=0 found=0
[3.0]
```

`vmap((x: float) => 3.0)` 对 `float[4]` 答 `[3.0]`。**没有任何 pass 参与**，这不是 `std::opt`
造成的，也不是位置的顺序问题：体的两个节点是一个端口和一个常量，提升遍历没有可复制的操作数
（`asked=0`），整条链退化成「一张图答一次」。

`std::opt` 只是让它更容易撞上：`vmap_layout_ops.cml` 里的 `width(r) = dim<1>(r)`，形状已知时
被折成常量 `2`，于是同一行

| 树 | 命令 | 读数 |
| --- | --- | --- |
| `test/cases/compile/transforms/vmap_layout_ops.cml` | `camel vmap_layout_ops.cml std::nvm` | `[2, 2]` |
| 同一程序 | `camel vmap_layout_ops.cml std::opt std::nvm` | `[2]` |

两行是同一个程序（`planes: float[2, 2, 2]`，两行各宽 `2`）。差别不在 `std::opt` 改了意思：没有
pass 的 `vmap((x: float) => 3.0)` 一样只答一行，只是那个程序没人写过。

## 证据（原文照录）

`probe_vmap_const.cml`：

```camel
import rulesets.vmap
import { vmap } from transforms

func k(x: float): float {
    return 3.0
}

func main(): int sync {
    let xs: float[4] = [1.0, 2.0, 3.0, 4.0]
    println(vmap(k)(xs))
    return 0
}
```

| 树 | 命令 | 读数 |
| --- | --- | --- |
| `b72d59535`（2026-11-12 的二进制） | `camel probe_vmap_const.cml std::nvm` | `[3.0]`，**exit 0**（静默少三行） |
| `lane-idorder`（依赖序改完、拒绝之前） | 同一条 | `[3.0]`，**exit 0**（同上，与本页的序无关） |
| `lane-idorder`（本批，见「现状」） | 同一条 | `skip=body construct=an answer of one row`，**exit 1** |

对照组，同一个二进制：`vmap` 一个有算术的体（`x * 2.0 + x / 2.0 - 1.0`）答四行
`[1.5, 4.0, 6.5, 0.25]`，`nodes=8 kinds=8/8 asked=4 found=4`。所以不是「`vmap` 不认常量」——
常量作为算子的操作数是好的（`liftConstant` 把它提成一行，规则集的 `at` 在每个索引读那一行，
`rulesets/vmap.cml` 第 59–97 行）；只有它当**答案**时没人把它摊开。

为什么这不是 `vmap` 的写法问题（三点，都不是风格）：

1. 体的答案节点不是算子，规则集没有可问的头部：`asked=0`。规则集能广播，是因为每条律的右侧都
   在批上再算一次（`vmap_dim` 用 `range(0, shape(xs)[0], 1)` 自己铺那一列），而「答案就是常量」
   时没有那条右侧。
2. 变换自己写不出这一摊：批的前导维度在编译期是 `?`，摊多少行只有运行期知道，而变换只能造节点，
   不能读运行期的数。它今天读得到的唯一「一行」是 `liftConstant` 造的那个。
3. 语法上不该让作者总能改写：`width` 是库函数，调用方写的是 `vmap(width)`；要求「体必须用上参数」
   等于要求每个库函数为 `vmap` 写一个专门版本——那正是规则集存在的理由，不是它的替代。

## 三种修法（各一句）

- **甲：造一个能表达「同一个值 N 份」的节点。** 一个新算子（`spread`/`repeat` 之类，长度是运行期
  读到的批长），变换在答案是一行时常量时补一个；代价是新算子 + 各后端内核 + 定义图，
  `docs/design/op-semantics` 那一套都要走一遍。
- **乙：在 shape 层把标量扩成向量。** 不造新算子，让变换按规则集已有的写法铺：答案是一行时常量
  时，请规则集答「这一批的行数」并用它的铺法（`rulesets/vmap.cml#vmap_dim` 的
  `range(0, shape(xs)[0], 1)->map(.. => extent)` 就是现成的先例）；代价是 `vmap` 需要造一段
  region/闭包（`jvp` 已在造），而且铺法要么写进规则集成为一条律，要么在 `vmap.cpp` 里重写一遍
  ——后者是把同一件事写两处，与本仓库「律只有一份」的规矩冲突。
- **丙：要作者给出语义。** 也就是本页：`vmap(f)` 当 `f` 的答案与参数无关时，语义上是「一整批
  同一个值」，还是「一行」？如果裁定是「一行」，那调用方的批长与答案无关，库函数就得自己保证
  用途；如果裁定是「一整批」，就落到甲或乙。

## 现状（本批交付的形状）

- **已按默认建议开工：拒绝。** 答案经 `liftConstant` 提到一行、且没有任何算子/规则消费它时，
  `vmap` 现在按 `skip=body construct=an answer of one row` 拒绝，不再把一行的数组交给调用方：

  | 树 | 命令 | 读数 |
  | --- | --- | --- |
  | `lane-idorder`（本批） | `camel vmap_layout_ops.cml std::opt std::nvm` | 前八行照旧，第九行 `width` 处 `skip=body construct=an answer of one row`，**exit 1** |
  | 同上 | `camel probe_vmap_const.cml std::nvm` | `skip=body construct=an answer of one row`，**exit 1**（原先是 `[3.0]`、exit 0） |

  这不是新语义：9.9 的规矩是「静默的错值比一次拒绝更坏」，`vmap.cpp` 自己的端口注释引的也是这句。
  拒绝的理由是「本层没有能把一行摊成一整批的节点」，不是「这个程序写错了」。
- **真正答对 N 行没有做**：等裁定（甲或乙）。
- `test/plans/feat/compile/rulesets.plan.toml` 里**没有**给这条写差分对（差分会在正确答案出现前
  永远红），而是一条行为条目 `vmap_refuses_an_answer_of_one_row` 把今天的拒绝钉住：前八行照旧 +
  `skip=body construct=an answer of one row` + exit 1。裁定落地时这条跟着改。
- 撤下 `vmap_layout_ops` 差分对的指针写在该 plan 的条目注释里（`vmap_layout_ops` 之后），
  与本页互指。
- 已钉住的是这一条的另一半：`width` 在 `std::opt` 之后**不再被拒绝为「按 id 序读到 Dead 位」**
  （`kinds=2/2`，见 `lane-idorder` 的 `240c51c93`、`a6d8047aa`、`4a906d1a7`）。

## 默认建议（按它开工，不等回复）

**先按 9.9 把错值关掉：答案只有一行而调用方的批是一批时，`vmap` 拒绝，不答一行。**
理由：这不是新语义，是 design principles 9.9 已经写下的规矩——「一个静默的 `2x` 比一次拒绝
更坏」，而 `vmap.cpp` 自己在端口那条注释里说的也是这句话；旧读法把一个长度错的数组交给了调用方，
调用方接下来任何一次 `len`、`sum`、下标都对不上，故障已经离出错点很远了。

- **本批能开工的**：那条拒绝（答案经 `liftConstant` 提到一行、且没有被任何算子/规则消费时，
  `skip=body construct=an answer of one row`）。它不动任何现有用例的期望，也不动规则集。
- **本批不能开工、等裁定的**：真正答对 N 行（甲或乙）。乙看起来是最省的一条，但它要么给规则集
  加一条「铺」律、要么在 `vmap.cpp` 里重写 `vmap_dim` 的写法，两者都要作者先定「律归谁写」；
  甲要开新算子，属于 R3/算子表那一层。这两条都不该由车道自己发明。