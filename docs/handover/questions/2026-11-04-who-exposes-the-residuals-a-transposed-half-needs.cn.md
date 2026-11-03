# 切出来的后一半要的那些残差，由谁交给调用点

**一句话**：2026-11-03 16:55 的裁定说「按内容对被调函数做 `split`，转置它的后一半，把调用转过来」。**这条规则在调用点少一环**：后一半要的残差是前一半**算出来的**，而前一半的出口是 `(result, lin)`，**残差被 `lin` 的闭包包住、不作为数据露出**，调用点无从填。**所以裁定还差一句：残差从哪条路出来。**

## 实测依据（`lane-linports` 规格页 `2026-11-04-how-is-the-by-content-rule-built-at-a-call-site.cn.md`）

1. 前一半出口的形状在 `modules/transforms/split.cpp#splitHalvesWith` 里造：`TupleType::create({result, halves.backward})`——**两格：结果、后一半**。**残差不在里面。**
2. 残差由 `modules/transforms/split.cpp#buildLinearClosure`（匿名命名空间）闭包进 `lin`，**调用点拿不到**。
3. 后一半的签名是 `(…, 残差…) => …`，**没有残差就填不满**；而"填不满"的后果不是拒绝，是 `Runtime indirect call norm-arity mismatch`（`transpose_loop_escape` 那两条长红就是这个机制的现场）。

## 两条路

- **甲：前一半出口露出残差。** 把前一半出口改成 `(result, lin, 残差…)` 或让 `lin` 带一份可读的残差表。**代价**：改 `Vjp<F>` 的**可见形状**——那是语言层面能观察到的类型，所有 `vjp` 的用例与文档都跟着变；这一条**要作者点头**。
- **乙：让 `transpose` 学会"调一个闭包值"。** 调用点不去凑残差，而是把"后一半 + 它闭包里的值"当成一个整体去调。**代价**：内部改动，`transpose` 侧要能解析闭包并填 `with` 实参；**不动语言可见的形状**。

## 默认建议（施工方默认）

**乙**，并按它开工。理由三条：

1. **甲改的是语言可见形状**，而这条规则的目的是让"两条路径走同一条规则"，不是改 `vjp` 的对外类型——**用改对外形状来修内部规则，代价与收益不成比例**。
2. **乙不动任何用例的期望**：`vjp` 的签名不变，`transpose_hand_closed_lambda` 的四行读数（`cut ct1= (6.0)` / `cut ct3= (18.0)` / `hand ct1= (6.0)` / `hand ct3= (18.0)`）应逐字不变。
3. **乙失败的样子是拒绝，甲失败的样子是错数**：乙若做不成，`transpose` 会说"这个闭包我填不满"；甲若做错，`vjp` 的类型悄悄换了、没有任何用例钉着。按 **对 > 明确拒绝 > 错数 > 崩** 的次序，乙的风险面更小。

## 判据 / 哨兵

- **`feat/transforms/split` 19/19**；`feat/compile/transforms` **不新增红**（基线 `92 = 89/2/1`）。
- **两条长红 `transpose_loop_escape.two_values_of_two_kinds` / `.second_state_of_the_same_loop` 必须仍是拒绝**，贴改后原文。**若它们变绿，必须数对——判据是把它们的数逐元素钉住，不是"不再红"。**
- **`grad_array_no_int` 必须仍答 `6.0` / `[2.0, 3.0]`**（逐字）。
- **库层 `with_grad` 在 `grad_first_with_parameter` 上与引擎同答**（逐元素）。
- **`with_grad_composition_meets_the_split_wall` 仍红且理由相同**（它的出口是"调用的对"，不是 `Fill`，判据给 `NotAPair`）——**不许被这次改动弄绿**。
- 若取甲，**必须同时给出"`vjp` 的新可见形状"的完整用例改写清单**，否则不许动。
