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

## 另一个方向（我认为更贵，所以没选）

**把条件从残差里拿掉**（例如后一半重新算一次比较，或把条件换成 `float`/`int`）。这样 `transpose` 不用改，
但：

- 重算比较 = 后一半按**另一个**值选臂，正是 4.2 明确不要的（"前一半按同一个值选臂"）；
- 换成 `float` 或 `int` 是改 4.2 写明的"条件作为残差"，而且作者给的形状读出来的数就未必还是 `3.0`。

## 还没解决的（另一问，不混进上面）

`vjp` 现在停在 `skip=arm arm=0`，原因不在余切空间，而在**臂的 body 长什么样**：

1. 一个 `split` 后一半的臂里放的是**提升后的调用** `jvp_mul`（`Func` 节点），而 `runArm` 只认带转置规则的
   `Oper`；
2. 这条臂的 `x` 是从**残差元组**里取的（`Accs`），而 `runArm` 只允许臂停在参数上。

也就是说"转置一个 `split` 后一半的臂"本身是一刀：要么把提升的调用看穿到它的原函数，要么在臂里转置
那样的调用；并且把"从残差里读到的值"当作余切无处可去的值（和上面同一条，往下一层）。

## 建议

维持今天的解释（不可微的残差槽贡献零余切）。下一刀做"转置 `split` 后一半的臂"，那一刀做完，作者形状
的 `vjp` 读数才会出现；在那之前 `split_branch_author_vjp` 一直钉着 `skip=arm arm=0`，不会被当成已完成。