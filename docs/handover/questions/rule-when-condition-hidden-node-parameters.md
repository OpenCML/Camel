# 问题：规则条件的形参表里，`const` 形参的隐藏节点参数是无条件存在，还是只在写了 `node_of` 时存在

事实与读数，不含判断。开发线 tip `0a1a509f5`。

## 1. 读数

三条用例在 `feat/mm/gc_poison_sweep` 的 `poison:` 副本里红，去掉 `CAMEL_GC_POISON=1` 的同一份用例绿：

```
camel test/cases/modules/rules/check_library.cml std::nvm                       -> exit 0
CAMEL_GC_POISON=1 camel test/cases/modules/rules/check_library.cml std::nvm     -> exit 1，三次三次如此
```

失败输出（逐字，`camel-db` 之外用 `camel` 亦同）：

```
error[RuntimeDiag::PassInternalError]: Pass 'std::nvm' failed with an internal error: Rule 'int_combine<i32>': parameter 4 of its condition is the node of a `const` parameter (`node_of`), but the left side binds no node for it.
```

另两条：`check_reduced_element_type.cml` 报 `float_combine<float>` 的 parameter 4；`check_every_rule.cml` 报 `int_combine<i32>` 的 parameter 4。三次运行的合取：`test/plans/feat/mm/gc_poison_sweep.plan.toml` 整跑 2755 用例，红 3 条，即上述三条；`test/plans/feat/modules/rules.plan.toml` 整跑（不设 poison）140 用例全绿。

## 2. 抛出点

`modules/rewrite/module.cpp`，`callWhen` 绑条件形参的循环里，消息在 281–292 行，判据是
`constParamOfNode(i) == SIZE_MAX || !constantOf(constParam)`：前者是「这个位置没有 `const` 形参」，
后者是「有 `const` 形参，但左值没有给它绑节点」。`Match::constant`（`src/runtime/writings.cpp:1278`）
无越界检查地读 `vars[var]`。

## 3. 两条说法

代码注释（`modules/rewrite/module.cpp:187-193`）：条件的形参是规则的形参，**最后的形参是 `const` 形参的隐藏节点参数，即 `node_of(c)` 在构建期被改写成的那些**；左值不用的形参不命名节点，读它就出错。

设计文档（`docs/design/rewrite/v2.cn.md:12`）：`const c` 在 `when` 里是常量的值，**需要节点本身（读事实、形状）时写 `node_of(c)`**。

## 4. 两边的事实

- 报错的 `int_combine`（`stdlib/rules.cml:71`）条件是 `is_integer::<T>()`；`float_combine`（`:190`）是 `is_float::<T>() && combineSameSign(c1, c2)`。**两条都不写 `node_of`**。
- 写了 `node_of` 的规则在别的用例里：`test/cases/modules/rules/node_of.cml:56`、`analysis_parts.cml:36`、`parts_order.cml:51`，它们不是这次红的三条。
- 条件形参表由 `GenericRuleInstance::whenArity`（`include/camel/compile/gir/rule_templates.h:61-62`「条件形参的个数，即其节点表的 arity」）与 `RuleObject::when()->graph()->funcType()->normTypesCount()`（普通规则）给出，`callWhen` 按它循环。
- 三条红的报数都是 **parameter 4**：按 `own = 3`（`x, c1, c2`）算，4 是第一个隐藏节点参数的位置。

## 5. 要问的

1. 一条规则的条件形参表，是不是**总是**「规则自己的形参 + 每个 `const` 形参一个隐藏节点参数」，还是只在条件里写了 `node_of` 的那些 `const` 形参才各有一个？
2. 条件要一个左值没绑节点的 `const` 形参时，正确的语义是什么：整趟失败（今天是 `RuntimeDiag::PassInternalError`，见 §1 的消息），还是该条件在这条匹配上为假？`include/camel/core/error/runtime.h:100-107` 对同类取值的注释是后者——"条件里的事实查询找不到时，条件为假（这条规则在此不适用），而不是遍失败"。
3. 这两问是否已经在别处决定过？本文件只报读数，没有找全先例。