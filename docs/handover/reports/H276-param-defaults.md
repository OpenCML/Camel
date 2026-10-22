# 普通形参的默认值与按名字的实参：实现报告（`handover/h276-params`）

这是实现者对本分支所落实内容的说明，不是评审结论。要裁决的问题放在最后一节。

设计依据：`docs/design-principles.cn.md` 4.4 的同一条规则——"形参可以有默认值，实参可以按名字给，普通形参与 `with` 形参同一条规则"。H235 已把它落实到 `with` 形参；本分支把同一套机器复用到普通形参，没有新增第二套。

1. **一套实现，两个参数表**：`evaluateDefaults` / `defaultsOfCallee(callee, isWith)` / `bindArguments(..., isWith)` 各只有一份，按 `isWith` 取 `with` 表或普通表；GCT 侧 `Tree::ParameterDefaults` 同时持有 `with` 与 `norm` 两份（各按自己的形参对齐，`constas` 与 rest 形参也占位）。默认值仍是在声明处求值一次的常量，调用处补一个静态数据节点；`let g = f` 得到的函数值不携带默认值（`function_value` 用例）。
2. **一个权威顺序是声明的顺序**：绑定之后 `normInputs` 的下标就是普通形参的下标，`normInputTypes` 只在绑定之后从节点建一次（`callableValueType`），调用处 `callSiteAtArity` 追加的那个参数在同一循环里；`withInputTypes` 仍只在它原来的位置建一次。写出来的顺序不再出现在任何类型列表里。顺带修好了一处旧的不一致：deferred `with` 键入做 `normTypeAt(i)` 与 `normInputTypes[i]` 的统一时，后者现在也是声明顺序。
3. **语法**：`f(1, b: 2)` 复用 `argumentValues` 产生式（`(keyValuePair | indexValue)`），尖括号里不变。括号里名字之后再写按位置的实参不再让解析失败，而是折成新的 Cst 节点 `StrayPositional`，由调用处报 `ParamArgumentPositionalAfterNamed`（指向那个实参）；`scripts/check-grammar.mjs` 的 `NODE_OF` 因此多一项（`strayPositional: 'indexValue'`），语法参考由 `--write` 重新生成。
4. **诊断（0x1CD–0x1D4）**：`ParamArgumentUnknown`、`ParamArgumentDuplicate`、`ParamArgumentMissing`、`ParamArgumentCallee`、`ParamArgumentPositionalAfterNamed`、`ParamDefaultNotConstant`、`ParamDefaultWrongType`、`ParamAfterDefault`。`docs/diagnostics.md` 用 `node scripts/diagnostics-catalog.mjs --write` 重新生成。旧的 `ParamDataMustBeStatic` 与 "default parameter values" 的 `FeatureNotSupported` 不再产生，编码保留不复用。
5. **证据**：`test/plans/feat/compile/param_defaults.plan.toml` 15 项——四个后端一致的取值用例（省略、按位置覆盖、按名字跳过中间形参、管道写法 `1->three(c: 3)`、`1->add`），`one_constant` 用 `std::gir` 断言两个调用只读一个 `"once"` 的 DATA 节点、两条边，三个拒绝各按行号定位，`function_value` 与 `struct_not_constant` 各钉一条限制。
6. **未改标准库**：`stdlib/array.cml`、`stdlib/rules.cml`、`stdlib/types.cml` 一行未动。

## 请裁决的问题

1. **`array.cml` 的 `take` / `drop`**：它们是 `with <n: int>`，每次调用都得写 `n`，写成 `with <n: int = 1>` 会更好读。这属于 `with` 默认值的收尾，本分支没动；要不要改、由谁改。
2. **按名字的实参遇到重载集与 rest 调用**：`signatureForArgumentContext` 对重载集与算子组返回空，此时按名字给实参报 `ParamArgumentCallee`；rest 函数按位置展开（`inlineRestCall`），不参与绑定，按名字给实参报同一条。两条都按"名字要在调用处解析"解释，但重载集里按名字选候选是否该支持，留给裁决者。
3. **结构体 / 数组字面量做默认值**：它们在当前实现里不是静态数据（在调用处构造），所以 `p: Point = { x: 3, y: 4 }` 报 `ParamDefaultNotConstant`，`struct_not_constant` 用例钉住了这一点。这是一条实现限制，不是本分支引入的规则；要不要支持属于折常量的范围。