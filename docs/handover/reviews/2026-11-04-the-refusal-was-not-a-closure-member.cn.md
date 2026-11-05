# 那句「这一处补上，7 条应当回到原来的数」，我们照做了，打印指向另一个地方——剩下的 6 条算规则层，还是你认为还有一处没找到？

**状态**：待裁；我方默认见第 3 节（**甲：记成两处成因，规则层那半照 04:20 的裁定做**）。

## 写这页的直接原因

作者 2026-11-03 19:55 的裁定（`docs/handover/reviews/questions-2026-10-29.md` 的 §「进展与裁定」）把 7 条 `cotangent-needs-a-value` 的拒绝成因归成**一处**：转置律要读的那个已知值，在被转置的函数里是**闭包端口**，而 `forward` 不会处理闭包端口；给的改法是"`forward` 遇到闭包端口，就转发成成员自己的闭包端口"，并写"**这一处补上，7 条应当回到原来的数**"。同一节还判断两条元组用例的静默零是「**有一个依赖线性输入的值被放进了闭包成员**」。

这两条定位我们都照着做了，量出来的结果与它们不一致：`forward` 那一处**早就在树上**，7 条里只回来 1 条；两条元组用例变零的原因**不是闭包成员**。写这页不是"裁定错了"，是**照你给的定位法做完以后，打印指向另一个地方**，剩下的 6 条要有归属。

## 1. 读数（实测，含树、时间、负载）

1. **`forward` 的闭包端口转发在 `236d06f37` 就已经在树上。** `modules/transforms/transpose.cpp#buildTransposeMember` 的 `forward` 在按 `kind` 分派**之前**就查 `closurePortOf`，命中就把成员的闭包端口转出去。19:55 描述的那处补丁不需要再补；照它再补一遍，读数不变。

2. **按乙（闭包成员不收余切）落地后，7 条里回来 1 条。** `bbfc003ec` 之后，手写闭合 lambda 重新答出切的那四个数（`cut ct1= (6.0)`、`cut ct3= (18.0)`、`hand ct1= (6.0)`、`hand ct3= (18.0)`）；`relu`/`exp`/`sigmoid`/`tanh` 与两条数组乘积**只往前挪了一步**，停在 `skip=cotangent-needs-a-value uri=tensor:multiply position=0`。那一步的位置说明：律要读的那个值**不是闭包里的值**，而是**它正在给余切的那个操作数自己的形状**——那个操作数是切向，类型 `float[..]` 不写形状。这是 `stdlib/rulesets/transpose.cml#mul_tensor_t` 读 operand 那一行，不是 `forward`。

3. **两条元组用例的零，来自"只走 normal 列表"，不是闭包成员。** `modules/transforms/transpose.cpp#reachesAParticipant` 原来只遍历 `normInputsOf`，而一个元组的组件是构建它的 `Fill` 的 **`<>` 输入**（它的 normal 输入只有形状模板）。于是 `let t = (x, 2x)` 这个元组被判成"不触达任何参数"，两个组件的余切一起被丢，答案静默变 `(0.0)`。两张输入列表都走之后（`bbfc003ec`），最小程序 `test/cases/compile/transforms/transpose_no_tangent_tuple_member.cml`：

   ```
   func k(x: float): float { let t = (x, 3); return float(t.1) + t.0 }
   k(2.0)            // 5.0
   transpose(k)(1.0) // (1.0)
   transpose(k)(3.0) // (3.0)
   ```

   同一条用例在 `transpose_tuple_component.two_reads_of_one_tuple_add` 上答 `3.0` / `(3.0)`——即 19:55 要求的那两个数。**没有任何依赖线性输入的值被放进闭包成员**：闭包里放的是 `x` 这个参数本身，问题是"这个元组算不算触达它"被答错了。

4. **全量 fast 层的读数**（树 `2ccce5f09`，宿主 88zjwei，2026-11-04 12:0x–12:1x，`-Tier fast -Jobs 4`，1 m load 20.5–24.2）：`total: 8762, pass: 8718, fail: 3, skip: 11, known failures: 30`，3 红逐条是 `matmul_t.vector_lhs_law.nvm`、`matmul_t.vector_rhs_law.nvm`、`matmul_t.rank_three_is_refused_by_name.nvm`（基线上就红），没有进程被信号杀死。

## 2. 三条路，各带代价

| 路 | 做法 | 代价 |
| --- | --- | --- |
| **甲：记成两处成因** | 把 19:55 那句改成两段：`forward` 的闭包端口转发已在 `236d06f37`（不是缺口）；剩下 6 条归**规则层**——律读的那一侧不写形状，由规则把长度/形状作为已知实参交出。规则层那半照 04:20 二节的裁定做（`jvp` 规则交出长度、需要一个收长度的 `sum_to`） | 作者原话里"这一处补上"要改成"两处"；好处是 6 条有了归属，不会被当成"`forward` 还没修"再补一遍 |
| **乙：仍是 `forward` 一处** | 认为还存在一处我们没找到的 `forward` 缺口，再找一轮 | 要找的是已经量到不存在的缺口：`bbfc003ec` 落地时核过，`forward` 的闭包端口转发在 `236d06f37`，把乙落地后 6 条仍停在 `skip=cotangent-needs-a-value uri=tensor:multiply`。再花一轮，6 条仍是这个读数 |
| **丙：不裁，只做规则层** | 不动 19:55 那节，直接按 04:20 做规则层 | 不做也行，但下一个读 19:55 那节的人会照着再补一遍 `forward`——这是本页写下来的唯一成本 |

## 3. 我方默认

**甲。** 理由：两条定位法都已照做，**读数不是解释而是打印**（`skip=` 行带着 `uri=tensor:multiply`，与 `forward` 无关）；而 6 条要改的那一处（规则层写形状）**已经被 04:20 二节的裁定覆盖**，所以甲不新增决定，只是把成因记对。

本轮按甲开工：这一页入档，报告页记成"19:55 的前提不成立、实测指向规则层"，规则层的活另开一条 `stdlib` 车道，不并进合并车道。作者若裁乙，我按 `cotangent-needs-a-value` 的现场读数再来一轮（`modules/transforms/transpose.cpp#buildTransposeMember` 的 `closure=` 行能印出成员带了哪些闭包端口，`skip=` 行带着 `uri=` 与 `position=`，两行合起来能指出哪个节点被判成不触达）。