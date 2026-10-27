# 含洞复合字面量：嵌套字面量的占位符、与声明位回退里那一次 `coerceTo`

裁定 `a20253e2c` 的 `holes-in-non-literal-positions`（上一页 `docs/handover/questions/holes-in-non-literal-positions.md`；已落地的那条规则在 `docs/handover/reviews/questions-2026-10-26-late.md:23-26`）里有两条**不能照字面执行**。已落地的部分在 `2f7bc235d`：结构体字段的洞与数组/元组元素走同一条路（四位置同报 `ElementTypeMismatch 0x03000012`，带字段名与字段型），声明位的报点从"洞的绑定处"挪到元素。

按 `AGENTS.md`：设计原则与裁定冲突时**设计原则优先**，冲突记为缺陷或待办。所以下面两条现在不是能直接照字面执行的裁定：两句各做一个隔离实验（只删这一处，其余保持 `2f7bc235d`，完整快层 `-Plan "smoke,test/plans/feat"`），都红。请在这两条上给出取舍。

## 1. 嵌套字面量的占位符：该由 GCT 带上自己的类型，还是不进 `CompositeData::holes()`？

**只删 `wroteAnElement`（`src/compile/gir/builder_control.cpp:988-995`；数组闸门 `:1025-1031`、元组闸门 `:1126-1136`）⇒ 完整快层 `total 6971 / fail 30`：**（先前口头报过的 37 是这两句**同时**删的合测量，其中 2 条来自当时的临时用例、4 条是裁定自己要改的钉住读数与生成物；本页两个数字是各自隔离删的值。）

- `nested_literals` 1（正例）：`test/cases/compile/return_type/nested_literals.cml:11:14` 与 `:11:22`（`return ((a, b), (b, a))`），逐字 `Cannot insert data of type 'int' into a(n) tuple with element type '(int, float)'.`；同文件 `:15:14`、`:15:22`、`:19:15`（`:19:15` 是 `'int'` 对 `'((int, int), int)'`）。
- `rules.*` 12：`test/cases/modules/rules/schedulers.cml:63` 的 `p->seq<[[bump_one], [bump_ten]], false>`（`stdlib/rules.cml:430` 的文档示例同形）逐字 `Cannot insert data of type 'Rule' into a(n) array with element type 'Rule[]'.`
- `surface.member_chain` 4（nvm/fvm/jit/prl）、`si_tuple_return` 1、`collfuzz.quick` 1、`entering.positions` 10（见第 2 条，两次实验都红）、`context.argument_name` 1（钉的 `12:17` 变成指洞的 `ElementTypeMismatch`、列 `19`；`test/plans/feat/compile/literal_context.plan.toml:599`，这正是裁定自己要改的那条读数，不算缺陷）。

**机制**（都在 GCT 侧，本轮车道没动）：嵌套字面量在 GIR 里是外层字面量的一个"洞"。`src/compile/gct/builder.cpp:4403` 把每个算出来的元素（含嵌套字面量）包成 `RefData` 占位符（位置全空时走 `:3967` 的 `allComputed` 分支，占位符建在 `:3968`；工厂在 `:250` 的 `makeRefData`）；GIR 在 `src/compile/gir/builder_expr.cpp:136` 用占位符的名字找节点、`:138` 取**那个节点的类型**当洞的类型。那个节点是嵌套字面量的构建节点，类型是**叶子**：`((a, b), (b, a))` 的元素 0 记成 `int`，而位置元素型是 `(int, float)`，于是 `builder_control.cpp:1028`/`:1133` 的 `assignableFrom` 必然判否。`wroteAnElement` 今天正好挡住这一类。

- **问**：给占位符带上自己的类型（构建节点/`RefData` 上记下嵌套字面量的类型，GIR 读它），还是让嵌套字面量不进 `CompositeData::holes()`（`include/camel/core/data/composite/array.h:38`、`tuple.h:37` 的 `holeIndices_`，即 `builder.cpp:4403` 不包成占位符、按"写下的元素"放进字面量）？两条都要动 `src/compile/gct/**` 与 `include/camel/core/data/**`。

## 2. 声明位回退里那一次 `coerceTo`：要不要区分"进入转换"与"数值收窄"？

**只删声明位回退的 `coerceTo`（`src/compile/gir/builder_expr.cpp:187-191`）⇒ 完整快层 `total 6971 / fail 11`：**`entering.positions` 4 后端 + 6 变体共 10 条（`test/plans/feat/consistency/entering.plan.toml:23`），加 `runner.backend_matrix` 1 条（测试器自测按 `--changed` 重跑了同一批）。

`test/cases/consistency/entering/positions.cml` 是 golden 全绿的正例，逐字：

- `:64:15`（`let ys = [4, null, 6]`）：`Cannot insert data of type 'int' into a(n) array with element type 'int?'.`
- `:66:15`（`let zs = [v, null, v + 1]`）：同一条。
- 另一面，删 `wroteAnElement` 时是 `:68:47`（`println(..., total([7, null]), ...)`）报同一条——"数组元素进 `T?`"这一路也在那道闸门后面。

`docs/design-principles.cn.md:209` 是规范句："**值进入 `T?` 或 `T!` 的位置时自动装入**，只要目标类型已知（实参、带类型的 `let`、**数组元素**、分支的两条臂）"；`:338` 把"数组字面量的元素、结构体字面量的字段"列为有期望类型的位置。删掉这一次 `coerceTo` 就是把数组元素这条路的进入转换一并免掉，正例变成错误。

- **问**：要不要只在这一路上免掉转换（即这一处 `coerceTo` 只放行 `T?`/`T!` 的进入，不放行 `int`→`f32` 这类数值收窄），保留 3.8 的进入转换？若不要，`entering.positions` 这条 golden 与 3.8 的"数组元素"要一起改口径——那是改语义，得你点头。