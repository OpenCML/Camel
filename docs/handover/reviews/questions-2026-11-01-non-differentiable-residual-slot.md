# 不可微的残差槽（`bool` 条件）在转置里贡献零余切 —— 这样解释对吗？

- 日期：2026-11-01
- 分支：`lane-remat`（h-remat）
- 相关：`docs/handover/reviews/m2-split-call-and-branch.cn.md` §6.1；`modules/transforms/transpose.cpp`；
  `modules/transforms/split.cpp`
- 一句话：作者 4.2 说"条件作为残差穿过切口"，而条件不是浮点数；转置那边对"一个不可微的残差槽"该怎么算，
  今天按**余切为零**落地，请你判。

## 事实（都是读数，不是推测）

1. 分支的切一刀已经通了：`split(jvp(leaky))` 在 `x = 2.0` / `x = -1.0` 分别读出 `dfdx = 4.0` / `0.5`；
   作者自己的形状 `f(x) = if x > 0 then x*x else 3*x` 在 `x = 2.0` / `x = -2.0` 读出 `4.0` / `3.0`
   （`split_branch_author.cml`，nvm 与 fvm 各一遍）。
2. 残差元组是 `(x, 条件)`，类型 `(float, bool)`；后一半因此是 `((float, bool), float) => float`。
3. 作者给的验收路径是 `vjp`，而 `vjp = transpose . split . reinterp<jvp>`，所以 `transpose` 要接住这个
   后一半。它先停在 `TRANSPOSE function=split_of_jvp_of_f_backward skip=signature`：`transposeSignature`
   要求残差元组每个成员都是 `float`。
4. 按"不可微的槽贡献零余切"改掉之后（见下"今天的解释"），这一条不再拒绝，`vjp` 走得更远，停在
   `TRANSPOSE function=split_of_jvp_of_f_backward skip=arm arm=0`——见"还没解决的"。

## 今天的解释（已落地，等你判）

**残差槽的余切空间可以是空的。** 谓词没有切空间：没有人对 `x > 0.0` 求导，它只是**选哪条臂**，不参与
算术。所以：

- 签名接受非 `float` 的残差成员；
- 余切在**余切空间那一层**统一丢：`addCotangent` 对"类型不是 `float` 的目标"直接不接（不造一个 `bool`
  的 `zero()`——那甚至不是该类型的值），因此它永远不会进算术；
- 输出仍然只给"每个切向参数一条余切"，残差槽不在输出里。

这个解释不改 4.2、不改残差元组的类型、不改 `split` 的产物，只给转置补一条本来就该有的规则。

**这一处的回退面只有两行**：`transposeSignature` 少一个"残差成员必须是 `float`"的判断，`addCotangent`
多一个"类型不是 `float` 就直接不接"的判断。你若裁反方向（残差槽不该被丢），退回原样只需把这两行
还原，别处不用动。

## 还没解决的（另一问，不混进上面）

`vjp` 现在停在 `TRANSPOSE function=split_of_jvp_of_f_backward arm-stop region=1 node=7
kernel=mul_tangent values=2 primal=:op/mul_d`。走到这里的路上，臂这一层已经补了三件事（都有读数）：

1. 臂从**残差元组**读到的值，余切交回它被读出的那一槽（`residual-return slot=0 region=1`），不在臂里编零；
2. 臂里的调用会转置（callee 进同一张成员表），**提升的调用**看穿到它提升自的那张图——提升的切向对
   切向是线性的，所以切向 i 的余切 = 原函数对值 i 的余切，只用值算；
3. 剩下的这条臂里放的不是用户函数的提升，而是**规则核** `autodiff::mul_tangent<float>`，参数
   `(x, y, dx, dy)`。核的切向那一半现在已经**声明**出来了（`modules/transforms/jvp.h` 里一张表：
   每个核的 value arity 与它算的是哪个算子的导数，`transpose` 读这张表，不靠 `*_tangent` 命名去猜）。

还缺的是**形状**：`transpose` 的成员都是按 callee 的**整个签名**造的，所以调用点仍要传切向的值，而切向
在转置图里没有值。核的转置是**线性映射的转置**（值 + 余切进，每个切向一条余切出），树里还没有造这种
成员的地方。这是下一刀，不是含糊的数。

**`transpose` 现在知道"核的切向那一半"的唯一来源就是那张表**。你若裁"该由签名自动推出"，回退面是一处
读字段（`tangentKernelOf` 的调用点）加上表本身，别处不用动。