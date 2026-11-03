# 乘积律读的那个形状，该由谁写出来？

**状态**：待裁；我方默认见第 4 节（**甲：由规则把长度写出来**，在 `stdlib` 那条车道做）。

写这页的直接原因：作者 2026-11-03 19:55 的判决（`docs/handover/reviews/questions-2026-10-29.md` §4）说，`cotangent-needs-a-value` 那 7 条拒绝的成因只有一处——转置律要读的那个已知值在被转置的函数里是**闭包端口**，而 `forward` 不会处理闭包端口；"这一处补上，7 条应当回到原来的数"。我按这条把两件事落地（闭包成员不收余切 + 只由闭包成员算出来的值也不收），实测**只有 1 条回到原数**，另 6 条往前挪了一步，停在另一处：律要读的那个值不是闭包里的值，而是**它正在给余切的那个操作数自己的形状**，而那个操作数是切向。见第 1、2 节。

## 1. 读数（实测，含树、时间、负载）

树 `/home/zjwei/camel-work/fwdclosure`（`34b659107` + 本节两处改动），宿主 88zjwei（32 核），2026-11-04 04:07–04:26，`-Tier fast -Jobs 4`，每次跑完的 1 m load 在 17.1–21.5 之间（宿主一直不静，读数按同负载对比）。

1. **`forward` 传闭包端口这一处，`236d06f37` 已经在了。** `modules/transforms/transpose.cpp#buildTransposeMember` 的 `forward` 在 `kind` 分派之前就查 `closurePortOf`，命中就把成员的闭包端口转出去。19:55 判决描述的补丁不需要再补。

2. **真正缺的是"闭包成员收不收余切"。** `wantsCotangent` 对闭包端口原来落到函数最后那句 `return true`（没有写下来的规则），于是"给 `2.0*x` 那一份余切"这种没人要的答案也会去磨律，磨律要读参与微分的操作数，那里没有值。加上"闭包端口回 `false`"，再加上 `reachesAParticipant` 不再只在 `partial` 时算（只由闭包成员和常量算出来的值，答案里本来就没有它的槽），手写闭包那条就通了。

3. **7 条现在的读数**（基线见 `docs/handover/reviews/2026-11-04-does-a-closure-members-cotangent-have-a-slot.cn.md` §4）：

   | 用例 | 基线（`545b24d92`） | 改动前（`34b659107`） | 改动后（本次） |
   | --- | --- | --- | --- |
   | `transpose_hand_closed_lambda.same_answer_as_the_cut` | `cut 6/18`、`hand 6/18` | 拒绝，`:op/mul_d position=0` | **`cut 6/18`、`hand 6/18`，与基线逐位相同** |
   | `relu_gradient_through_the_library` | `value=3.0 dL/dx=[0.0, 0.0, 1.0, 0.0]` | 拒绝，`tensor:multiply position=0` | 拒绝，`position=1` |
   | `exp_gradient_through_the_library` | `value=10.107337927389695 d=[2.718281828459045, 7.38905609893065]` | 同上 `position=0` | 拒绝，`position=1` |
   | `sigmoid_gradient_through_the_library` | `value=0.8914007525718497 d=[0.2350037122015945, 0.19661193324148185]` | 同上 `position=0` | 拒绝，`position=1` |
   | `tanh_gradient_through_the_library` | `value=-0.2994769986957551 d=[0.7864477329659274, 0.41997434161402614]` | 同上 `position=0` | 拒绝，`position=1` |
   | `transpose_array_product.answers_both_operands` | `value = 20.0`、`d/dxs = 2.0 3.0 4.0`、`d/dw = 1.0 2.0 3.0` | 拒绝，`position=0` | 拒绝，`position=0`（没动） |
   | `jvp_vjp_array_product.one_point_two_ends` | `value = 20.0`、`forward = 6.0`、`backward = 6.0` | 拒绝，`position=0` | 拒绝，`position=0`（没动） |

   全树：`feat/compile` **2230 pass / 2 fail / 10 known failures**（基线 2243 = 2230 / 2 / 11；两条红是同两条 `transpose_loop_escape.*`）。

4. **探针把停点钉死了**（`split_of_jvp_of_total_backward`：`closureN=3`、`normN=1`，唯一那个普通端口是切向 `dxs`；改前 `PROBE rule=mul_tensor_t slot=1`、`value-of ref=3 inNorm=0`）。`stdlib/rulesets/transpose.cml#mul_tensor_t` 的右侧是

   ```
   mul_tensor(x, w) => (ct: T[..]) => (sum_to(ct * w, x), sum_to(ct * x, w))
   ```

   `sum_to` 的第二个实参只读**形状**。所以给第 `i` 个操作数磨余切时，律读的就是**第 `i` 个操作数本身**：`w` 那一份读 `w`，`x` 那一份读 `x`。切向没有值，`forward` 走到 `shapeCarrierOf`，而 `float[..]` 没写死形状，所以答不出来（`float[2]` 就能答——那是 16:15 那条规则：转置需要参数的形状，就在类型里写出来）。

5. **所以 19:55 那句"成因只有一处"少了一处。** 闭包那一处确实补上了（手写闭包 `:op/mul_d` 一族已回原数），`mul_d` 的右侧没有 `sum_to`，读完另一个操作数就结束；`tensor:multiply` 的右侧有，于是 6 条停在这里。这不是回归：改前它们停在更前面，改后停在更后面，同一句 `skip=cotangent-needs-a-value`、同一个 `uri=tensor:multiply`。

## 2. 与 19:55 那句话的对账

作者的原话（2026-11-03 19:55）："这一处补上，7 条应当回到原来的数。" **量下来不是这样：补上之后只有 1 条回去。** 逐条对账：

| # | 用例 | 现在读到 | 回到基线数？ |
| --- | --- | --- | --- |
| 1 | `transpose_hand_closed_lambda.same_answer_as_the_cut` | `cut ct1= (6.0)` `cut ct3= (18.0)` `hand ct1= (6.0)` `hand ct3= (18.0)` | **是**，四个数与基线逐位相同，手写闭包与 `split` 造的那一半相同 |
| 2 | `relu_gradient_through_the_library` | `TRANSPOSE function=split_of_jvp_of_total_backward skip=cotangent-needs-a-value uri=tensor:multiply position=1 reason=the rule's right side reads a value the caller does not carry` | 否，`position=0` → `position=1` |
| 3 | `exp_gradient_through_the_library` | 同上，`position=1` | 否，同上 |
| 4 | `sigmoid_gradient_through_the_library` | 同上，`position=1` | 否，同上 |
| 5 | `tanh_gradient_through_the_library` | 同上，`position=1` | 否，同上 |
| 6 | `transpose_array_product.answers_both_operands` | `TRANSPOSE function=split_of_jvp_of_energy_backward skip=cotangent-needs-a-value uri=tensor:multiply position=0 ...` | 否，位置没动 |
| 7 | `jvp_vjp_array_product.one_point_two_ends` | 同上，`position=0` | 否，同上 |

两句话解释这条差额，都是量出来的，不是推的：

1. **19:55 描述的补丁已经不在待补之列。** `forward` 在 `kind` 分派之前就查 `closurePortOf`，命中就把成员的闭包端口转出去——这是 `236d06f37` 的提交内容（"transpose reads a closed half's residuals from its captures, not from a parameter"）。所以"补这一处"没有可补的代码，本次也**没有**新加。
2. **剩下 6 条的病灶不在闭包，在律自己。** 手写闭包走 `:op/mul_d`，它的右侧读完另一个操作数就结束，所以补上闭包那两条判决（不收余切、只由闭包成员算出来的值也不收）它就通了。`tensor:multiply` 的右侧多一个 `sum_to`，而 `sum_to` 的第二个实参读的是**正在被给余切的那个操作数自己**的形状；那个操作数是切向，`float[..]` 没写死形状，于是 `forward` 走到 `shapeCarrierOf` 也答不出来。这是第 1 节第 4 条钉的停点，也是第 3 节甲要解的那一处。

**所以这 6 条不该被当成"闭包判决没落地"的证据**：改前它们停在更前面，改后停在更后面，同一句 `skip=cotangent-needs-a-value`、同一个 `uri=tensor:multiply`，位置从 0 走到 1。

## 3. 三条路

| 路 | 做法 | 代价 |
| --- | --- | --- |
| **甲：规则把长度写出来** | 17:35 已经给下标族做过：`element_at(dxs, extent_pair(i, len(xs)))`，长度由前一半算、作残差进闭包、`transpose` 经 `forward` 拿到。照这个给乘积族做：`jvp_mul_tensor` 写线性部分时把两侧长度作为**已知实参**交给那个线性调用，`mul_tensor_t` 读那两个实参，不再读操作数 | `stdlib/rulesets/jvp.cml` 与 `stdlib/rulesets/transpose.cml` 两处；本车道按派工不碰 `stdlib/**`。改完 6 条按第 1 节第 3 条那张表的"基线"列回原数，`known_fail` 全消 |
| **乙：类型里写形状** | 把 6 条用例的 `float[..]` 改成 `float[2]` / `float[3]`，`shapeCarrierOf` 造同形状的零数组给律读形状 | 数字不变（只剩形状被读），但 6 条恰恰是拿**开秩**数组写的：改完就等于"库这条路要求形状写在类型里"，而这句话要么进文档、要么被下一条用例推翻。另外 `transpose_needs_parameter_shape` 已经守着"形状不在类型里就拒绝"这一侧 |
| **丙：就这么留着** | 把 `cotangent-needs-a-value` 当语言边界写进文档，6 条一直挂 | 基线那批数回不来，19:55 的预期（7 条回原数）做不到；而且今天拒绝的成因是"律读了线性操作数"，不是"语言不让" |

## 4. 我方默认

**甲。** 理由：17:35 已经就同一件事裁过一次（律只能读已知操作数，原值信息由前一半算、作残差进闭包），下标族照办后通了；乘积族是同一个病灶的下一处，没有新判决可等。

本轮我已按甲把**本车道能做的**都落地了：`wantsCotangent` 对闭包端口答 `false`、`reachesAParticipant` 对每个转置都算且看两条输入表（否则元组用例静默变零）。手的闭包那条已回基线四个数并去掉 `known_fail`；另 6 条保留 `known_fail`，pin 改成 `skip=cotangent-needs-a-value uri=tensor:multiply`（不带位置——位置从 0 挪到 1 是同一成因往前走了一步，不该被当成换了个毛病）。

**阈值**：甲落地时若发现除这 6 条之外还要改别处的期望，退到乙，并在那一页里写清"库这条路的参数形状必须写在类型里"。
## 5. 已解（2026-11-04，`lane-multensor`，`52d4ff75b`）

**甲，但读的不是类型，是另一个操作数。** 6 条回到基线数，`known_fail` 全消。

先说清一件事：甲原文写的是"把两侧长度作为已知实参交给那个线性调用"。**这条路在本仓现有的算子集合里走不通**，两条都试过，都不是猜的：

1. `sum_to` 的第二个实参要的是一个**值**，形状只是它被读的那一面。要让律"只读长度"，得有一个 `sum_to_stated(t, at: int[])` 这样的孪生算子。`git grep sum_to_stated` 在整棵树和所有分支里都是 0——它不存在，而新加它要动 `modules/tensor`（本车道按派工不碰）。
2. `extent_of::<X>()` 这条路对开秩类型没有答案：`float[..]` 不写死形状，`extent_of` 在实例化处就被拒（`extent_of_needs_a_type_that_states_its_shape` 守着的正是这一侧）。6 条用例的参数恰恰全是 `float[..]`／`float[]`。所以"长度从类型来"对这 6 条**没有可读的长度**。把它当作宽化端口（`*_from_type` 那一族的写法）同样拿不到东西，因为宽化端口交出来的就是 `extent_of` 的答案。

真正能读的已知值是**另一个操作数**。`sum_to` 的第二实参在等形状时是恒等，而 `tensor:multiply` 就是逐元素的——引擎自己的孪生 `modules/tensor/ops/elementwise_ops.cpp#multiplyVjp` 只写 `multiply(dy, rhs)` 累加进 `lhs`，自己不做任何归约，归约是 `accumulateOperand` 按操作数形状补的。所以"乘积的形状"和"另一个操作数的形状"是同一个形状，谁都能写出来。于是：

```
mul_tensor(x, w) => (ct: T[..]) => (sum_to(ct * w, w), sum_to(ct * x, x))
```

每一侧命名的是它**没有在给余切**的那个操作数：`x` 那一侧读 `w`，`w` 那一侧读 `x`。没有一侧读切向，没有零载体，`sum_to` 仍然读形状。这就是 17:35 那条（原值信息由有值的那一半交出来）在乘积族上的形状，也是下标族 `element_at(dxs, extent_pair(i, len(xs)))` 的同一招。

**取舍**：`T[..]` 包含秩 0，把"数组 × 数"交给这一个律的写法（原先注里那句）现在改为归 `:op/mul_d`／`mul_t`，律本身只对逐元素乘积成立。这不是缩水：开秩操作数在秩 0 一侧的归约，旧写法同样读不到形状（拒绝点完全相同），而新写法给出的答案在算子接受的每一种形状上都对。

读数与判据见 `docs/handover/reports/2026-11-04-the-product-law-reads-the-other-operand.cn.md`。