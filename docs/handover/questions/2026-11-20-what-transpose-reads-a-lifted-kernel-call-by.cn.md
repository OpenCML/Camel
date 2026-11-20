# `transpose` 转置一个提升过的法则核调用时，实参按什么读

**状态**：开放。这一页是 `lane-split-type-tangent` 上量到的一件事：`split` 那条「值半读了参数 0
的切向」的拒句**没有了**（`split` 现在从提升的类型读哪几段是切向），墙往后挪了一步，挪到了
`transpose`。挪过去之后有 17 条计划读数变红，而它们全是**同一个旧句子指的那一批**。

## 1. 读数一：割现在建得出来（宿主，Release，`std::nvm`，`--trace transforms`）

```
REINTERP split function=jvp_of_split_of_jvp_of_square__first_with_as_param_forward forward=420
        backward=421 type=(float) => ((float, (float, float) => (float, float) pure), (float) => float pure) pure
        residuals=1
```

同一条车道在改之前对同一张图打的是：

```
REINTERP split function=... skip=forward construct=a tangent port
        why=the value half reads the tangent of parameter 0, which the forward half does not take
```

也就是说：出口是**按分量提升**的（`Lift<(float, F')>` = 值对 + 提升过的函数），割现在把
「值成员的值腿 + 整个函数成员」交给前一半（`(float, (float,float)=>(float,float))`），把
「值成员的切向腿」交给后一半（`(float)=>float`）。值半不再走到 `autodiff::mul_tangent` 上去读
参数 1。

## 2. 读数二：墙现在在 `transpose`（同一张图，往下一步）

```
TRANSPOSE function=autodiff::add_tangent<float> graph=446 type=(float, float, float) => (float, float) nodes=5
TRANSPOSE function=autodiff::mul_tangent<float> graph=448 type=(float, float, float) => (float, float) nodes=7
TRANSPOSE function=jvp_of_autodiff::mul_tangent<float> graph=442
        type=(float, float, float, float, (float, float)) => (float, float, float, float) nodes=16
TRANSPOSE function=jvp_of_split_of_jvp_of_split_of_jvp_of_square__first_with_as_param_forward_backward
        skip=call-value-missing param=2 node=4 arg=0
error: transforms: transpose has no transpose for this function: the callee's transpose was built,
       but the parameter at position 3 reads a value this call does not carry.
```

`jvp_of_autodiff::mul_tangent<float>` 是**提升过的法则核**（`REINTERP jvp function=autodiff::mul_tangent<float>
graph=423 liftedFrom=375 type=(float, float, float, float, float, float, float, float) => (float, float)`）：
二阶的那一段本来就要调它（割的后一半抄的是源图出口的切向那一侧，而那一侧里就有它）。

## 3. 缺陷的名字

`transpose` 决定「这次调用的哪些实参是它能转置的参与位」有**两条**来路，这里是两条都不合用：

- 有 `GCNode::tangentArgs` 位图（法则展开时写的）就用位图（`transpose.cpp:5204-5231`）；
- 没有位图就把「有切空间的参数」全算成参与位（`transpose.cpp:832`、`886-887`：
  `participates.empty() ? (space.hasSpace() && space.space != nullptr) : participates[i]`）。

而 `jvp` 建出来的调用**不写位图**（这一点已在
`2026-11-20-the-tangent-argument-record-is-not-written-by-the-transform.cn.md` 里量过），
于是走第二条：一个 8 个 float 参数的提升核，**每个参数都有切空间** ⇒ 全被判成参与位 ⇒
「没有值要转发」⇒ 它去找参数 3 的实参而这次调用不带 ⇒ `call-value-missing`。

**这不是新错路，是全零位图和「全都有切空间」的巧合**：对一个提升过的核，第二条来路答的恰好是
错的那一半（把切向当成参与位，把值也当成参与位）。

## 4. 读数三：计划上这一挪的价钱

```
nested 10        : 3 pass / 3 fail / 4 known        （改之前 4 / 0 / 6）
split 30         : 30 pass / 0 fail / 0 known       （不动）
compile/transforms 216 : 203 / 0 / 13               （不动，两条点名用例仍 PASS）
feat/{compile,opt,transforms} 4357 : 4314 / 17 / 26
```

那 17 条红**全部**是旧句子指的那一批：`compositions.jvp_vjp_f` 2 条、`compositions.nested_grad_min`
4 条、`compositions.nested_grad_through_a_binding` 4 条、`compositions.hessian_of_a_quadratic` 4 条、
`nested.nest_square` / `nest_cube` 各 1 条、`nested.nest_quartic` 1 条（这一条是 `negative`，
钉的就是旧拒句）。没有一条是「本来是绿的、现在错了」。

## 5. 三条路，默认 B

- **A 把 17 条 `known_fail_msg` / `negative` 的句子重钉到新的 `transpose` 拒句。**
  代价：一行一条，17 处；读数立刻变绿，但什么都没修好，而且下一格修好 `transpose` 之后还要
  再钉回来一次。它唯一的价值是把读数与实现对齐。
- **B（默认）`transpose` 读参与位的方式换成和 `split` 同一条法则：从提升的类型读 `valuePortsOf`
  的那几个端口是值，其余有切空间的端口才是参与位。**
  理由：`transpose.cpp:832` 与 `split.cpp#valuePortsOf` 现在问的是同一件事（「这个端口的类型
  是不是值」），而 `split` 侧已经从类型答出来了；`transpose` 侧的代价要先量一次：`participates`
  空的时候它同时决定 `transposeSignature` / `calleeNeeds` 的形状（`transpose.cpp:794-900`），
  换读法要连那一侧一起对一遍——**一格左右，不保证一格内绿**，因为 `nested` 的方波是三层套
  （`split_of_jvp_of_split_of_jvp_of_square`）。
- **C 让 `jvp` 不提升一个法则核的调用**（`*_tangent` 本来就是线性那一半，它的提升只为了二阶）。
  代价：判断「一个调用是不是已经是线性半」没有类型判据（名字后缀不是类型），会退回按名字猜；
  而这正是本线一路拒绝的做法。**不建议。**

**先量什么再选 B**：`transpose` 在 `participates` 为空时，`transposeSignature` 里由
`answered` 决定的那个签名，对 `jvp_of_autodiff::mul_tangent<float>`（8 个 float 进、一对出）
应该答成什么——如果答案就是 `valuePortsOf` 的前 4 个端口，B 是一处读法替换；如果它还要
「被调方自己的割」参与，B 就是一格以上。

## 6. 这一轮被量错、被说错的地方（逐字留着）

- **「`nested` 的 `known` 从 6 掉下来，三条就绿了」**：前半对，后半错。`known` 确实 6→4，
  但三条 **fail** 了：它们的 `known_fail_msg` 钉的是旧拒句，而拒句换了句子。读数变绿需要的
  不是修好 `split`，是 `transpose` 那一格（或 A 的重钉）。
- **「割的后一半不碰二阶的核」**：错。后一半抄的是源图出口的**切向**那一侧，而二阶的切向里
  就写着 `jvp(mul_tangent)`；它不是被割出来的，是被**抄**过去的。