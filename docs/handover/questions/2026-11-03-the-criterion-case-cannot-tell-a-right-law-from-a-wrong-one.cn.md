# 一条分辨不了对错的判据，能不能算判据？——`mean_axis_transpose` 的 6.0 既对也错

## 一句话矛盾（先读这一句，可一眼判真伪）

**`rules_angle_args.mean_axis_transpose` 断言的是 `transpose=6.0`，而 `6.0` 这个数由一个**
**直接返回余切 `ct` 的错律也能印出来：`ct` 自己就是 `(1, 2, 3)`，和为 6.0，正确的余切**
**`[[0.5, 1, 1.5], [0.5, 1, 1.5]]` 的和也是 6.0。所以这条用例转绿不构成"律对了"的证据；**
**按今晚一直用的次序（对 > 明确拒绝 > 错数 > 崩），一条分辨不了对错的绿比红更危险——
它会把一个错律钉成"已达成"。**

先把这条量出来再说别的：把 `mean_axis_t` 的右手边临时写成 `(ct: float[..]) => ct`（一个
**错**律：把余切原样还回去，既不摊开也不除长度），同时抬开 `transpose.cpp` 的个数闸，用例**
**照样印 `transpose=6.0`**。测完两处都已还原，`stdlib/rulesets/transpose.cml` 与
`modules/transforms/transpose.cpp` 与 `20678725b` 逐字相同。

## 问什么

`rules_angle_args.mean_axis_transpose`（`known_fail = ["nvm","fvm"]`，`known_fail_msg` =
`TRANSPOSE function=loss_mean skip=arity uri=tensor:mean_axis operands=3`）今天报的是个数闸。
抬开个数闸之后停在"端口没人填"：

```
TRANSPOSE function=loss_mean graph=296 type=(float[..], float) => (float[..],)
Runtime indirect call norm-arity mismatch: caller graph 'main' node 3 has 1 args,
  target graph 'transpose_of_loss_mean' has 2 norm ports
```

这与 `docs/handover/reviews/2026-11-03-who-carries-the-forward-halfs-intermediate-values.cn.md` 的
#2 是同一形状，本页复现了它。要作者裁的不是"怎么让它绿"（它今天就能被弄绿，见上一节），而是：

> **在判据分辨不了对错之前，这条用例要不要算"待达成的判据"？还是先把它改造成能分辨的形状？**

## 量到的三条（`lane-meanaxis`，HEAD `20678725b`，2026-11-03 22:00–22:25，主机 88zjwei）

### 一、`6.0` 分辨不了对错（这一夜的硬读数）

```
mean_axis(x, 0, false) 的余切，ct = 1.0 时：正确解 [[0.5,1,1.5],[0.5,1,1.5]]，和为 6.0
错律 (ct) => ct 的答案：                     [1, 2, 3]（= w*ct 自身），和为 6.0
```

用例只印 `sum(back.0)`，两者逐字相同。这不是"断言太松"的抱怨：它意味着**任何在这里转绿的实现
都无法被这条用例区分开**，而 `known_fail` 一旦删掉，错的实现就带着"已达成"的印记进 develop。

### 二、"把律写干净"关不掉那个口：绑定即捕获

评审页的默认 #1 是"律的右手边读了中间节点的前向值 → 登记一个残差端口"。我试着把因**去掉**：
把右手边写成 `(ct: float[..]) => ct`，一个不读 `xs` 的律。抬开个数闸后**仍然是 2 个 norm 口**
（`transpose_of_loss_mean` 2 个，调用点 1 个实参，同一条 norm-arity 报错）。

⇒ 多出来的那个口**不是"律读了值"造成的，而是"律的左手边绑定了 `xs`"这一绑定本身**被记成了捕获。
`transpose.cpp` 的 `capturesOf` 收的是"体里问过、而端口不给它的参数"，而不是"右手边用到的名字"。
所以"让律不读线性操作数"这条修法在本例里**关不掉端口**——关口的决定在**约定**（绑定即捕获，
还是只有真读到的才捕获），这是一条要作者裁的规则，不是 `mean_axis_t` 的写法问题。

### 三、"长度只能从已知操作数拿"（作者 11:10）在这条用例上无解

11:10 那条说：转置律只能读已知操作数；线性操作数的形状类型里没写时，必须由一个已知操作数提供。
这条用例里**没有任何已知操作数能提供长度**：

- `loss_mean(x: float[..])`，调用点是 `transpose(loss_mean)(1.0)`——**`x` 在整个程序里不存在**，
  它既不是实参、也不是 `with` 变量，转置图拿到的只有一个 `float`。
- `float[..]` 连字面量都造不出来：`let xs = ([[1.0, 2.0, 3.0], [4.0, 5.0, 6.0]] as float[..])`
  报 `Cannot insert data of type 'float[]' into a(n) Array with element type 'float'`；改用
  `with <xs: float[..]>` 再 `grad(loss_w)<xs>` 也没有更顺——`grad` 拒绝
  "it has no `with` parameter, and grad differentiates the first `with` parameter"（我把 `with`
  加上了，调用点又撞 `<float[..]> () => float[..]` 不能收 `<float[][]>`）。
- 已知的只有 `axis = 0` 与 `keepdims = false`：它们给的是**轴**和**摊开的规则**，不是长度。
- 余切 `ct` 只给长度 3（归约后的形状），`lanes = numel(xs)/numel(ct)` 需要的那一半不在。

所以正确解的**形状 `[2, 3]` 在转置图里无处可知**。这不是"律写得不够好"，是这条用例的程序形状
缺了长度这个值。

## 一条病、四处律（2026-11-03 22:35 量，探针 `D:\Temp\p2-shapes.txt`）

**同一条病，四处律，不是三种。** 逐条量到的是下面这张表；"读什么"一栏是律的右手边**为了什么**
去读线性操作数，"已知操作数能给吗"一栏问的是 11:10 那条路（长度/形状由已知操作数提供）能不能走。

| 律 | 读线性操作数的什么 | 位置（原文一行） | 已知操作数能给吗 | 缺的是一句话 |
| --- | --- | --- | --- | --- |
| `sum_t` | `xs` 的**整张形状** | `stdlib/rulesets/transpose.cml#spread_like`：`return full(shape(xs), value)` | 否 | 输入的整体形状 |
| `mean_axis_t` | `xs` 的**总长度 + 形状** | `#mean_axis_t`：`numel(xs) / numel(ct)`、`ones_like(xs)` | 否（`ct` 只给归约后的长度） | 总长度 |
| `take_t` | `t` 的**整张形状**（行×列） | `#take_t`：`scatter_rows(zeros_like(t), indices, ct, 0)` | **半**：`ct` 给列数，`indices` 给行号不给行数 | 行数 |
| `idx_arr_t` | `xs` 的**长度** | `#idx_arr_t`：`scatter_into(zeros_like(xs), i, ct)` | 否（`i` 是 `int`，给不了长度） | 长度 |
| `mul_tensor_t` | `x`/`w` 的**形状** | `#mul_tensor_t`：`sum_to(ct * w, x)`、`sum_to(ct * x, w)` | 常量侧 ✓（常量本就已知），线性侧 ✗ | 形状 |

**机制链条一句话**：律的右手边读线性操作数 → 走法的探针表
（`modules/transforms/transpose.cpp#TransposeState::calleeNeeds`）把那个操作数的**值**记成该成员的
一个捕获口 → `modules/transforms/transpose.cpp#transposeSignature` 从**正函数的签名**算、只数余切
→ 两张表对不上，调用时死在 `nargs.size() == normPorts.size()`。这不是"律写得不够好"，是**两条表
各算一份**。

**三个最小程序，同一句报文**（今天全部编不过）：

```
f(x: float[])    = x[0] * 2.0          →  transpose_of_f  has 2 norm ports
g(t: float[..])  = sum(take(t, [1]))   →  transpose_of_g  has 2 norm ports
h(x: float[..])  = sum(x)              →  transpose_of_h  has 2 norm ports

Runtime indirect call norm-arity mismatch: caller graph 'main' node 3 has 1 args,
  target graph 'transpose_of_X' has 2 norm ports
```

**没有一条编过，所以没有"转置后的读数"可贴——这本身就是读数**：今天这三条律没有一个能对
`transpose` 出数，能出数的只有走 `vjp`/`split` 的那条路（见下）。

**`11:10` 在 `take_t` 上无解，这一句要分开写，因为它把责任从律挪到了调用点。**
`take_t` 需要的行数**没有任何已知操作数能给**：`indices` 给的是被选中的行号，不是 `t` 有多少行；
`ct` 只给列数。所以只能走"线性调用多收一个已知实参（`len(t)` / 形状）"那条——而这条路**今天
没接**：`stdlib/rulesets/jvp.cml#jvp_idx_arr` 仍写 `index_into(dxs, i)`，`take` 的 jvp 侧同样没带
长度。⇒ **不是"律没写"，是"实参没接"。** 责任在调用点，不在律的写法。

**`sum_t` 也在病里，只是被遮住了。** 它今天"看起来正常"，是因为 `vjp`/`sum` 那条路走的是
`split`：捕获进的是 `split` 后一半的**闭包**（闭包端口本来就按捕获数算），不经过
`transposeSignature` 的端口数比对。所以**这条病不限于新写的律**，也不限于 `mean` 一族：
任何人直接对一个含 `sum` 的函数取 `transpose`，今天都撞同一句报文。

**所以这条裁决不能只落在 `mean_axis_t` 上**：五条律是同一个写法，按一条裁，按一条改，
否则下一个只修 `mean` 的人会把矛盾留在 `sum`/`take`/`idx_arr` 上。

## 三条路

- **A：残差按端口进转置图**（评审页默认 #1）。签名从 `(float) => (float[..],)` 变成
  `(float[..], float) => (float[..],)`，每个"直接对非线性函数取 `transpose`"的调用点跟着改；本用例
  必须先造 `x`、再 `transpose(loss_mean)(x, 1.0)`，断言改成逐元素。代价：改约定、改签名、改调用点；
  好处：一次解决 #1–#5（`mean`/`softmax`/`reshape`/两状态循环）。
- **B：只带长度**（11:10 的字面落地：`len(xs)` 一个 `int` 当已知实参）。签名变成
  `(int, float) => (float[..],)`，用例同样要先造 `x`（长度来自 `x`）。代价：规则层要新写"带长度的
  线性版本"（和 `idx_arr` 那条同一套，作者说"不新增核心机制"），且 `take`/`idx_arr`/`mean` 三处
  一起动；好处：带的值从"整个数组"降到"一个数"，签名改动面小。
- **C（默认建议，一条，按此往下做）：一条约定改四处，并先接上调用点那根线。** 约定就一句：

  > **转置律不得读线性操作数的形状/长度；律需要的信息由调用点作为"已知实参"传入。**

  顺序三步，一步一步各自可验：

  1. **先接线（不动律）**：把形状/长度作为已知实参接到线性调用上（`jvp_idx_arr`、`take` 的
     jvp 侧各一处，`mean_axis` 一处），照 11:10 写的"不新增核心机制"。这一步的判据是**端口的数
     变对**：`transpose_of_*` 的 norm 口数等于调用点实参数，报文从 norm-arity 消失（哪怕律还
     没改，这一步就能量出来）。
  2. **再改律**：`sum_t`/`mean_axis_t`/`take_t`/`idx_arr_t`/`mul_tensor_t` 五处，把"读操作数拿
     形状"换成"读那个已知实参"。判据是五条各自的最小程序**都能编过并出数**（`sum_t` 那条今天
     也编不过，正好当同一个判据用）。
  3. **最后拆 `mean` 的个数闸**：`modules/transforms/transpose.cpp` 的 `normOperands.size() != 2
     && != 1` 换成"按位置问每个操作数要不要余切"——`int`/`bool` 无切空间，天然答零。只有前两步
     落地之后这一步才安全，因为今天这道闸正是唯一挡住"错律也能绿"的东西（本页第一节量的那个
     6.0）。
  4. 用例侧（与实现并行、不依赖它）：先把 `mean_axis_transpose` 的断言改成**能分辨对错**的形状
     （先造 `x`、断言逐元素），再做上面的实现顺序。

## `11:10` 与"接线"这条路的反证

**这一节推翻上面「三条路」C 的第 1 步**（"先接线，不动律"）。原来以为第一步是"接线"——给转置律的
线性调用多接一个实参，让声明和图的口数对上；实测不是：**两个方向都堵死**，所以"多接一个实参"这条路
是死的。

### 反证读数（实测，原文照抄）

程序：`f(x: float[], i: int)`，`i` 无切空间。

`transpose(f)` 的**声明类型**是 `(int, float) => (float[],)`——`i` 与余切，两个位置；而它的**图有
3 个端口** `(x, i, ct)`。两个方向各试一次：

```
传 2 个实参：
Runtime indirect call norm-arity mismatch: caller graph 'main' node 4 has 2 args,
  target graph 'transpose_of_f' has 3 norm ports

传 3 个实参：
Function of type '(int, float) => (float[],)' cannot be called with argument types: (float[], int, float)
```

读法：实参 2 个时，图嫌少一个（`3` 个 norm 口）；实参 3 个时，**声明那一侧先按类型拒掉**——声明的
形参列表里根本没有 `x` 这个位置。**两边都堵死。** 所以"给 `idx_arr_t` / `take_t` 多接一个实参"这条路
是死的。

### 为什么是死的：不是接线层，是定义层

不是少接了一根线，是**两张表对"哪些形参进端口"的定义不同**：

- `stdlib/types.cml#Transpose` 的形参走 `ParamsOf<F>`，它**只留无切空间的形参**——所以声明里
  `x`（`float[]`，有切空间）不出现，`i`（`int`，无切空间）出现。
- `modules/transforms/transpose.cpp#transposeSignature` 的**捕获段**，把"**有**切空间、却被律读了值"
  的形参**又塞回端口列表**。

于是图里多出的那一个口，在声明里**没有对应的位置**。给再多实参，也只是让图多一个口；声明那一侧
永远少一格，短路发生在调用之前的类型检查上，不在调用点上。**失败点在两处定义，不在接线。**

### 正面读数（这一半有用）

**无切空间的形参在两边是一致的**：`i` 既在声明里、也在图里，端口数只差那个捕获（图 `3` 对声明
`2`，差的正是 `x`）。所以"让长度作为无切空间的形参进来"这个念头，在声明侧和建图侧都是自洽的。

唯一不碰那两条定义就能把长度送进去的接法，是让长度成为**线性半里的一个 `int` 形参**——它在两边都
会出现，不需要谁改捕获规则。**但那要求 `jvp` 的规则展开允许线性 lambda 收一个非切线实参，这是新
机制**，按作者 `11:10`「不新增核心机制」，它必须先裁。**所以这条写成"待裁"，不写成"计划"。**

### 一句自我更正

**原来以为第一步是"接线"，实测不是**——第一刀必须落在 `transposeSignature` 的捕获规则或
`Transpose<F>` 的声明上，那属于**约定**（谁进端口），不属于接线。这一句能省下一个人的半天：先接线
的话，接多少根都不动那个口。

## 未测出

- 没量过 A 或 B 落地后 `softmax` 那条能不能一起走通。它的墙**不是同一条**：
  `TRANSPOSE function=loss_sm skip=rule uri=tensor:softmax reason=unbound`——**根本没有转置律**，
  与 `skip=arity` 不同族；它要的是 `y = softmax(x)`（中间节点的前向值），是这一族里最难的一格，
  按作者"下一批"第 4 条排在后面。（作者 2026-11-03 22:30：先记下不追。）
- 没量过"绑定即捕获"改成"真读到才捕获"之后，`mean`/`take`/`idx_arr` 三处各自的端口数会变成什么，
  也没量过它对 `two_values_of_two_kinds` 的 `captures=2`（另一条病）有没有影响——那一行按令未动。
- 没量过"先造 `x` 再 `transpose(loss_mean)(x, 1.0)`"这个形状今天能不能编译（要等 A 或 B 落地，
  今天签名的口数不对），所以第 1 步只承诺"断言能分辨"，不承诺"今天就能跑出正确的 6.0"。