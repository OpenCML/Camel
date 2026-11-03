# 抬起的 `with` 值进了值列表：已按甲收窄，切不动的形状按名字拒

## 现在什么状态

`lane-jvpwith` 让被调函数的抬升把 `with` 实参**当值带切向**（`modules/transforms/jvp.cpp#liftSignature`：
能带的 `with` 值按声明顺序进普通值列表，它的切向进同序的切线列表）。这一半是对的，而且是本轮要的：
`vjp((ww) => weighted<ww>(2.0))(3.0)` 从 `value=9.0 grad=0.0`（**错数**）变成 `value=9.0 grad=(3.0)`，
引擎一侧一个字没动。

不能带的形状（聚合、数组、以及「被调函数自己没有值」的两级嵌套）**已经在 `jvp` 里按名字拒绝**，不再
落到 `split` 里崩。这一页记的是：两条门判据、它们现在的拒绝句、以及为什么剩下的路只有 `split` 能走。

## 两条门判据（都能从代码里读出来）

### 一、只有「切向是自己的标量」才提升（`jvp.cpp#withValueIsCarriable`）

被提升的 `with` 值会变成抬起图的一个**参数**，所以它必须是整条路都吃得下的形状：切向空间等于自身、
一个字宽（`Tangent<float> = float`）。聚合与数组不提升。**删了会怎样**（2026-11-04 实测）：

- `Point` 权重（`grad_first_with_parameter` 的 `pg`）：提升后抬升本身建得出来（`jvp` 单独跑
  `value=11.0 tangent=4.0` 是对的），`split` 切它时死在 `nodevm.cpp` 的 `idx < t->size()`
  （`Tuple index out of bounds`）——**崩**；
- `f64[..]` 权重（`wg`）：抬升建得出来，`split` 两步后按名字拒
  （`the port is not a parameter of the graph, so neither half takes it`）——拒绝离原因远了两步。

同一形状在**没有 `with`** 的普通 struct 参数上也崩（`split(jvp((qq: Point) => qq.a * 4.0 + qq.b))(p)`
在 `db741501d` 原样上就崩，同一条保证），所以这不是本 lane 造的，只是本 lane 让它多了一条到达的路。

### 二、`with` 值不被提升时，在被调的抬升里**拒绝**而不是写零（`jvp.cpp` 的 `with`-端口一步）

一个不是 `var` 的 `with` 值如果不被提升，它的切向就是该类型的零 —— 而调用方的链上**有**它的方向。
把这个方向换成零，正是本 lane 消掉的那个错数（`grad=0.0`），只是形状更宽。所以拒绝，句子：
「a `with` value of type `Point` … a callee's lift hands a direction on through its ordinary list, and
only a scalar can go there …」。`var` 的 `with` 参数例外：它是 body 会写的单元，本来就没有方向可丢。

### 三、两级嵌套里「被调函数自己没有值」拒绝（`jvp.cpp` 的 `Func` 分支）

一个函数如果**没有自己的普通参数**（读到的每个值都从 `with` 来），它的值那一半全是提升出来的
`with` 值。在**被调的抬升里**再遇到它，`split` 会把那个被调切第二遍，而切出来的半边指着一张嵌套程序
没带的图。**删了会怎样**：`nodevm.cpp` 的 `runtimeTarget != nullptr`
（`a direct call names a function whose graph is not in the program's graph table`）——**崩**。

为什么不是「被调函数有普通参数才提升」这条字面规则：`sk` 调的 `scaled` 同样没有普通参数
（两个值都在 `with` 里），而它在**顶层**抬升里被调，切一刀就够，答 `6.0` 是对的。字面规则会把这个
数拿走。坏的是**第二刀**，所以门判的是第二刀。

## 现在的读数（树 `jvpwith`，`db741501d` + 本 lane，2026-11-04 03:2x +08:00，主机 load 1m 17–29）

```
# weighted —— 基线 grad=(0.0)，现在对上
engine   grad=3.0 with_grad=(9.0, 3.0)
library  vjp value=9.0 grad=(3.0)
library  split value=9.0 grad=3.0

group gx   engine=3.0 library=3.0 value=9.0
sk engine=6.0
sk library=6.0 value=12.0
twoparam vjp   value=6.0 grad=2.0
nested split value=7.0 grad=2.0          <- 没有 with 的两级嵌套仍然通过
jvp int arg value=6.0 tangent=3.0

# 三处收窄后的拒绝（都有源位置，都不是崩）
transforms: jvp does not reinterpret a `with` value of type `Point` yet (...)
    --> jvpwith_pg.cml:9:7        with <p: Point>
transforms: jvp does not reinterpret a `with` value of type `float[..]` yet (...)
    --> jvpwith_wg.cml:8:7        with <w: f64[..]>
transforms: jvp does not reinterpret a call to `scaled`, which takes nothing but its `with` values yet (...)
    --> jvpwith_sx.cml:14:18      return scaled<k, x>()
```

## 为什么不选丙（一律不提升）

丙就是**退回错数**：`weighted` 会重新答 `grad=0.0`，而引擎答 3.0。按 **对 > 明确拒绝 > 错数 > 崩**，
丙比现在差两档，不可取。

## 剩下给 `split` 的（不在本 lane 的文件里）

1. **结构体形状的端口**：`modules/transforms/split.cpp#valuePortsOf` 拿前半、`#tangentArgumentsOf`
   拿后半，两侧都按端口走；一个 struct 端口的投影在切里没人翻译，落成
   `nodevm.cpp` 的 `idx < t->size()`（`Tuple index out of bounds`）。应有句子：这个端口不是任何一半
   能取的值形状 —— 与它现有的「不是图的参数」同一族，说清形状而不是只说位置。
2. **两级嵌套里没被采纳的图**：`split.cpp` 的 `calleeCuts`（每次 transform 调用一张表）与
   `nestedPrograms`（`for (const rt::Program *nested : nestedPrograms)` 才 adopt），一个被调在**另一
   次嵌套切**里被切时，外层指到的半边图没被带进来，落成 `nodevm.cpp` 的 `runtimeTarget != nullptr`。
3. **`()` 槽的算术**（本轮顺带发现，与 `with` 无关）：抬升签名给没有切向空间的值一个 `()` 槽，被调
   函数的后半被调用时实参比参数多一个 —— `Runtime indirect call norm-arity mismatch: caller
   'split_of_jvp_of___g_318___backward' node 3 has 2 args, target 'split_of_jvp_of_widen_backward' has
   1 norm ports`。`split.cpp#tangentArgumentsOf` 是算这组实参的地方。

这三处都不改，这是记录与交接，不是待裁的问题；本轮的选择（甲：`jvp` 按名字拒、`split` 待补）已经落地。