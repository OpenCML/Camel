# 那两条 vector 律要的是**形状**，而声明没有写它

**这页为什么存在**：`0.2.44` 全树只剩两条红（`matmul_t.vector_lhs_law.nvm`、`matmul_t.vector_rhs_law.nvm`），
`docs/handover/reports/2026-11-04-b3-reverted-and-the-ten-it-does-not-explain.cn.md` §4 把它们记成「既有欠账」，
`fail = 2` 一直是它们的名字。它们**不是**律写错了，也不是 B3 的回归：律要的那个东西是一**个形状**，
形状本来可以从类型答，而两条用例的参数类型把长度留成了开放的。下面是量出来的全链，和改法。

## 一、先看句子与位置

实跑（宿主 `88zjwei`，`-Plan feat/showcase_s2`，tip `ca464bca2`）：

```
TRANSPOSE function=split_of_jvp_of_sum_of_product_backward skip=cotangent-needs-a-value
  uri=tensor:matmul position=0 reason=the rule's right side reads a value the caller does not carry
TRANSPOSE function=split_of_jvp_of_sum_of_product_backward skip=cotangent-needs-a-value
  uri=tensor:matmul position=1 reason=the rule's right side reads a value the caller does not carry
```

`position` 是**节点端口的下标**，不是调用实参的序号（`modules/transforms/transpose.cpp#ruleCotangent` 的
`size_t position`，由 `inputs` 的下标给出）：`matmul(a, b)` 的 `position=0` 是 `a`，`position=1` 是 `b`。
于是 `vector_lhs` 停在 `a`（`v: float[]`），`vector_rhs` 停在 `b`（`u: float[]`）——两条都是**rank-1 的那个操作数**，
不是「左边一条右边一条」。（`position=1` 那条此前被记成 `position=0`，是名单里的误记，本页按实测写。）

## 二、那个「值」是形状

- **谁在读它**：`stdlib/rulesets/transpose.cml#matmul_t` 的两个余切助手把答案读到**已知操作数**上——
  `#ct_of_a` 的 `sum_to(matmul(ct, transpose(b)), a)`、`#ct_of_b` 的 `sum_to(matmul(transpose(a), ct), b)`。
  `sum_to` 的第二个操作数**只被读形状**（`stdlib/tensor.cml#sum_to`：「`t` read at the shape `like` states」）。
  所以读的是 `a`（或 `b`）的 **extents**，不是它的元素。
- **谁该答它**：`modules/transforms/transpose.cpp#shapeFromTypeOf`——把 `extent_of` 的答案（`tensor:zeros` 的
  一个 `int[]`）写进图里，要求类型**陈述** extents（`include/camel/core/type/composite/array.h#statedExtentsOf`：
  `float[]` == `float[?]`，陈述「一个维度、没有长度」，所以它 `nullopt`）。规则层另有一处同名的判定
  `modules/transforms/rule_expand.cpp#answerTheShapeOf`，但它只答「每个读者都是 `tensor:shape`」的 capture；
  `sum_to` 的那个读者不满足，所以这条形状由 **walk 那一侧**先答，答不出来才轮到「要一个值」。
- **转置腿为什么拿不到**：`modules/transforms/transpose.cpp#transposeSignature` 交给转置后函数的只有 cotangent，
  被求导的那个参数**没有端口**（`#knownValue` 走到 `portOfParam` 那一支就是空的）。所以参与求导的参数的形状
  只能来自**声明**——这正是作者 2026-11-03 16:15 的那条语言规则，`TransposeRefusalCause` 里为它留着
  `ParameterShape`（`modules/transforms/transpose.h#TransposeRefusalCause`），句子是「把 extents 写进声明」。

## 三、两条红的原因：声明把长度留空了

两条用例原先写的是 `func sum_of_product(v: float[], w: float[2, 2])` / `(x: float[2, 2], u: float[])`。
`float[]` 陈述 rank、不陈述长度，于是**形状在程序里哪都没有**：图里写不下（`shapeFromTypeOf` 交白卷），
调用者也没有（那个参数没有端口），walk 只能按「律要一个值」拒绝。**同一个类，另一个入口给的却是另一句话**：
直接 `transpose(f)` 在 `float[...]` 上由端口数核对给出
`RuntimeDiag::TransposeNeedsParameterShape`（pin 在 `test/plans/feat/compile/rules_angle_args.plan.toml` 的
`transpose_needs_parameter_shape`，「Write the extents the parameter's value has in its declaration」）；
走 `vjp`（`split` 的 backward 那一半）时先撞上 `rule_expand` 的句子
（`modules/transforms/rule_expand.cpp#expandRuleRightSide`，`why = "the rule's right side reads a value the caller does not carry"`），
于是一个「改声明」的问题被读成「这条律要一个拿不到的值」。**这处没有改**：`cotangent-needs-a-value` 有三十余处 pin，
把这一支改写成 `ParameterShape` 是另一笔、要先问作者「一个类两个词」要不要收。本页只把它写下来。

## 四、改法与读数

改两处声明、两处调用点（数值、断言、pin 一个没动）：

```
func sum_of_product(v: float[2], w: float[2, 2])      let v = [1.0, 2.0] as float[2]
func sum_of_product(x: float[2, 2], u: float[2])      let u = [0.5, 0.25] as float[2]
```

调用点的 `as` 不是补丁：`SemanticDiag::ArgumentNeedsConversion` 的句子自己就要求它（「argument 1 ('v') has type
`float[]`, which is less exact than `float[2]` … write the conversion: `v as float[2]`」）。

读数（`-Plan feat/showcase_s2 -Tier fast -Jobs 8`，tip 干净）：`[SUMMARY] total: 7, pass: 7, fail: 0`，killed 0、timed out 0。
`expect_output_contains` 逐字过，即打印出来的就是 pin 的那三个数：
`vector_lhs value=3.0 dL/dv=[0.75, 1.125] dL/dw=[[1.0, 1.0], [2.0, 2.0]]`、
`vector_rhs value=3.5 dL/dx=[[0.5, 0.25], [0.5, 0.25]] dL/du=[4.0, 6.0]`，`max_abs_diff` 三个 0.0。
**所以这两条从来不是「律缺一条」，是「程序没把形状写给律」**：形状一到手，`sum_to` 的 `like` 由
`tensor:zeros(extent_of::<float[2]>())` 造出来，两条律按原样答对。

## 五、别从这里读出的两件事

1. **不要把律里的 `sum_to` 删掉换绿。** 它是这条律的归约那一半（批轴那天要用的），也是答案**类型**的来处
   （`sum_to` 答 `S`，正是操作数的类型）；删掉它会让答案的类型对不上参数，是拿语义换颜色。
2. **不要以为 vjp 那条路「不许 `float[]`」。** 语言拒绝的是「律要这个参数的形状、而声明没写」，
   不是拒绝开放长度的向量本身：同一个 `v: float[]` 放进 `with <v: float[]>` 的 `with_grad` 那一腿是绿的
   （引擎的 kernel 自己不读形状）。