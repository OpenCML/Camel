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

## 7. §5 那个「先量什么」的答案：被调方那一侧已经对了，缺的是**外层提升图**的端口

`compositions.jvp_vjp_f` 现在停在（`std::nvm`，`--trace transforms`，逐字）：

```
TRANSPOSE function=jvp_of_split_of_jvp_of_sq_backward skip=call-value-missing param=2 node=4 arg=0
error[RuntimeDiag::RuntimeError]: An error has occurred: transforms: transpose has no transpose for
this function: the callee's transpose was built, but the parameter at position 3 reads a value this
call does not carry.
```

把 `transpose` 正在转置的那张图和那个调用的节点打出来（节点号、种类、类型、普通实参、切向位图）：

```
jvp_of_split_of_jvp_of_sq_backward graph=381 nodes=9 exit=8 type=(f32, f32) => (f32, f32)
  n0 kind=Port f32   n1 kind=Port f32   n2 kind=Port f32   n3 kind=Data f32
  n4 kind=Func (f32, f32) tan=0xF0 norm=[2 2 0 0 3 3 1 1]
  n5/n6 kind=Accs f32 of n4     n8 kind=Fill (f32, f32) with=[5 6]
```

⇒ 四件事，一件一件说：

1. **位图现在是有的**（`tan=0xF0`：第 4..7 个实参是切向），所以 §3 那条「两条来路都不合用」对
   这个调用**不再成立**：`transpose` 读位图，只向调用要前四个**值**实参。
2. **被调方的提升成员是对的**：`TRANSPOSE function=jvp_of_autodiff::mul_tangent<f32> type=(f32, f32,
   f32, f32, (f32, f32)) => (f32, f32, f32, f32)` —— 四个值 + 一个余切，正是 §5 要量到的那个答案。
   所以 B 在**被调方**这一侧不需要再改。
3. **挡住的是外层那张图自己的端口**：`param=2` 取到的实参是 `n0`，它是图 381 的**参数 0**（`dx`，
   另一半是它的切向）——被调方要它当值用。而 `transpose` 给图 381 建的成员**一个值端口都没有**：
   `participates` 空 ⇒ 凡有切空间的参数都算「答了」（`buildTransposeMember` 的
   `participatesOf`，签名由 `transposeSignature` 写），而「参与位不占端口」（同一函数里
   `portOfParam` 的填充：`if (participatesOf(i)) continue;`）⇒ `portOfParam[0]` 是无效 ⇒
   `forward(n0)` 空 ⇒ `call-value-missing`。
4. ⇒ **缺的不是「哪个实参是切向」这条记录，是「一个自己也提升了签名里，哪些端口是值」**：
   `(f32, f32) => (f32, f32)` 按 `split.cpp#valuePortsOf` 读出来值端口是 `{0}`，切向端口是 `{1}`；
   若外层成员的 `participates` 也这样读（值端口占端口、切向端口答余切），`dx` 就有端口可转发。

⇒ 于是 B 的形状要再收一句：**不是只在被调方的成员上读 `valuePortsOf`，而是对「签名本身是一个
`Lift`」的图，转置它自己时也按 `valuePortsOf` 划参与位**；只改被调方那一侧，这张图的 `dx` 仍然
没有端口。这一条按红线（`transpose.cpp` 不由本车道改）留给下一格裁定。读数不动：`feat/transforms`
的 `split`+`nested`+`compositions` 三个计划 `66 / 39 pass / 0 fail / 27 known`，
`smoke`+`feat/compile`+`feat/opt` `4296 / 4291 pass / 0 fail / 5 known`。

## 决策者答（2026-11-23，据现行设计原则与核心抽象复核）

共同依据：原则 9.9（负责人 2026-11-22）"`jvp(f)` 的类型 `Lift<F>`……按类型解读提升后的参数表没有歧义……所以图上不记'这张图由哪次提升而来'"，以及核心抽象第 1、5、6 节。今天的 C++ `jvp`/`split`/`transpose` 在变换骨架一步重做。 决策者 11-21 07:25 已撤回"给 `Transpose<F>` 加一格"，改为"`lin` 只在切向上线性，提升签名的值半由 cut 作为残差捕获"（JAX `linearize` 的形状）；负责人 11-22 写进原则：残差是闭包的捕获（核心抽象第 5 节），`liftedFrom` 删除（9.9）。于是 `transpose` 不需要知道"是否提升"，`ports == promised` 的端口数校验作为骨架的不变式保留。
