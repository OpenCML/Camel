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

## 同一族：`take_t` 与 `idx_arr_t` 也读线性操作数

`stdlib/rulesets/transpose.cml:258`（`idx_arr_t`）的 `scatter_into(zeros_like(xs), …)` 与
`:283`（`take_t`）的 `scatter_rows(zeros_like(t), …)` 都读**线性操作数自己的值**，只为拿形状。
11:10 裁的就是这一族（"`idx_arr_t` 写的是 `scatter_into(zeros_like(xs), i, ct)`，`zeros_like(xs)`
读的是线性操作数自己的值，只为拿它的长度……所以这条规则写法本身不成立"）。
**所以这条裁决不能只落在 `mean_axis_t` 上**：三条律是同一个写法，按一条裁，按一条改，
否则下一个只修 `mean` 的人会把矛盾留在 `take`/`idx_arr` 上。

## 三条路

- **A：残差按端口进转置图**（评审页默认 #1）。签名从 `(float) => (float[..],)` 变成
  `(float[..], float) => (float[..],)`，每个"直接对非线性函数取 `transpose`"的调用点跟着改；本用例
  必须先造 `x`、再 `transpose(loss_mean)(x, 1.0)`，断言改成逐元素。代价：改约定、改签名、改调用点；
  好处：一次解决 #1–#5（`mean`/`softmax`/`reshape`/两状态循环）。
- **B：只带长度**（11:10 的字面落地：`len(xs)` 一个 `int` 当已知实参）。签名变成
  `(int, float) => (float[..],)`，用例同样要先造 `x`（长度来自 `x`）。代价：规则层要新写"带长度的
  线性版本"（和 `idx_arr` 那条同一套，作者说"不新增核心机制"），且 `take`/`idx_arr`/`mean` 三处
  一起动；好处：带的值从"整个数组"降到"一个数"，签名改动面小。
- **C（默认建议）：守站，并先把用例改造成"能分辨对错"的形状，再谈 A 还是 B。** 具体两步：
  1. 用例侧：先造出 `x`，断言改成**逐元素**（打印数组本身，或断言 `sum` 加一个元素值），让"正确解
     `[[0.5,1,1.5],[0.5,1,1.5]]`"与"错律 `[1,2,3]`"**印出不同的字**。这一条不需要任何实现路先定。
  2. 实现侧：`known_fail` 保留、`known_fail_msg` 不动，`known_fail_note` 加两句——本页的
     "6.0 分辨不了对错"与"绑定即捕获"，并指向本页。理由：**在分辨不了对错的判据上选实现路，
     等于给未来的错数盖章**；而 A、B 都要动用例的调用形状、`take`/`idx_arr` 也一起动，先有能分辨
     的判据，才知道改完是不是真的对了。

## 未测出

- 没量过 A 或 B 落地后 `softmax` 那条能不能一起走通。它的墙**不是同一条**：
  `TRANSPOSE function=loss_sm skip=rule uri=tensor:softmax reason=unbound`——**根本没有转置律**，
  与 `skip=arity` 不同族；它要的是 `y = softmax(x)`（中间节点的前向值），是这一族里最难的一格，
  按作者"下一批"第 4 条排在后面。（作者 2026-11-03 22:30：先记下不追。）
- 没量过"绑定即捕获"改成"真读到才捕获"之后，`mean`/`take`/`idx_arr` 三处各自的端口数会变成什么，
  也没量过它对 `two_values_of_two_kinds` 的 `captures=2`（另一条病）有没有影响——那一行按令未动。
- 没量过"先造 `x` 再 `transpose(loss_mean)(x, 1.0)`"这个形状今天能不能编译（要等 A 或 B 落地，
  今天签名的口数不对），所以第 1 步只承诺"断言能分辨"，不承诺"今天就能跑出正确的 6.0"。