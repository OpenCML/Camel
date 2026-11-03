# 那两条元组用例里没有闭包成员：19:55 那条裁决的前提，今天量不出来

## 问什么

作者 2026-11-03 19:55 的裁定说，「改成不收以后，两条元组用例静默变成 0」说明**有一个依赖线性输入的值被放进了闭包成员**，
并指定了做法：在那两条用例上把闭包成员逐个打出来，找到是谁把它放进去的，在那一个判断处修；修好后两条用例照旧答
`(3.0)` 与 `(1.0)/(3.0)`，同时 `wantsCotangent` 对闭包端口答 `false`。

本车道照这个做法打了一遍，量到的是**相反的事实**：两条用例**一个闭包成员都没有**，`residualsOf` 在它们身上根本没跑，
而把「闭包端口不收余切」落成判决在整份 `feat/compile/transforms` 上是**空操作**（改前改后 `105 = 98 过 / 2 红 / 5 known`，
两条用例逐位不变）。要裁的一句是：

> **这个 0 不在我们能取到的任何一棵树的这两条用例上。要不要接受「本车道不改语义，只留读数与逐位断言」，
> 并把「闭包成员不收余切」当作一条独立的小步另派（它落在本车道禁改的 `transpose.cpp`）？**

## 1. 读数（树 + 时刻 + 负载）

树 `D:\Projects\Camel-wt\closuredefect` @ `34b659107`（`origin/develop`，2026-11-04 04:04 `fetch` 取到），
主机 `88zjwei` 的常驻检出 `/home/zjwei/camel-work/closuredefect`；量的时刻工作站本地 2026-11-04 04:07–04:27，
主机 1 分钟负载 **16.2–25.6 / 32 核**（不是空载，所以墙钟不当基线引用）。基线（本树、未改动）：

| 目标 | [SUMMARY] |
| --- | --- |
| `test/plans/feat/compile/transforms.plan.toml` | total 105, pass 98, **fail 2**（`transpose_loop_escape.two_values_of_two_kinds`、`.second_state_of_the_same_loop`）, known 5 |
| `test/plans/feat/compile` | total 2243, pass 2230, **fail 2**（同上两条）, known 11 |
| `test/plans/feat/transforms` | total 29, pass 21, fail 0, known 8 |

## 2. 逐个清单：两条用例的闭包成员（0 个）与每个余切落点

用的探针（临时、已回退、**不入 git**，补丁在工作机 `D:\Temp\closureprobe.patch`）：`split.cpp#residualsOf` 每列一个残差
（节点、kind、类型、`parameter`、`linearHalf`，再沿它的输入列出背后的端口，用 `parameterOfPort` 判 `value`/`tangent`）；
`transpose.cpp#buildTransposeMember` 每列一个 `function->closureNodes()` 成员，并在 `addCotangent` 入口每落一个余切打一行
（目标节点、kind、类型、`normPorts` 下标、是否闭包成员、`wantsCotangent` 的答案）。

`transpose_tuple_both_slots.cml`（`both(x) = let t = (x, 2.0 * x); t.0 + t.1`）：

```
CLOSUREPROBE-BUILD function=both params=1 ports=1 partial=0 closureCount=0 closureType=none
CLOSUREPROBE-CT function=both target=7 kind=11(Oper)  from=0 normIndex=-1 closure=0 wants=1   ← 出口 `+`
CLOSUREPROBE-CT function=both target=5 kind=5 (Accs)  from=1 normIndex=-1 closure=0 wants=1   ← t.0 的读
CLOSUREPROBE-CT function=both target=6 kind=5 (Accs)  from=2 normIndex=-1 closure=0 wants=1   ← t.1 的读
CLOSUREPROBE-CT function=both target=0 kind=1 (Port)  from=1 normIndex=0  closure=0 wants=1   ← 元组第 0 格 → 参数 x
CLOSUREPROBE-CT function=both target=2 kind=11(Oper)  from=2 normIndex=-1 closure=0 wants=1   ← `2.0 * x`
CLOSUREPROBE-CT function=both target=0 kind=1 (Port)  from=4 normIndex=0  closure=0 wants=1   ← `2.0*x` 的律 → 参数 x
```

`transpose_no_tangent_tuple_member.cml`（`k(x) = let t = (x, 3); float(t.1) + t.0`）：

```
CLOSUREPROBE-BUILD function=k params=1 ports=1 partial=0 closureCount=0 closureType=none
CLOSUREPROBE-CT function=k target=6 kind=11(Oper) from=0 normIndex=-1 closure=0 wants=1     ← 出口 `+`
CLOSUREPROBE-CT function=k target=4 kind=11(Oper) from=1 normIndex=-1 closure=0 wants=1
CLOSUREPROBE-CT function=k target=5 kind=5 (Accs) from=2 normIndex=-1 closure=0 wants=1
TRANSPOSE function=k contribution=0 node=3 type=int space=none from=4                        ← 整数那一位，按类型归零
CLOSUREPROBE-CT function=k target=0 kind=1 (Port) from=2 normIndex=0  closure=0 wants=1      ← 参数 x
```

两件事直接读出来：

- **闭包成员 0 个、残差 0 个**（这两份探针一行 `RESIDUAL` 都没有，因为程序里没有 `split`；`closureCount=0`、
  `closureType=none`，所以 `transpose` 也不给它们建闭包端口）。一个不存在的成员不可能因为「不收余切」而变成 0。
- **答案就是从参数自己的端口出来的**：`both` 是 `1·ct`（元组第 0 格直通 `x`）+ `2·ct`（`2.0*x` 的 `mul_t` 律）= `3·ct`，
  所以 `back(1.0)=(3.0)`、`back(2.0)=(6.0)`；`k` 是 `1·ct`，整数那一位按类型归零并具名打印，所以 `(1.0)` 与 `(3.0)`。
  两个数都来自数学习导，不是从期望串反推的，也不是基线抄下来的：`both` 的 `3x`、`k` 的 `3+x`。

## 3. 全语料：259 个闭包成员，0 个依赖线性输入，0 个收到余切

在同一棵树上，把 `test/cases` 里所有提到 `transpose|jvp|vjp|grad|split` 的用例（**429 个**）逐个跑一遍探针：

| 量什么 | 数 |
| --- | --- |
| 被转置的图上打出的闭包成员（`CLOSUREPROBE-MEMBER`） | **259**（57 张图） |
| 其中依赖某个 `normPorts` 端口的（即依赖参数/切线的） | **0** |
| 落在闭包端口上的余切落点（`closure=1`） | **0** |
| `residualsOf` 列出的残差行 | **249** |
| 其中依赖**切线**端口（`role=tangent`）的 | **7**，全部在**循环图**上 |

那 7 条的用例是 `transforms/split/split_loop_value_projected`、`transforms/split/split_call_one`、
`compile/transforms/vjp_loop_rounds_walk_backwards`、`compile/transforms/transpose_loop_state_second`，形状同一个：
`jvp` 把循环的状态提升成 `(值, 切线)` 的**一对**，所以「循环的值」这个整体确实包含切线，`residualsOf` 就把它读成残差。
但它们**不会变成闭包成员**：`buildSplitGraph` 在 `ExitShape::Loop` 上先分派 `cutLoopGraph`（`split.cpp#buildSplitGraph`
里那段「循环是唯一不先读残差元组的格子」的注释），`cutLoopGraph` 自己算胶带与不变量的表，`residualsOf` 那份表被丢掉。
所以这 7 条是**读数，不是效果**；`transpose_loop_state_second` 那条更远一层——它今天的红是 `split` 拒绝
（「this loop is on the chain the exit reads …」），还没走到残差。

结论：作者那条规则（依赖切向量的值永远不能成为残差或闭包成员）在**非循环切法上今天已经成立**，而且是按结构成立的——
`residualsOf` 的走法把切点之后算出来的值（`later` 队列、`pairTangentReadOf` 的切线实参）只**穿过**不**携带**，
一个切线端口被走到时会换成它的**值**端口（`parameterOfPort(...).first`）。循环那一路是另一套。

## 4. 那个 0 从哪来

把「闭包端口不收余切」按页里的话落成判决（`wantsCotangent` 第一句：目标是 `function->closureNodes()` 的一员就答 `false`），
在同一棵树上量：**整份 `feat/compile/transforms` 逐条相同**（105/98/2/5，改前改后一致），两条用例逐位相同
（`3.0`、`(3.0)`、`(6.0)`；`5.0`、`(1.0)`、`(3.0)`）。这与 `2026-11-04-who-carries-the-operands-own-shape-into-the-transposition`
读数四（「落在闭包端口上的余切不收集」是空操作，命中仍 22）互相印证——那页的 22 与今天的 0 个 `closure=1` 命中是同一件事的两种口径。

所以页里那张表（`(3.0)→(0.0)`、`(1.0)/(3.0)→(0.0)/(0.0)`）**在今天能取到的树上复现不出来**。三种可能，读数各判一句：

1. **那个补丁不止「闭包端口不收」**：它多半同时切断了某个**非闭包**的余切（例如元组 `Fill` 的某一格，或 `Accs` 的某一槽），
   才会同时把这两条用例归零、又翻掉 14 条循环用例。页里说它「已回退，未入库」，补丁随那棵树没了，本车道无法重放。
2. **那棵树与今天不同**：页里的读数是 `lane-knownarg` 的树；从它到今天的 `34b659107` 之间，读「按位置取长度的规则」换成了
   `pick_at`，两条用例的成员里本来就没有闭包，更没有要闭包成员顶替的值。
3. **口径本身是错的**（页里读数二那句「在转置后的成员里 `x` 是闭包端口」）：今天 `both` 与 `k` 的 `closureCount=0`，
   `x` 是**参与微分的参数**，在转置后的图里连端口都没有（它是答案的位置，不是要交的值）。这一句按今天的读数不成立。

## 5. 三条路，各带代价

- **甲（默认）：不改语义，只留读数与逐位断言。** 两条用例的期望从「任意出现 `3.0`」收紧成答案元组本身
  （`(3.0)`、`(6.0)`、`(1.0)`、`(3.0)`），并把 `(0.0)` 写成**禁止出现**，所以「静默变成 0」以后会红、不会再绿着溜过去。
  代价：`wantsCotangent` 最后那句兜底 `return true` 仍在（没人写过它的判决），但今天没有任何读数说它是错的。
- **乙：现在就把「闭包端口不收余切」写进代码。** 代价：那一处在 `modules/transforms/transpose.cpp#buildTransposeMember`，
  是**另一条车道正在改的文件**（`forward` 的闭包端口转发），本车道按纪律不碰；而且量到它对本车道的两份目标都是空操作，
  落了既不会红也不会绿，只是一条写下来的规则——它该跟「闭包成员的余切有没有槽」那条裁决一起落，不该在这条车道上落。
- **丙：指出本车道量错了地方。** 给一个能看见 `(0.0)` 的**用例名**或**树名**（或那条补丁），本车道照它重打一遍；
  代价是再一轮构建与读数（约 10 分钟），但这是唯一能把那个 0 钉住的路。

## 取证位置

- 残差表与残差编号：`modules/transforms/split.cpp#residualsOf`；闭包端口：`split.cpp#addResidualCaptures`；循环分派：`split.cpp#buildSplitGraph`
- 闭包成员（被转置的图自己的）：`modules/transforms/transpose.cpp#buildTransposeMember`
- 答案按参数排（这里没有闭包成员的位置）：`transpose.cpp#buildTransposeMember` 里 `portParameter`/`portOfParameter` 那段
- 逐位断言：`test/plans/feat/compile/transforms.plan.toml` 的 `transpose_tuple_component.two_reads_of_one_tuple_add`
  与 `transpose_no_tangent.tuple_member`