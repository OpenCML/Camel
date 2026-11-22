# `with` 形参的默认值与按名字的实参（H235 的两项，handover 分支 `handover/h263-withargs`）

设计依据：`docs/design-principles.cn.md` 4.4（"形参可以有默认值，实参可以按名字给……默认值是编译期常量表达式，在调用处补入图中；按名字的实参写在按位置的之后；有默认值的形参之后的形参也必须有默认值"）。普通形参的默认值仍不实现（`FeatureNotSupported`），默认值只对 `with` 形参生效。

1. **默认值属于声明，不属于类型**：`with <rounds: int = 8>` 在声明处求值一次（模块顶层函数在 DECL、局部函数在函数体建立时、泛型在注册模板时），存进按声明图（或模板）索引的旁表；调用处漏掉的形参补一个该常量的静态数据节点。因此 `let g = f` 得到的函数值不携带默认值，`f<4, true>` 这样的部分应用只绑定写出来的实参。默认值不能读同一个声明的 `with` 形参（那不是编译期常量）。
2. **按名字的实参在调用处解析**：`f<[step], rounds: 8>` 先按名字归位，再按形参顺序交给类型解析；名字必须属于给单一签名的被调者。语法、AST 与调用处的 `name: value` 写法沿用已有的 `parseAngledValues` / `NamedDataLoad`，只改了 `with` 子句与调用的类型实参列表，没有碰 `when` 的产生式。
3. **诊断**：`0x1B7` `WithArgumentUnknown`（名字不是该函数的 `with` 形参）、`0x1B8` `WithArgumentDuplicate`（同一形参给两次）、`0x1B9` `WithArgumentMissing`（漏掉没有默认值的形参）、`0x1BA` `WithDefaultNotConstant`、`0x1BB` `WithDefaultWrongType`，另有 `0x1BC` `WithParameterAfterDefault`、`0x1BD` `WithArgumentCallee`（按名字的实参遇到函数值/重载集，无从知道名字属于哪个声明）、`0x1BE` `WithArgumentInValue`（按名字的实参用在函数值里）。
4. **已知限制**：默认值只在 `with` 绑定被直接调用消费时补入；函数值、部分应用、经变量调用都不补（与"默认值不属于类型"一致）。按名字的实参要求被调者只有单一已知签名；算子重载集不解析名字。
5. **未改 `stdlib/rules.cml`**：该文件与 `test/cases/modules/rules/schedulers.cml` 由 `handover/h253-rules` 分支改写（`strict` 已改为 `with <stages: Rule[][], strict: bool>`）。把 `strict` 写成 `with <stages: Rule[][], strict: bool = false>`、并删掉调用点只传 `false` 的实参，属于那个分支的收尾；本分支不动它，免得两处同时改同一批调用点。落地后 `test/plans/feat/modules/rules.plan.toml` 的 13 个调度期望不应变化（默认值就是现在显式传的 `false`）。
6. **证据（测试能看见的）**：`test/plans/feat/compile/with_defaults.plan.toml` 的 `one_constant` 用 `std::gir` 打印两次调用 `bump` 的图，断言只有一个 `4242` 的 DATA 节点、有两条边从它出发、以及两个调用节点——这就是"在声明处求值一次"：折出来的常量是声明的一个静态节点，调用只读它。`instances` 让 `tag` 以 int、string、float 实例化三次，各自取默认值 3（`3:7`、`3:a`、`3:2.5`），并且 `tag::<int><9>` 仍可按位置覆盖——每个实例的 `with` 端口是自己的，默认值是声明的同一个常量。
7. **默认值的类型不能依赖类型参数**：`with <v: T = 1>` 在注册模板处就被拒绝（`WithDefaultWrongType`，`type_parameter` 用例），所以一个声明的默认值对所有实例是同一个值；"每个实例各有一份"在类型上没有必要，也不该由一个读 `T` 的默认值来产生。
