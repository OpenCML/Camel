# 从问题页带进各步施工计划的验收输入

本页汇集 2026-11-23 分诊时，各问题页留给施工计划的用例与读数；施工计划写验收节时据此取用。

## 变换骨架

- 规则右侧写 `<>`（规则语言四项扩充）的核实；`axis` 参数律的拒绝改具名诊断（来自 internal/reviews/2026-11-19-a-rules-angle-argument-is-a-port.cn.md；internal/reviews/2026-11-20-a-law-that-states-its-angle-answers-only-that-angle.cn.md）
- `nested` 的 cube/square/quartic 三条（二阶导验收）（来自 internal/reviews/2026-11-20-what-a-lifted-graph-s-second-segment-means-in-the-next-cut.cn.md）
- `first_step_tensor_vjp.cml`；showcase_s2 的 3 条拒绝用例（来自 internal/reviews/2026-11-20-when-a-ruling-read-a-success-line-as-the-failure.cn.md）
- `grad(grad(x⁴))(2) = 48` 与 `jvp(lin)` 的读数；`transforms:transpose` 的 `jvp` 规则作为骨架的一部分补（来自 internal/reviews/2026-11-20-whether-jvp-of-a-closure-answering-function-is-a-pair-or-a-lifted-closure.cn.md）
- `hessian_of_a_quadratic`（来自 internal/reviews/2026-11-20-whether-the-second-with-parameter-is-the-operand-the-cut-drops.cn.md）
- `jvp_vjp_f` 打出 `1.0 / 2.0 / 3.0`，`grad(grad(x⁴))(2) = 48`，`floatgate.*` 三条与 `residuals_in_the_closure` 逐字不变；`known_fail_msg` 钉当天那一句的规矩照 who-lifts 页 §六（来自 internal/reviews/2026-11-21-how-does-a-lift-know-it-is-a-lift-at-build-time.cn.md；internal/reviews/2026-11-21-where-does-the-capture-tangent-get-its-value.cn.md；internal/reviews/2026-11-21-who-lifts-the-members-of-a-closure-fill.cn.md；internal/reviews/2026-11-21-who-pairs-the-captures-of-a-body-built-closure.cn.md）
- `pick_at`、`mul_tangent` 对 `max4` 两例（来自 internal/reviews/2026-11-21-the-record-and-the-type-do-not-say-the-same-thing-about-a-kernels-arguments.cn.md）

## 前端分阶段

- sync 体最小用例 `add_to`；`var_parameter.write_outside_sync`（期望打印结果，不是元组）（来自 internal/reviews/2026-11-19-a-sync-body-takes-the-call-s-tuple-as-the-statement-s-value.cn.md）
- `var_rule_subpasses.cml:54-60`（`let f = bump; f(c, 2)`）、`var_params.cml:52`（`bump_then`）（来自 internal/reviews/2026-11-19-a-tuple-flows-into-an-operator-s-arguments.cn.md）
- `test/cases/linear/byref/var_param_reference.cml`（`count` 2 万层，不长帧）、`var_writes_through_recursion`、`var_read_before_callee_write`（来自 internal/reviews/2026-11-19-a-var-parameter-recursion-loses-its-tail-call.cn.md）
- 页中 113 条失败的两类用例（含 `modules/autodiff/var_through_with_grad.cml:26`：泛型或变换接收带 `var` 形参的函数，按声明类型检查）（来自 internal/reviews/2026-11-19-route-b-s-113-failures-split-into-two-classes.cn.md）
- 本页 §1、§3 的构造点清单，作为"建图只有一处造调用"的核对表（来自 internal/reviews/2026-11-19-the-call-construction-sites-and-the-three-sources-of-an-exit-type.cn.md）
- `var.parameter_write_is_the_callers_value`、`var_parameter.write_outside_sync`（期望打印结果，不是元组）（来自 internal/reviews/2026-11-19-the-entry-packs-nothing-and-the-five-reds-are-a-call-s-tuple.cn.md）
- 顶层 `var` 改报编译错误（来自 internal/reviews/2026-11-19-the-front-end-in-place-path-is-reachable-for-var-parameters.cn.md；internal/reviews/2026-11-19-the-front-end-in-place-rewriting-is-unreachable-not-removed.cn.md）
- `decorator_*` 7 条、`order_edges_regions` 5 条、`same_resource_order*` 6 条（声明里没写结果类型的图）（来自 internal/reviews/2026-11-19-the-three-steps-target-code-exists-only-on-the-parked-branch.cn.md）
- `println(store(x))` 必须打印结果（`var.parameter_write_is_the_callers_value`）（来自 internal/reviews/2026-11-19-three-attempts-at-projecting-a-calls-value-and-what-each-refuted.cn.md）
- 禁项："按地址作键的表，其遍历次序不得影响建出的图"（来自 internal/reviews/2026-11-20-who-reads-the-two-graphs-of-a-refused-capture-on-windows.cn.md）

## 执行（核心抽象第 14 节）

- `two_names_for_a_struct`、`value_used` 两条守门用例（计数替换 `valueMadeHere` 时验收）（来自 internal/reviews/2026-11-19-an-in-place-argument-has-to-be-one-this-graph-made.cn.md）
- `sort_rebinding.cml`、`xs[0] = 9` 元素写的原地形式（由执行计划给出）（来自 internal/reviews/2026-11-19-the-front-end-in-place-path-is-reachable-for-var-parameters.cn.md；internal/reviews/2026-11-19-the-front-end-in-place-rewriting-is-unreachable-not-removed.cn.md）

## 阶段 3b 编辑会话与事实

- `eval-round10/e2e` 里 `instrument_ordered` / `_reversed` 三个导出，作为编辑会话的验收用例（来自 internal/reviews/2026-11-19-an-order-edge-across-an-inline-region-is-ignored-by-the-effect-check.cn.md；internal/reviews/2026-11-19-order-cannot-reach-a-node-that-inline-just-made.cn.md）
- `gpu_scope.*` 两条和 `literal.tensor_element_type_gpu_scope_compiles`（clause 刷新后补顺序边）（来自 internal/reviews/2026-11-19-the-call-site-refresh-in-the-tiles-pass.cn.md；internal/reviews/2026-11-19-the-writes-a-clause-refresh-adds-have-no-order.cn.md）
- 编辑会话撤销后签名必须回到保存点的样子，作为会话不变式用例（来自 internal/reviews/2026-11-19-what-a-rollback-puts-back-the-pointer-or-the-content.cn.md）
- `Fold`/`Norm` 形状对不上位置时读成该类顶元（足迹一条规则）（来自 internal/reviews/2026-11-20-the-higher-order-footprint-has-a-place-and-a-reader.cn.md）

## 阶段 2

- 迁移前先量页 §4① 的测量：`draft.h#ungated` 的 9 个调用者有没有读到等待输入（来自 internal/reviews/2026-11-19-the-gate-value-has-five-readings.cn.md）

## 阶段 3a

- `GCGraph`、`GraphDraft` 直接引用计数只减不增、最后到 0，上线后删旧账本（来自 internal/reviews/2026-11-20-a-ledger-that-may-only-fall-meets-a-legal-merge.cn.md）
