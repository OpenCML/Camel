# 含洞复合字面量：一个元素都没写时要不要同判；结构体字段的洞要不要走同一条路

2026-10-26 裁定（`docs/handover/reviews/questions-2026-10-26-late.md:21` 的 `holed-composite-literals-do-not-take-the-expected-type`，"洞只检查、不转换"）落地后**实测剩下的两个口径**。已落地的部分：位置给字面量写下的元素它的元素类型，计算出来的元素对着这个类型检查，不是的地方在**那个元素**上报 `SemanticDiag::ElementTypeMismatch`（`src/compile/gir/builder_control.cpp:969` 的 `rejectHole`；数组在 `:1020`/`:1030`、元组在 `:1110`/`:1123`），于是参数位、声明位、箭头接收位三个位置报同一条诊断（用例 `test/cases/compile/literals/context_composite_holes_positions_mismatch.cml`，钉在 `test/plans/feat/compile/literal_context.plan.toml:954-972`）。下面两条都不改判、只提问，`src/**` 一行未动。

## 1. 一个元素都没写的字面量（`[n]`）：该不该也报指洞的 `ElementTypeMismatch`

分界今天落在"字面量自己写下了元素没有"（`src/compile/gir/builder_control.cpp:983` 的 `wroteAnElement`）：一个都没写时 `rejectHole` 不走，字面量不进入位置（`src/compile/gir/builder_control.cpp:779-795` 的 `compositeLiteralEntersIn` 在 `:792` 对没有解析完的字面量直接回答"不进入"，判断本体是 `:432` 的 `compositeEntersIn`，由 `:539` 转来；实参位与接收位在 `src/compile/gir/builder_operators.cpp:2466` 问它），于是找不到匹配的函数；只有声明位会走到回退（`src/compile/gir/builder_expr.cpp:181-199`，其中 `:186` 还会用 `coerceTo` 把洞**转换**一次）才报 `ElementTypeMismatch`。

逐字读数（`camel check -E short`，`func first(xs: f32[]): f32 { return xs[0] }`、`n: int`；位置取自本车道的临时探针 `_probe_holes_question.cml`，量完即删，行号按它）：
- 实参位 `return first([n])`：`SemanticDiag::NoMatchingFunction` `0x0300000F`（`7:45`），逐字 `No matching function found for argument types: <> (int[]).` + `first: (xs: f32[]) => f32 pure`。既有用例 `test/cases/compile/literals/context_argument_name.cml:12` 把它钉在 `12:17`（`test/plans/feat/compile/literal_context.plan.toml:599-607`，`(int[])` 与候选名都在断言里）。
- 接收位 `return [n]->first`：同一条 `NoMatchingFunction` `0x0300000F`（`14:41`）。
- 声明位 `let xs: f32[] = [n]`：`SemanticDiag::ElementTypeMismatch` `0x03000012`，逐字 `Cannot insert data of type 'int' into a(n) array with element type 'f32'.` —— 但报在**形参 `n`** 上（`9:19`），不在写这个元素的那一行。
- 对照，同一个洞 `n` 只要旁边写了字面量元素（`return first([1.0, n])`）：`ElementTypeMismatch` `0x03000012`，报在**元素**上（`16:54`），消息逐字同上。

- **问题**：按"洞只检查不转换"，`[n]` 的洞是不是也该报指洞的 `ElementTypeMismatch`？若该，三处都报，`context.argument_name` 那条既有断言（连同 desc）要一起改，并且声明位还要把报点从形参挪到元素（今天 `9:19`）；若不该，"没有写下元素时字面量不进入位置"就该写成一条明确规则，并让声明位也照它报——今天这三处两处 `NoMatchingFunction`、一处 `ElementTypeMismatch`，仍不一致。
- **我们的看法**：两个洞是同一种东西，`:1030` 判的是同一个条件（`elemType->assignableFrom(hole)`），差别只在字面量有没有写下一个元素；我们按文档里已有的措辞（不进入的字面量就是找不到匹配的函数）保住了 `pick([n])` 的原读数，没有替你改那条既有断言。倾向统一到 `ElementTypeMismatch`，报点也统一到元素，代价是上面那几处读数一起改。

## 2. 结构体字段的洞：要不要走同一条路、同一条码

结构体分支不走 `rejectHole`：字段上的洞只回答"不是这个类型"，字面量退回自己的类型，由所在位置报（`src/compile/gir/builder_control.cpp:1074-1077`）。

逐字读数（`camel check -E short`，`type P = { x: f32, y: f32 }`、`func norm(p: P): f32`，洞是 `n: float`；行号同样按那个临时探针）：
- 实参位 `norm({ x: n, y: 1.0 })`：`NoMatchingFunction` `0x0300000F`（`22:48`），`No matching function found for argument types: <> ({ x: float, y: float }).` + `norm: (p: P) => f32`。
- 接收位 `{ x: n, y: 1.0 }->norm`：同上 `NoMatchingFunction` `0x0300000F`（`29:45`）。
- 声明位 `let p: P = { x: n, y: 1.0 }`：`SemanticDiag::DeclaredTypeMismatch` `0x03000110`（`25:5`），`This value has type '{ x: float, y: float }', which is not the declared type 'P'.`
- 返回位（你已记下的读数）：`SemanticDiag::ReturnTypeMismatch` `0x03000011`（`15:35`），用例 `test/cases/compile/literals/context_composite_holes_mismatch.cml:15`，钉在 `test/plans/feat/compile/literal_context.plan.toml:914-925`（同一文件的另一处返回位给 `MB`，消息里带 `'{ n: float, t: (float, float) }'` 与 `declared return type 'MA'`）。
- 字段本身是数组时一样：`{ xs: n2 }`（`n2: float[]`）落在字段 `f32[]` 上，实参位 `NoMatchingFunction`，实参类型写成 `({ xs: float[] })`。

- **问题**：结构体字段的洞要不要也统一到数组/元组那条路（在字段处报 `ElementTypeMismatch` 并带上字段类型）？若统一，牵动的是 `literal_context.plan.toml:914-925`（`context.composite_holes_mismatch` 现在钉 `ReturnTypeMismatch`、`15:35` 与两段消息文本）、以及一切拿"结构体字段的洞"当找不到匹配的函数的用例；`docs/diagnostics.md` 里 `ElementTypeMismatch` 的示例也会被生成器重选。不牵动 `context.struct_argument_shape`——那是字形不对（少写字段），仍该是找不到匹配的函数。
- **我们没做**的原因（等你的话）：那条既有用例是你记下的读数，裁定点名的三个位置是参数位、声明位、箭头接收位，示例只有数组；结构体字段要不要同判超出这三处，所以只问不判，`src/compile/gir/builder_control.cpp:1074-1077` 一行未动。若你要统一，请顺带说一句结构体这一支要不要也认"没有写下元素"那条分界（`{ x: n }` 这种字段全是洞的字面量）。

两条各一个字就能收：**统一** 还是 **不统一**（以及第 1 条要不要连报点一起挪到元素）。