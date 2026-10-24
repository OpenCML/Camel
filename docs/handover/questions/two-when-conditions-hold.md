# 两条 `when` 对同一次调用**同时成立**时取谁

（由 `handover/h399-when-at-call` 提出。该车道落地的"调用点读 `when`"本身按已有裁定实施，不需要新裁定；
它只剩这一处空白。实现记录见 `docs/handover/reports/when-at-the-call-site.md`。）

## 问题

调用点现在按算子组给出的顺序取**第一条适用的**规则（`exportNativeOperators` 的 `stable_sort`：固定签名在前，
其后是书写顺序）。`docs/design-principles.cn.md` 4.4（第 272 行）只写了"条件不成立 ⇒ 这个重载不适用"，
**没有写"两个条件都成立时取谁"**。作者 2026-10-24 对 `impl-declaration-arms` 的裁定
（`docs/handover/reviews/questions-2026-10-23.md` 第 288–294 行）要求"规则统一"到所有带 `when` 的同名重载，
但同样没有覆盖"同时成立"。

于是今天：**都成立时**回到"先写的赢"，这与刚刚被判为缺陷的"先写的赢"（当时是条件根本没被读）形似而不同——
这次是条件被读了、且两条都真。最小复现是 `test/cases/compile/native/when_order_at_call_i32_first.cml`
与 `..._int_first.cml` 的写法把两条 `when` 改成同时成立（一个用户结构体同时满足两条是最容易构造的形态）。

## 选项与代价

- **选项 A（现状：书写顺序兜底）**。代价：声明期的 `WhenDeclarationOverlap` 只对编译器**登记**的类型逐个求两条
  `when`（基本类型 + 每种复合形状一个样本），一个没抽样到的类型（例如用户结构体）可以同时满足两条，这时调用点又
  回到"先写的赢"。语义不好描述，且随类型集合变化而改变。
- **选项 B（调用点报歧义）**。像同名函数那样报 `rejectAmbiguousCall`（`builder_operators.cpp` 已有）。代价：无条件
  的重载（`fixedSignature()==nullptr` 的计算签名）今天也允许"两条都收下、先写的赢"，一律报错会动到这一大片既有
  行为，超出本缺陷范围。
- **选项 C（声明期检查用封闭类型集）**。把"程序里出现的类型"并进互斥检查（`check` 的 `program` 参数已有先例）。
  代价：仍然是编译期近似，且要新接口。

## 建议

若判 B，只对**带 `when` 的候选**报歧义；无条件重载的既有重叠留给单独一条车道，别混在这一次里。

## 出处

- `docs/design-principles.cn.md` 4.4（第 272 行，`when` 的适用条件与调用处报错）。
- `docs/handover/reviews/questions-2026-10-23.md` 第 288–294 行（`impl-declaration-arms`："忽略 `when` 是缺陷"、
  "规则统一"）、第 434–438 行（`sorts-per-element-type`：调用处不读 `when` 是缺陷，选 1）。
- 复现用例：`test/cases/compile/native/when_order_at_call_i32_first.cml`、
  `test/cases/compile/native/when_order_at_call_int_first.cml`，计划条目在
  `test/plans/feat/compile/native_declarations.plan.toml`。
- 实现位置：`src/compile/gir/builder_operators.cpp`（`Builder::resolveGroupedCall`）、
  `src/compile/gir/builder_modules.cpp`（`Builder::ruleVerdictForCall`）。