# 规则库的 `x * 0 => 0` 会去掉效应：由引擎拒绝，还是先把规则撤下来？

## 问题

H226 的测量（`test/plans/feat/opt/algebra_rules.plan.toml`，基线 `origin/develop c638d55a0`）把规则库与旧的 C++ 代数 pass 逐条对量。其中一条是**库会做、C++ 两边（删掉的法表与今天的参考模式）都从来不做**的：

```
let a = println(5) * 0
println(a)
```

| 运行的 pass | 程序输出 | 图 |
| --- | --- | --- |
| 无 | `7 9 5 0 0`（5 行，`println(5)` 跑了） | `effect_mul_zero` 7 节点 / 6 边，`:op/mul_l` 1、`:io/println` 2 |
| `std::opt::algebra neutral_only=true` | `7 9 5 0 0`（同上） | 同上，一字不动 |
| `rules::simplify` | `7 9 0 0`（4 行，`5` 没了） | 6 节点 / 4 边，算术 0 个，`:io/println` 仍是 2 |

规则是 `stdlib/rules.cml:33` 的 `int_mul_zero(x) { x * 0 => 0 }`（同族还有 `int_zero_mul`、`int32_mul_zero`、`int32_zero_mul`）。右侧把乘积换成常量 `0`，读乘积的那个 `:io/println` 节点**还在图里**（println 计数仍是 2），但没有任何使用者，运行时的活性（从出口沿数据/同步边可达）也就不再包含它——于是 `println(5)` 的效应不再发生。

这违反**设计原则 9.7**（`x * 0 => 0` 不得作用于 `println(5) * 0`，`docs/design-principles.cn.md:556-559`）与**重写语言 v2 第 8 节**（被右侧丢掉的部分必须是纯的，`docs/design/rewrite/v2.cn.md:111`）。H243 第 2 项把这个检查判给**引擎**。本树里没有实现：`src/runtime/**` 没有任何"丢弃 / 丢效应"的检查（在 `src/runtime` 下 `grep` 丢弃/效应词只命中 `src/runtime/control.cpp:94` 的一条无关注释）。

施工方**没有**把"效应仍然运行"写成红用例：那条守卫要绿必须先有引擎检查，而 `src/runtime/**` 属于别的任务。它被如实记录成一条**绿的测量**——`algebra_rules.divergence.library_runs` 的期望就是那 4 行 `7 9 0 0`，条目描述里写明这是对 9.7 的违反；`divergence.cxx_runs` 把 C++ 一侧的 5 行钉在它旁边。引擎检查落地后，前者要改成 5 行并删掉这条记录。

## 选项与代价

- **(A) 由引擎落地 9.7 检查（H243 第 2 项），规则保留**。代价：检查落地之前，今天的 `std::opt::algebra`（= `rules::simplify`）在这个形状上会改变程序的可见行为，而这个窗口没人能关；落地后 `divergence.library_runs` 的期望从 4 行改回 5 行（一处测量记录）。收益：检查对所有规则、所有 pass 一次性成立，规则的写法不用为引擎的欠账让步。
- **(B) 检查落地前，把四条零规则从 `simplify` 里撤掉**（`int_mul_zero`、`int_zero_mul`、`int32_mul_zero`、`int32_zero_mul`；`saturate` 的表可以保留，那里的状态只是"多种写法"，不提取）。代价：库少四条能力；`test/plans/feat/modules/rules.plan.toml` 与 `test/plans/feat/opt/algebra.plan.toml` 里以它们为期望的条目要改（施工方未逐条清点，数量待定）。收益：今天就没有违反 9.7 的路径。
- **(C) 接受现状**：认定"势效应检查是引擎的事，库先跑着"。代价：与 (A) 相同的暴露窗口，且没有任何记录迫使它关闭（只有这条测量）。

## 施工方建议

**(A)**：判词已经把检查判给引擎，撤规则等于按引擎的欠账给"库能做什么"打折；而且这一族检查（右侧丢掉的子项必须纯）对将来每一条规则都适用，只在零规则上绕过去是治标。若发布节奏不允许等引擎，则 **(B)** 是唯一能让今天的 `std::opt::algebra` 不违反 9.7 的选项，代价是两处计划期望的修改。

## 来源

- 测量：`test/cases/opt/algebra_rules/divergence.cml`、`test/plans/feat/opt/algebra_rules.plan.toml`（`divergence.library_runs` / `divergence.cxx_runs` / `divergence.source_runs`）、逐图断言 `test/cases/opt/algebra_rules/pairs.mjs`；被删的法表在这张图上的原样输出记在 `test/plans/feat/opt/golden/algebra_rules.divergence.deleted_table.jgir`（实测与今天参考模式的 dump 逐字节相同）。
- 规则：`stdlib/rules.cml:33-34,76-77`。
- 判词：`docs/design-principles.cn.md:556-559`（9.7）、`docs/design/rewrite/v2.cn.md:111`（§8）、`docs/handover/reviews/questions-2026-10-23.md`（H243 第 2 项）。
- 被删的法表：`e8c290554:src/passes/opt/generic/algebra.cpp`（没有 `x*0`、没有 `x-x`；删除提交 `a510676bb`）。