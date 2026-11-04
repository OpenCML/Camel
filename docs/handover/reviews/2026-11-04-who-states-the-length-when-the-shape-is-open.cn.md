# 形状开放时，长度由谁陈述？

**状态**：待裁；我方默认见第 4 节（**甲：由前半程陈述，落点在 `modules/tensor/ops/**` 的一个新算子**）。本轮按默认停在"明确拒绝"，报告里标 `未达成主目标`。

写这页的直接原因：作者 2026-11-03 17:35 / 17:55 两次裁定"**形状与长度必须由规则层当作已知操作数交给转置，绝不许从操作数的值里读**""**不要零载体**"。本轮把"形状由类型陈述"这一半落进了代码（`stdlib/rulesets/transpose.cml#mul_tensor_t` 写成 `sum_to_stated(ct * w, shape(x))`，`modules/transforms/rule_expand.cpp#expandRuleRightSide` 把 `shape(<捕获>)` 换成类型里陈述的长度），实测**数字一个没变**——因为凡类型陈述得出长度的情形，树里今天本来就答对（下面第 1 节第 2 条），而**长度陈述不出来**的那一族仍在拒绝（第 1 节第 3、4 条）。剩下这半步不是形状能补的，是"长度根本没人陈述"。

## 1. 读数（实测，含树、时间、负载）

树 `D:\Projects\Camel-wt\shapes` → 宿主 `88zjwei`（32 核），分支 `lane-shapes`（基线 `34b659107b`），2026-11-04 06:46–07:3x，1 m load 19.22 / 24.64。

1. **基线**：`feat/compile` = `total: 2243, pass: 2230, fail: 2, known failures: 11`；`feat/compile/transforms` = `total: 105, pass: 98, fail: 2, known failures: 5`；`feat/transforms` = `total: 29, pass: 21, known failures: 8`；`feat/transforms/split` = 19/19；`feat/modules/stdlib_registry` = 13/13。

2. **陈述得出长度的广播，今天已经与引擎同数**（我写的探针，`vjp` 与 `grad` 同一个程序）：
   - `total(x: float[2, 2, 3], h: float[3]) = sum(x * h)`，`vjp` 读数 `value=24.0`、`d/dx` 逐元素 `[1,2,3]` 铺四道、`d/dh = [4,4,4]`——引擎侧同一个程序的 `grad` 读数逐元素相同（`test/plans/feat/compile/broadcast_product_gradient.plan.toml` 两条）。
   - `total(x: float[2, 3], h: float[3]) = sum(x * h)`：`value=12.0`、`d/dh = [2,2,2]`。
   - **这一族今天靠的是零载体**（`modules/transforms/transpose.cpp#shapeCarrierOf` 在没有值时按类型造一个零数组，`sum_to` 只读它的形状），所以数字对、机构的来源不对——作者 17:55 的那句"不要零载体"说的正是这里。本轮改动把这份读数改由类型陈述的长度答（第 2 节），**数字不变**。

3. **长度陈述不出来的一族仍然拒绝**，且我量到拒绝落在**哪个参数**上（临时在 `why` 里加了 `parameter=… port=…` 再跑，读完已撤）：

   | 探针 | 节点操作数类型 | 拒绝 |
   | --- | --- | --- |
   | `total(x: float[2, 2, 3], h: float[]) = sum(x * h)` | `(float[2, 2, 3], float[])` | `skip=cotangent-needs-a-value uri=tensor:multiply position=0`，缺的是 **`parameter=1 port=float[]`**（即 `h` 自己） |
   | `total(x: float[3], s: float) = sum(x * s)` | `(float[3], float)` | 同上，缺的是 **`parameter=1 port=float`**（即数字 `s`） |

   两次都是**位 0 的答案要读位 1 的*值*`（`ct * w`），不是它的形状**。所以这不是形状通路能补的：形状那一半已经补好了（`shape(x)` 走类型），补不了的是"另一个操作数的值在这一程里没有"。

4. **那 6 条已知拒绝的是同一件事**：`test/plans/feat/compile/transforms.plan.toml` 与 `transpose_array_product.plan.toml` 里 `known_fail_msg = "skip=cotangent-needs-a-value uri=tensor:multiply position=0"` 的 6 条（`transpose_array_product` 2 条 + `relu/exp/sigmoid/tanh_gradient_through_the_library` 4 条）。

   同一天 `docs/handover/reports/2026-11-04-the-three-laws-state-their-extents.cn.md` 第七节独立量到同一句话：`sum` 那一半修好之后，`relu_gradient_through_the_library` **往前推进了一个节点**，停在乘积律上，"那个位置的答案要读另一个操作数的值，也就是切线，转置永远没有"。那页的结论与本节第 3 条相同，落点也相同：不是形状，是值。

   改法只要动**闭包成员收不收余切**那一处（`modules/transforms/transpose.cpp#buildTransposeMember` 的 `wantsCotangent` 兜底 `return true`，见 `docs/handover/questions/2026-11-04-does-a-closure-members-cotangent-have-a-slot.cn.md` 第 4.1 节）；作者 19:55 已裁**乙（不收）**，但那次试验实测**一次翻 16 条**（两条数字从 `(3.0)` 掉成 `(0.0)`，另 14 条转红：`transpose_runs_a_loop_rounds_backwards`、四条 `transpose_loop_escape.*`、`transpose_unread_state_answers_zero.jit/.prl` 等），且要连带裁"`Vjp<F>` 的可见形状随不随之变"。所以本节把这一条**留着不动**，它不属于"形状由谁陈述"这一页。

## 2. 本轮落地的那一半（形状由类型陈述）

- `stdlib/rulesets/transpose.cml`：新增私有 `sum_to_stated::<T>(t: T[..], at: int[]): T[..] = sum_to(t, zeros::<T>(at))`；`mul_tensor_t` 改写成 `(sum_to_stated(ct * w, shape(x)), sum_to_stated(ct * x, shape(w)))`。
- `modules/transforms/rule_expand.cpp#expandRuleRightSide`：一条律的右式里，若某个捕获**只**被 `tensor:shape` 读，且**该律自己的端口**（`rulePortsOf`）陈述得出长度，就把这个 `shape(...)` 节点直接换成一份 `int[]` 常量（`statedExtentsValue`），并把该捕获从 `keep`/绑定里去掉。`statedShapeOf` 从 `transpose.cpp` 挪进 `rule_expand.h`，两处共用一份读法。
- `zeros::<T>(at)` 不是零载体：`sum_to` 的第二个操作数按文档只被读形状（`stdlib/tensor.cml#sum_to` 自己的例子就是一个零数组），这串零是**律用规则层交来的长度自己造的**，不是拿来顶替某个操作数的值；顶替值的那个（`shapeCarrierOf`）在 `mul_tensor_t` 这条律上不再被走到。请作者顺手看一眼这个读法算不算"零载体"。

## 3. 三条路，各管哪一半

| 路 | 做法 | 管得了什么 / 状态 |
| --- | --- | --- |
| **甲：形状由类型陈述** | 律写 `sum_to_stated(ct * w, shape(x))`，规则层把 `shape(x)` 换成类型里的长度（`statedShapeOf` → `int[]` 常量） | **本轮已落**（第 2 节）。管的是"被回答的那一位自己的形状"。**数字不变**，因为陈述得出长度的情形今天本来就对（第 1 节第 2 条）；陈述不出长度的情形它也管不了（类型里就没有） |
| **乙：长度由前半程当已知实参交下来** | 照 `sum_stated` / `take_stated` / `mean_axis_stated` 的样子做一个带长度的乘法算子，正向律把 `shape(x)`、`shape(w)` 算好交进去，反向律读那个 `int[]` 操作数 | **本轮派工禁止**（要动 `modules/tensor/ops/**`）。这条路**管不了** `mul_tensor`：位 0 的答案要的是另一个操作数的**值**（`ct * w`），不是它的长度——长度交下来，值仍然没有 |
| **丙：让走查不问那一位** | `modules/transforms/transpose.cpp#buildTransposeMember#wantsCotangent` 对闭包成员回 `false`（作者 19:55 裁的乙） | 这才是那 6 条真正的落点；实测**一次翻 16 条**（既有页第 1 节第 3 条），且要连带裁 `Vjp<F>` 的可见形状。属既有页，不属本页 |

所以本页的问题只在甲、乙之间：**类型陈述不出长度时（`float[]`、`float[..]`、数字），这个长度该由谁陈述？** 甲的答案是"没人陈述，拒绝"；乙的答案是"前半程陈述，要一个带长度的算子"。对 `mul_tensor` 来说这题怎么答都不影响它的红——它红在丙。但对 `mean_axis` 那一族、以及任何"反向律要造一个和线性操作数同形状的数组"的律（既有报告第一节那三条），这题就是全部：那三条已经用乙答了，所以它们今天绿。

## 4. 我方默认

**乙，但本轮不做。** 理由：乙是**已经走过的路**（三条孪生算子就在树里），甲只是它的退化情形（长度刚好写在类型里）；本轮派工禁 `modules/tensor/ops/**`，所以按派工只落甲，开放长度那一族**继续明确拒绝**（今天已经是拒绝，不是错数）。

**主目标（广播下 `mul_tensor_t` 与引擎同数）按第 1 节第 2 条其实已经成立**（陈述型形状，实测同数），**但按第 1 节第 3、4 条在开放型形状上不成立**——卡在丙，不是卡在形状。报告按派工要求标 `未达成主目标`。

若作者裁定"零载体一律不许，包括 `sum_to(t, zeros(at))` 这种律自己造的零数组"，则 `sum_to_stated` 要换成从操作数直接读长度（`reshape` + `sum` 之类），请一并裁一句。