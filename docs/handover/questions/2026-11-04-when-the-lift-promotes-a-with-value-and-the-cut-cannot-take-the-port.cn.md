# 抬起的 `with` 值进了值列表，切不动的那三种形状归谁拒

## 问什么

`lane-jvpwith` 让被调函数的抬升把 `with` 实参**当值带切向**（`modules/transforms/jvp.cpp#liftSignature`：
非 `var` 的 `with` 值按声明顺序进普通值列表，它的切向进同序的切线列表）。这一半是对的：`jvp` 单独跑一个
struct 权重的 λ，切向就是对的（读数在下）。但库层是 `vjp = transpose(split(jvp(f)))`，它还要能**切**它，
而三种形状在 `split` 里停住或崩：

1. **struct 形状的端口** → 崩（`Tuple index out of bounds`）；
2. **张量（`f64[..]`）形状的端口** → 拒绝（`the port is not a parameter of the graph, so neither half takes it`）；
3. **两级嵌套、内层被调函数没有普通参数** → 崩（`a direct call names a function whose graph is not in the program's graph table`）。

**要作者裁的是**：这三种形状的拒绝（或修），归 `jvp`（本 lane 的文件里加一道它讲不真的门），还是归
`split`（本 lane 明令不动的文件，而那两句拒绝的原话已经在那里）？

## 实测依据（树 `jvpwith`，`db741501d` + 本 lane 改动，2026-11-04 03:0x +08:00，主机 load 1m 13–20）

`grad_first_with_parameter` 的四组逐组读数（每组单独一个程序，程序与输出都原样粘）：

```
# gx -- 基线 library=0.0（错数），现在对上
group gx   engine=3.0 library=3.0 value=9.0

# sk -- 基线拒绝 jvp has no forward transform，现在对上
sk engine=6.0
sk library=6.0 value=12.0

# sk 的另一半：两参数被调，基线拒绝，现在对上
twoparam vjp   value=6.0 grad=2.0
twoparam split value=6.0 grad=2.0

# wg -- 与基线同一条拒绝，原文不变
wg engine=[4.0, 5.0, 6.0]
transforms: split does not build this function (the port is not a parameter of the graph, so neither half takes it).

# pg -- 基线拒绝 jvp has no forward transform，现在崩
pg engine={ a: 4.0, b: 1.0 }
Camel Invariant Violation: ../src/passes/sched/nodevm/nodevm.cpp(548) Guarantee: idx < t->size()

# sx -- 基线拒绝 jvp has no forward transform，现在崩
sx engine=4.0
Camel Invariant Violation: ../src/passes/sched/nodevm/nodevm.cpp(639) Guarantee: runtimeTarget != nullptr
```

**两条「不是本 lane 建的」证据**，都是为了说明上面第 1、3 条不是新缺陷：

1. **struct 的崩在没有 `with` 的普通 struct 参数上就能复现**。`split(jvp((qq: Point) => qq.a * 4.0 + qq.b))(p)`
   在 `db741501d` 原样上崩（同一条 `Tuple index out of bounds`），而同一个 λ 的 `jvp` 单独跑给
   `jvp only value=11.0 tangent=4.0`——抬升答对了，是**切**崩。按 `file#Symbol` 说：`split.cpp#valuePortsOf`
   与后半的 `file#tangentArgumentsOf` 走的是端口，而 struct 的形状在切面里没人翻译。
2. **两级嵌套在基线上是通过的**。`split(jvp((x: float) => outer(inner(x))))(3.0)` 在 `db741501d` 上
   `nested split value=7.0 grad=2.0`、`nested vjp value=7.0 grad=2.0`（`inner`/`outer` 各 1 个普通参数）。
   `sx` 的差别只有一处：内层 `scaled` 的**普通参数为空**（`(k, x)` 两个都是 `with`，全被提升进值列表），
   于是切出来的两级里第二级的图没被采纳进程序表。

## 三条路

- **甲（默认）**：`jvp` 照建，`split` 按名字拒绝切不动的形状。**代价**：`split` 要加判据（struct 端口的
  拒绝、两级嵌套的图采纳），那是 `lane-lincall` 的文件；在它落地之前，`pg`/`sx` 从「拒绝」变成「崩」。
- **乙**：`jvp` 侧加门——聚合类型的 `with` 值不提升（退回写零）、以及「已在被调抬升里又遇到被调」时拒绝。
  **代价**：这两句话**都不真**：`jvp` 确实建得出（`value=11.0 tangent=4.0` 就是它建对了），门只能在注释里
  承认是替 `split` 挡；而且 `split` 修好后这道门必须删，删的人不会知道它为什么在那儿。
- **丙**：聚合/张量 `with` 一律不提升（= 退回本 lane 之前的零切向）。**代价**：这正是 `gx` 那条错数的来源，
  直接取消本 lane 的目标。

## 默认建议（施工方默认）

**走甲**，理由三条：

1. 严重序是「对 > 明确拒绝 > 错数 > 崩」。乙/丙都换不来「对」，而甲已经消掉了**错数**（`gx`、`sk`、两参数
   的那条都对了）；剩下的崩是既有的、静默之外的失败。
2. 判断「切不切得动」的唯一知情处是 `split`，拒绝的原话和位置也在那里；把判据写在 `jvp` 等于第二个
   意见。
3. `jvp` 侧的门会让「被调函数的抬升」这件事在下游修好之后仍然被挡住，而它挡不住任何真正的坏图。

**代价说清楚**：`pg`、`sx` 现在**崩**，基线是**拒**。不静默、不出错数，但比基线差一档。若作者要求
`lane-jvpwith` 先退回「拒」等 `split`（= 乙），最小门是：`liftSignature` 只提升「切向空间等于自身且
不是聚合/数组」的 `with` 值；`liftFuncValue` 在 `promoteWiths` 已为真时拒绝（那正好就是 `sx` 的两级）。

## 判据 / 哨兵（本次读数，主机 load 1m 13–20，不安静）

- `feat/compile` **2238 = 2235 / 2 / 1**，与 `db741501d` 逐条相同（两条 `transpose_loop_escape`）。
- `feat/compile/transforms` 基线 **105 = 102 / 2 / 1**；本 lane 加了一条新用例后 **106 = 103 / 2 / 1**，
  红的两条不变。
- `feat/transforms/split` **19/19**。
- 新用例 `test/cases/compile/transforms/library_grad_of_a_with_weight.cml` 在基线上**红**（`grad=0.0` +
  第二行拒绝），本 lane 两行都对——两条断言都是先测出红才留下的。