# 守卫有效性审计（不是今晚的完工报告）

**这份页只回答一个问题**：今晚新加的那些断言与守卫，**拿掉被守的那一分支，红的正好是它吗**。不是「跑过了」，是「它红过，而且只它红」。
做法一律是：在临时工作树 `58dcb949c` 上改一处、构建一次、跑一个计划、原样复原、不提交。六次变更，六条读数。

## 表

| 断言 | 拿掉了什么 | 红的条数 | 是否「恰好」 | 结论 |
|---|---|---|---|---|
| `order_edge_on_a_pure_node_is_refused`（`feat/opt/edges`） | `graph_edges.cpp:347` 的 `nodeTakesNoOrder` → `bad("is on a pure node…")` | 1 / 14 | 是 | **有效** |
| `rank_zero_slot_array_element` ×4（`_fvm/_jit/_tf`） | `ArrayDataFactory::add` 的元素转换（`src/core/data/composite/array.cpp:26`） | 4 / 84 | 恰好那 4 条 | **部分有效：守元素转换一族** |
| `rank_zero_slot_nested_field` ×4 | `convertStructValue` 里 rank-0 的 `coerceTo`（`builder_control.cpp:632`） | 4 / 84 | 恰好那 4 条 | **部分有效：守嵌套字段一族** |
| `argument_rule`（接受那条） | `declarationOf` 的 maker 分支（`builder_tags.cpp:399`，`__rule_generic` / `ruleMakerCalled`） | 1 / 48 | 是 | **有效** |
| （同族） | `declarationOf` 把 `Rule` 折回 `Value`（`builder_tags.cpp:395`，FUNC 的 `ruleBinding` 分支） | **0 / 48** | 什么都没红 | **空转** |
| `argmax_axis_scope_reasons`（gpu pin） | `array.cpp:326` 注册名改成 `argmax_axis_x` | 1 / 106 | 是（一条，不是红一片） | **有效** |

读数原文：

```
[FAIL] order_edge_on_a_pure_node_is_refused                  68.05 ms
[SUMMARY] total: 14, pass: 13, fail: 1
```
```
[FAIL] rank_zero_slot_array_element                          65.00 ms
[FAIL] rank_zero_slot_array_element_fvm                      62.71 ms
[FAIL] rank_zero_slot_array_element_jit                      64.83 ms
[FAIL] rank_zero_slot_array_element_tf                       71.93 ms
[SUMMARY] total: 84, pass: 80, fail: 4
```
```
[FAIL] rank_zero_slot_nested_field                           1.25 s
[FAIL] rank_zero_slot_nested_field_fvm                       2.26 s
[FAIL] rank_zero_slot_nested_field_jit                       3.26 s
[FAIL] rank_zero_slot_nested_field_tf                        4.27 s
[SUMMARY] total: 84, pass: 80, fail: 4
```
```
[FAIL] argument_rule                                         51.06 ms
[SUMMARY] total: 48, pass: 47, fail: 1
```
```
[SUMMARY] total: 48, pass: 48, fail: 0          ← Rule 折回 Value，无人察觉
```
```
[FAIL] argmax_axis_scope_reasons                             91.28 ms
[SUMMARY] total: 106, pass: 100, fail: 1, skip: 5
```

## 结论一：`declarationOf` 的 FUNC 分支是**死代码**（今天，已删）

**已按裁定删除**，原处留了一句注释：一条带 `with` 参数的 `rule` 到不了这里，因为值用法已被改写成 `name()` 调用；将来若真有输入能走到这里，变的是那条改写，不是这里该复活。删除后 `feat/compile/tags` 仍是 `49 / 49 pass`。

`declarationOf`（`builder_tags.cpp:382`）对两条路给出 `Rule`：

1. 声明是 FUNC 且带 `ruleBinding`（`:394`）；
2. 声明是 NREF，且它的初始化表达式调的是 `__rule` / `__rule_generic`（`:398`）。

**第 2 条有守卫**（maker 分支关掉 → `argument_rule` 恰好一红）。**第 1 条没有，而且不是「缺一条用例」那么简单：它今天从源码不可达。**
证据三件：

- 折回 `Value` → 48 条全绿；
- 补写一条**专门走这条路的用例**（`test/cases/compile/tags/rule_binding_argument.cml`：`with <k: float>` 的 `rule`，名字作标签实参）——**它接受（输出 `6.0`），而在折回的状态下也接受（49/49 绿）**；
- 原因在 `builder_expr.cpp:774-777`：带 `ruleBinding` 的名字**一旦被当作值使用**，就被 `ruleBindingValueOf` 改写成调用 `name()` 的 NREF（`builder_calls.cpp:645`）。等 `declarationOf` 看到它时它已经是 NREF，走的是第 2 条路。

所以 FUNC 分支**要么是给将来某种「直接摸到 FUNC 节点」的输入留的，要么就该删**——这是裁定级别的事，不是我这里改。**新加的那条用例保留**：它锁住的是「带 `with` 参数的 rule 可以作标签实参」这个真行为；但**它不是这条分支的守卫**，页里如实记着。

## 结论二：四个拒绝用例由**报告点**守，不由分类分支守（缺口已查明）

`err_tag_argument` / `err_type_argument` / `err_rule_value_argument` / `err_rule_nothing_argument` 在 `declarationOf` 两处分类变更下**都保持绿**——它们不由分类分支守。
再查一步：把报告点 `reportNotValue`（`builder_tags.cpp:430`，`SemanticDiag::TagArgumentNotValue` 的唯一抛点）静默掉，**红的恰好是它们四条**：

```
[FAIL] argument_tag                                          74.27 ms
[FAIL] argument_type                                         58.83 ms
[FAIL] argument_value_still_refused                          52.00 ms
[FAIL] argument_nothing_refused                              61.90 ms
[SUMMARY] total: 49, pass: 45, fail: 4
```

**所以它们守的是「这条名字不是值时要报出来」这件事，不是「它被归成哪一类」。** 分类只影响**文案**（`declarationText` 的四个分支），
文案的正确性今天没有用例守——这是留下的第二个缺口，比第一个小，但要记着：**把 `Rule` 折回 `Value` 时，`argument_value_still_refused` 仍绿，因为它只检查拒绝与文案里「a declaration of this program that is not one function」这句**——它恰好落在那格。
原文那句话（「四支里任一支放宽 → 对应用例红」）**没有实测支撑，不进任何页**。

## 结论三：rank 一族的口径

两个守卫**各守一族**：元素转换 → `rank_zero_slot_array_element`（4 条）；`convertStructValue` 的 `coerceTo` → `rank_zero_slot_nested_field`（4 条）。
`field` / `var` / `parameter` / `let` / `return` / `tuple_element` 在这两次变更下**都绿**，由它们各自的 `coerceTo` 调用点守——**这是推断，不是实测**。（整族是 8 种槽位 × 4 个后端 = 32 条。）