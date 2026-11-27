# 补 `#op` 覆盖不到的 22 条：分类与结论

来源：`task-228` 34 条红里不属于"遍声明补 `#op`"范围的 22 条（`test/plans/feat/compile/native_declarations.plan.toml`）。
分类口径（四列）：① 用例与其 `.cml` 源 ② 声明有没有算子 ③ 情形归类 ④ 结论（补算子 / 改断言）。

## 总表

| # | 用例名 | 用例源（`test/cases/compile/native/`） | 被测声明 | 有没有算子 | 情形 | 结论 |
|---|---|---|---|---|---|---|
| 1 | native.random_declaration_import | random_declaration_import.cml | `stdlib/random.cml:215 seed` / `:220 next_key`（有 `#op`）、`:52 key`、`:189 shuffle`、`:197 choice`、`:205 sample`、`:224 rand`、`:230 randn`（均**无 `#op`**、有函数体） | **部分有**：`seed`/`next_key` 有 `#op('rand:seed')`/`#op('rand:next_key')`；其余是 Camel 函数 | 混合：2 个真算子 + 6 个普通库函数 | **不补算子**；断言要区分"真算子声明"与"Camel 函数"，见 §结论 A |
| 2 | native.random_wrong_arity | random_arity.cml | 同上（错元数调用） | 同上 | 同上 | 断言保留（错元数必须红） |
| 3 | native.random_wrong_type | random_type.cml | 同上（错类型调用） | 同上 | 同上 | 断言保留 |
| 4 | native.array_declaration_import | array_declaration_import.cml | `stdlib/array.cml:331 argsort`（**有** `#op('array:argsort_i')` 等 8 条）、`:351 sort`、`:73 flatten`、`:89 unique`（**无 `#op`**，有体） | **部分有**：`argsort` 有；其余是 Camel 函数 | 混合 | **不补算子**；见 §结论 A |
| 5 | native.array_wrong_arity | array_arity.cml | 同上 | 同上 | 同上 | 断言保留 |
| 6 | native.array_wrong_type | array_type.cml | 同上 | 同上 | 同上 | 断言保留 |
| 7 | native.array_sort_wrong_arity | array_sort_arity.cml | `stdlib/array.cml:351 sort`（无 `#op`，Camel 体） | **没有**（Camel 函数） | 它根本不是算子 | 断言保留；但**它不该走"native 声明"这条检查**——见 §结论 A |
| 8 | native.array_sort_wrong_type | array_sort_type.cml | 同上 | 同上 | 同上 | 同上 |
| 9 | native.declaration_type_argument | declaration_type_argument.cml | `stdlib/tensor.cml:71 numel`（**无 `#op`**） | 需要查：`numel` 是否 C++ 算子（见 §结论 C） | 待定 | 断言保留（多给类型参数必须红） |
| 10 | native.tensor_show_declaration_import | tensor_show_declaration_import.cml | `stdlib/tensor.cml:351 show`（无 `#op`） | **有**：C++ 侧 `src/stdlib/tensor/ops/catalog.cpp:36 {"show", …}`、kernel `utility_ops.cpp:120 showKernel`、`module.cpp:102 "show"` | 算子本该有且**已有**（登记在模块里） | **不补算子、不改断言**；红是检查路径问题（`#op` 缺 ⇒ 声明名即算子名 `tensor:show`，需执行器认它） |
| 11 | native.tensor_show_wrong_arity | tensor_show_arity.cml | 同上 | 同上 | 同上 | 断言保留 |
| 12 | native.tensor_show_wrong_type | tensor_show_type.cml | 同上 | 同上 | 同上 | 断言保留 |
| 13 | native.tensor_show_empty_array | tensor_show_empty_array.cml | 同上（空数组走第二条规则） | 同上 | 同上 | 断言保留 |
| 14 | native.tensor_show_empty_array_typed | tensor_show_empty_array_typed.cml | 同上（typed 空数组） | 同上 | 同上 | 断言保留 |
| 15 | native.nn_declaration_import | nn_declaration_import.cml | `stdlib/nn.cml:41 save_params`（无 `#op`） | **有**：`src/stdlib/nn/module.cpp:31 "save_params"`、`:89 OperatorTable::declare("nn", {{"save_params", false}})` | 算子本该有且**已有** | **不补算子**；同 #10 的情形 |
| 16 | native.autodiff_declaration_import | autodiff_declaration_import.cml | 用例 import 的是 `laws` 的 `stop_gradient`（**不是** `autodiff` 模块的 `grad`） | **有**：`src/stdlib/laws/operators.cpp:42 "laws:stop_gradient"`、`:33 {"stop_gradient", &stopGradientKernel}` | 算子本该有且**已有** | **不补算子**；同 #10 |
| 17 | native.rule_reader_declaration_import | rule_reader_declaration_import.cml | `stdlib/rewrite.cml:234/240/246/252` 四个 reader | **有 `#op`**：`#op('rewrite:name')`（:233）、`'rewrite:two_way'`（:239）、`'rewrite:needs'`（:245）、`'rewrite:consts'`（:251）；由模块函数表回答（`module.cpp:4717` 区） | 算子本该有且**已有**，且**已带 `#op`** | 这条最像"已修好"：`#op` 已在 corpus 里 ⇒ 剩余问题只是检查路径 |
| 18 | native.rule_reader_wrong_arity | rule_reader_arity.cml | 同上 | 同上 | 同上 | 断言保留 |
| 19 | native.rule_reader_wrong_type | rule_reader_type.cml | 同上 | 同上 | 同上 | 断言保留 |
| 20 | native.graph_constant_import | graph_constant_import.cml | `stdlib/graph.cml:307 constant::<T>`、`:226 constant_bytes`（均无 `#op`） | **有**：`src/stdlib/graph/module.cpp:224 {"constant", true}`、`:196 {"constant_bytes", true}` | 算子本该有且**已有** | **不补算子**；同 #10 |
| 21 | native.graph_constant_wrong_arity | graph_constant_arity.cml | 同上 | 同上 | 同上 | 断言保留 |
| 22 | native.graph_constant_wrong_type | graph_constant_type.cml | 同上 | 同上 | 同上 | 断言保留 |

## §结论 A：不该给"普通库函数"补算子（6+6 条声明）

`random.cml` 的 `key`(:52)/`shuffle`(:189)/`choice`(:197)/`sample`(:205)/`rand`(:224)/`randn`(:230) 与 `array.cml` 的 `sort`(:351)/`flatten`(:73)/`unique`(:89) 都是**有函数体的 Camel 函数**；它们不是算子，没有算子可指。
⇒ 这类声明**不需要 `#op`，也不该有**；`task-228` 里它们红，是因为"整块删掉 `declarationHasOperator` 豁免"后，连有体的函数也被当 native 声明去查算子——**红的是豁免被删，不是缺算子**。
⇒ 结论：**改断言/改实现都不必要，只要豁免保留**；若决策者要"34 条全消"，这 12 条靠恢复豁免即可，不靠补 `#op`。

## §结论 B：算子已在模块里登记、但声明没有 `#op`（4 族 12 条）

`tensor:show`、`nn:save_params`、`laws:stop_gradient`、`graph:constant`/`constant_bytes` —— 四个模块都在 C++ 里登记了算子（出处见总表），声明却不带 `#op`。
⇒ 这类**可以补 `#op`**（写法只有一种：`#op('<模块>:<模块内路径>')`，即 `tensor:show`、`nn:save_params`），补上后声明名与算子名的对应关系是显式的；
⇒ 但**即使不补也不该红**：声明名 `show` 落在模块 `tensor` 里，默认算子名就是 `tensor:show`，检查应能找到它。

## §结论 C：`numel`（#9）待查

`stdlib/tensor.cml:71 numel` 不带 `#op`；本卡只读、未查它的 C++ 侧登记位置，需 `lane-kinds-oper` 在实测时顺带确认（若 `tensor` 模块登记了 `"numel"`，则与 §结论 B 同类）。

## 一句话给决策者

**22 条里没有一条是"缺算子要补"**：12 条是**普通 Camel 函数**（靠豁免，不靠 `#op`），12 条是**算子已登记但声明没写 `#op`**（可补可不补，补了更显式），1 条 `numel` 待查。
⇒ 想让这 22 条全绿，要做的是**恢复"有体函数"的豁免**（`declarationHasOperator` 那条判断）与**修正检查路径**，不是再造 22 个算子。
