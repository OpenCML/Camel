# 遍只能看图：七处还在读语法树，缺的四个字段在同一处

作者（负责人）2026-11-22 `8f54f5f00` 定下：「把源码里的信息编进图是编译器的职责；之后运行的遍——变换、重写、优化、导出、执行——只从图上取得一切必要信息，不读语法树、不调解析或编译。规则也一样：编译器把规则编成图上的值，重写核心只认这种编译好的规则，不认语法树里的规则头。」

本页是一次全树只读审计的结果。判据是：该文件属于「编译之后运行的遍」（`modules/*/` 的变换/重写/优化/导出/执行，或 `src/passes/`），且它 `#include` 了语法树/解析器的头或调了它们的入口，且那引用是为自己所用而不是传递引入。`src/parse/` 与 `src/compile/` 读树是职责所在，不算。

## 七处真违规

| 文件 | 读的是什么 |
| --- | --- |
| `modules/transforms/module.cpp:48,68`（apply `:206`，注册 `:365`） | `rulesOf("rulesets.jvp")` |
| `modules/transforms/jvp.cpp:25,187,189,192` | `core::forwardRulesOf`/`forwardRuleOf` → `src/core/forward.cpp:221` → `rulesIn` `:200` → `lookupRules` `:207` |
| `modules/transforms/vmap.cpp:24,89,126,166,403,447,687` | `vmapRuleOf`/`vmapRulesOf`/`rhsCalleeOf`/`lookupRule` |
| `modules/transforms/rule_expand.h:7,160`、`rule_expand.cpp:14,228,257-258` | `chooseRuleAmong` 的形参类型就是 `parse::ast::RuleMatch` |
| `modules/transforms/transpose/rules.cpp:200`（`:135,:178,:201` 调用） | 传递 include（`core/forward.h`）+ 自己写 `RuleMatch` 类型，调 `core::transposeRulesOf` |
| `modules/transforms/transpose/walk.cpp:2223,2228` | 传递 include；`transposeRulesOf` + `RuleLookup` |
| `src/passes/trans/rulesets/rulesets.cpp:6,47,129,130,136`（注册 `src/execute/pass.cpp:661`） | `lookupRule`/`ruleFamilies`/`rulesOf`/`rhsCalleeOf` |

## 为什么它们只能回去读树：图上的四个字段不在

四项**全部**缺在 `GenericRuleEntry`/`Instance`（`src/compile/gir/rule_templates.h:98-125`）：

1. **规则名与 `head`/`arity`**。键→规则名本该来自 `compile::gir::genericRuleTable`（`src/compile/gir/rule_templates.cpp:122-125`），但 `head`/`arity` 不在 entry 上；`lhsOperatorUri`（`rule_templates.h:86`）只对纯算子形状的左式有值（`rule_templates.cpp:179-186`）。`src/compile/gir/program.cpp:1061` **已经持有** `RuleHead.head`/`arity`，没落到表上。
2. **`rhs callee` 名**。图上只有 `instance.rhs` 这张图（`rule_templates.h:74`），名没有字段。`RuleHead.rhsCallee` 落在同一处即可。
3. **左式绑定的变量与闭包槽数**（`modules/transforms/module.cpp:169-172,181-192`）。`GenericRuleEntry`/`Instance` 无此字段。`RuleHead.variables` 同上。
4. **「族」列表，以及「族未加载」与「族没有规则」的区分**（`rulesets.cpp:129`、`jvp.cpp:192`、`walk.cpp:2228`）。族名在注册时被剥掉了限定符（`src/compile/gir/builder_generics.cpp:597-604`），所以 `entry->name` 上没有族。

**这四项是同一个动作：把 `RuleHead` 已经有的东西落到 `GenericRuleEntry` 上。** 不是四个设计问题，是一次补字段。加完之后这七处才有地方可取。

## 已经做对的一半

`modules/rewrite/` **没有任何路径直接读规则头**：规则从编译好的 `Rule` 值取（参数里的 `Rule[]`，见 `include/camel/runtime/rules.h:29-55`）。所以「重写核心只认编译好的规则」这一半原则已经成立，剩的是读**规则头里那些还没编进图的东西**。

另排除两条最像的：`modules/rewrite/module.cpp:4360` 与 `modules/tiles/src/module.cpp:927` 的 `declareResources` —— 两个模块全树无 `ast/`、`parse/`、`syntax/`、`lexer`、`parser` 的 include，也无 `parse::` 调用，它们只写 `OperatorTable` 的资源表。

## 本页的建议（未派工）

先做**补字段**（一项），再看七处能不能都改成从图上取 —— 不要七处分别改。判据是：补完字段后，`modules/transforms/` 与 `src/passes/trans/rulesets/` 里不再出现 `rule_head.h` 的 include，且各 plan 读数不变。
## 作者裁定（2026-11-22 02:05，`501b95781`）

**不按本页建议补字段**。把 `RuleHead` 的四项搬到 `GenericRuleEntry` 上，七处就改成读编译器的一张表 —— 仍然不是「从图上取」，只是换了一处旁表。

目标形状在设计稿 §3：求导、转置、批处理的规则挂在**算子记录**上，是编译好的规则值（与 `rewrite` 用的 `Rule` 同一种），由规则集模块加载时登记；变换按「这个算子的 jvp 规则」去算子记录上取，不按名字查族、不读规则头。「族未加载」就是「这个模块没被导入」，由模块系统回答。

这属于重构的「算子记录」一步，等讨论定了一起做。本页作为那一步的现状读数。

**本页的建议因此作废**，保留它是因为七处的**位置**与**缺什么**仍然有效：缺的不是四个字段，而是算子记录上还没有规则值。