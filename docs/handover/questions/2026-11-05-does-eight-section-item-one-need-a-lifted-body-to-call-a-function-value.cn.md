# 第八节第 1 条的后两句，是否以「库层能写出 `with_grad`/`has_aux`」为前提？

2026-11-05 凌晨，`lane-libgrad`（提交 `65db419bb`，父 `f08951def`）。`docs/handover/batches-2026-10-31-72h.cn.md`
第八节第 1 条按顺序写四件事：① 导数主线收尾；② 类型层拆包 `Gradient<F>`；③ 库层 `grad`/`with_grad`（含
`has_aux`）写成组合；④ **与引擎在全部 autodiff 用例上对齐后**，删 `pullback.cpp`/`buildGradientGraph`/
`GradResolver`；⑤ **S2 改用库层 `grad` 训练**。本轮只做 ③，量到的结果是：③ 的一半（`grad`）已经是组合，
另一半（`with_grad`、`has_aux`）**一个字符也写不出来**，而挡住它的不是组合能力。

## 1. 问句

④ 与 ⑤ 这两句，是否以 ③ 做完（库层能写出 `with_grad`/`has_aux`）为前提？

若是：③ 在「被微分的体能调用函数值」这条通道打开之前无法做完，那么 ④ 的「对齐之后」与 ⑤ 的「改用库层
`grad` 训练」在今天是**无法完成**的两句，而不是「下一轮的工作量」。若否（即可在 ③ 未完成时先删、先改），
请指出对齐判据是什么——本轮量到的每一种「库层写法」都在同一个构造上停下，因此找不到一个既算「对齐」
又不含这个构造的判据。

## 2. 让它是问句的读数（本轮实测，逐条抄，不概括）

**2.1 三类普查，53 个 autodiff 用例（`feat/transforms` 34 + `feat/linear` 19）：`31 / 22 / 0`。**

- **① 库层已答 31**：split 族 22（`split_forward_half` … `split_unread_member_with_no_zero`）+ floatgate 5
  （`f32_scalar_transpose` … `f32_branch_join`）+ `vjp_single`、`pullback_of_a_sum_of_squares`、
  `pullback_read_at_a_cotangent` + `split_identity_pipeline`。
- **② 库层写不出 22**：组合 4（`grad_vmap_f`、`vmap_grad_f`、`jvp_vjp_f`、`grad_vjp_g_zero`）+ linear 的引擎
  `grad` 10（`grad_refused`、`grad_through_loop`、`grad_array_loop`、`grad_array_index`、`grad_loop_collect`、
  `discrete_loop_state`、`discrete_loop_iterable`、`discrete_struct_field`、`discrete_effectful_value`、
  `refuse_loop_no_channel`）+ `var_reference` 的 `with_grad` 8（`var_array_append`、`var_tensor_local`、
  `var_tensor_read`、`var_tensor_read_after_call`、`var_tensor_read_after_write`、`var_tensor_read_in_callee`、
  `var_through_with_grad`、`grad_tuple_read`）。
- **③ 能写但答不同 0**：凡「数字可能不同」的地方都先撞 ② 的墙，一个数都没轮到最后比较。

**2.2 `has_aux` 四形态，各自撞什么（四条的原文）。** 四形态都要一个 body 把被变换的函数当值调用：

1. 嵌套 lambda 取外层参数包：`error[SemanticDiag::UnresolvedReference]: Unresolved reference: 'xs'`；
2. lambda 参数写 `<w, ...rest>` 拆第一个 `with` 参数：
   `LambdaParameterWithoutType: ... the expected type '<__pack_0_0: float> () => (float, float)' ends before the rest parameter`；
3. 把包 `let` 下来再拼：`error[SemanticDiag::RestArgumentNotTuple]: The rest argument '..t' is not a compile-time tuple`；
4. 具名函数让嵌套 lambda 读自己的包：`Camel Invariant Violation` at `src/compile/gir/builder_support.h#linkNodes`
   （`nodeGraphOf(from) == nodeGraphOf(to)`），进程当场终止。

**2.3 同一个构造在运行期原来是崩的，本笔改成了拒绝。** `jvp(use)(plain, 2.0, (), 1.0)` 作用在
`func use(f: (y: float) => float, z: float): float { return f(z) }` 上，曾在 `std::nvm`、`std::fvm`、`std::jit`
三个后端全部 `SIGSEGV`（`NodeVMSchedPass::call` → `rtdata::isViewValue`）；现在在节点处拒绝：
`REINTERP jvp function=use skip=callee construct=a call through a function value`，用例
`test/cases/compile/transforms/jvp_refuses_a_call_through_a_value.cml` 钉住 `cause` 与 `construct`。引擎对同一
形状从另一侧给的判决是 `cannot differentiate through this call of a function value: which function it calls is
not evident`（`modules/autodiff/pullback.cpp`）。

**2.4 最要紧的一条：callee 是名字时，同一组合与引擎逐元素相同。** 不是「近似」，是 `==`：

```
vjp((ww: float) => weighted<ww>(2.0))(3.0)        value=9.0  grad=3.0    引擎 3.0
vjp((ww: f64[]) => dot<ww>(x))(w)                 value=32.0 grad=[4.0, 5.0, 6.0]
vjp((pp: Point) => point_loss<pp>(4.0))(p)        value=11.0 grad={ a: 4.0, b: 1.0 }
```

（第一行由 `test/cases/compile/transforms/library_grad_of_a_with_weight.cml` 钉住，后两行是本轮在同一棵树上
量的一次读数，对应 `test/cases/modules/autodiff/grad_first_with_parameter.cml` 的四组。）
**这条读数是这一页的理由**：缺的不是「把算子拼起来」的能力——拼起来就是对的——缺的是**「把一个值当函数调」
之后还能被 lift** 这一条语法/类型通道；引擎的 `grad`/`with_grad` 今天走的是 `GradResolver` 那条递签名的路，
而库层要替代它，就必须有这条通道。

## 3. 默认（我方按此继续，不停工）

**保留 `pullback.cpp`/`buildGradientGraph`/`GradResolver` 三个文件，暂不删；S2 暂不改造；`has_aux` 四形态按
「不可写」记账，不导出只会拒绝的名字。**

理由两句：删与改都是「对齐之后」的动作，而「对齐」的前提今天实测为假（2.2、2.4）；而崩已经改成具名拒绝，
这是本轮已经拿到的真东西，它可以独立成立，不必等这条裁定。

**本轮按默认做到哪一步**：`stdlib/vjp.cml#grad` 保持组合（`vjp` + seed 1 + `.0`，体里没有 `GradResolver`、
`buildGradientGraph`、`pullback.cpp` 任何一个符号）；`vjp.cml#with_grad` 仍拒绝，但拒绝句与两处旧读数已改成本轮
实测的原因（「`with` 参数的切向是它类型的零」自 `lane-withtan`（2026-11-04）起为假，实测
`jvp((ww: float) => weighted<ww>(2.0))(3.0, 1.0)` 是 `value=9.0 tangent=3.0`）。删与改留给拿到这条裁定的一轮。