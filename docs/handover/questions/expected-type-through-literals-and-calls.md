# 期望类型穿过"复合字面量的洞"与调用点：`gpu.tiles_transfer` 那 1 个红要改哪一层

Author: 车道 `handover/h412-nested-literal`（`D:\Projects\Camel-wt\h412-nested-literal`），2026-10-24。Status: open — 需要负责人选一条路（(a) 语言语义 还是 (b) 张量模块），并给"调用点期望类型是否给类型变量/开放元素类型定值"这一句裁定。测量在 Linux 主机（`88zjwei`）上、`origin/develop`（`a6f4cfbcf`）与本车道改动同一构建窗口内完成，读数走 `camel-remote.ps1` 与计划运行器。

## 数字先

- 裁定（`questions-2026-10-23.md` 第 5 条）说 `tree_training.cml:8` 是"结构体字面量里嵌套的元组/数组字段拿不到声明字段的元素类型"。**那一半我落地了**：三处嵌套在我改之前逐字是红的，改后全绿（见下）。
- **`test/plans/gpu/tiles_transfer.plan.toml` 仍然红，`total 3, pass 2, fail 1`，诊断逐字不变**：`tree_training.cml:8` 的三个字段都是**调用**（`reshape`/`zeros`），不是写下的字面量。
- 该用例**不需要任何嵌套就能复现**：`func f1(): Tensor<f32> { return reshape([1.0, 2.0], [2]) }` → `Return type 'Tensor<float64, [2]>' does not match the function's declared return type 'Tensor<float32>'.`；`let t: Tensor<f32> = reshape([1.0, 2.0], [2])` → `SemanticDiag::DeclaredTypeMismatch`。所以裁定的**归因**（"嵌套字段"）对这个用例不成立，**要的结局**（该用例编译）需要另一层改动。

## 我落地的那半（已提交）

`src/compile/gir/builder_control.cpp` 新增 `Builder::compositeWithHolesInType`，`src/compile/gir/builder_expr.cpp` 的 `visitDataNode` 在**未解析复合字面量**（有计算元素 → FILL）分支里先问一次 `expectedValueType_`：位置的形状（同种、字段名同、定长一致）与每个**计算元素**的类型都进得去时，字面量取**位置自己的类型**（含具名结构体，5.2），写下的元素按 `literalElementInType` 从**写下的精确值**转换一次；否则完全保持旧行为。

根因（哪一步没带上期望类型）：`literalDataInType` / `compositeDataInType`（唯二会按位置改写字面量的函数）都以 `CompositeData::resolved()` 为前提，所以**只要有一个计算元素**，整个字面量就走 FILL 分支；该分支自己算类型——数组取"计算元素类型的 join"，结构体取"元素统一出来的类型"——**从不读位置类型**。

实测（改前 / 改后，同一命令）：`{ n: x, t: (1.0, 2.0) }`（`x: int`，字段 `(f32, f32)`）改前 `Return type '{ n: int, t: (float, float) }' does not match the function's declared return type 'MA'.`，改后编译并打印 `1.00000012`（f32 中点之上那个值，`1.0` 表示按 double 舍入过、`1.00000006` 表示 float64）；定长数组字段、开秩数组字段、元组里的数组三种同样。回归用例 `context.composite_holes` / `context.composite_holes_mismatch`（`test/plans/feat/compile/literal_context.plan.toml`）。

## 剩下那一半为什么需要裁定

`reshape([1.0, 2.0], [2])` 的元素类型**只**由实参决定：`modules/tensor/ops/registry.cpp` 的 `OpDefResolver::resolveWith` 用实参类型（`norm[i]`）建形参类型，`modules/tensor/type.cpp` 的 `tensorFactsOf` 把 `f64[]` 映成 Float64；`reshape` 的 `infer` 读 `ctx.facts(0).dtype`。调用之前**没有目标类型**——这正是 `docs/design-principles.cn.md` §15 已列的未决条目（`entering-operator-args`，逐字："因为内建算子的形参类型由 C++ 的类型规则从实参算出，调用之前没有目标类型。候选：让类型规则能报出'我接受的类型'，使内建算子与用户函数一致"）。按 AGENTS.md，未决项与未覆盖项**问作者、不擅自决定**。

两条都能让这个计划绿，代价不同：

- **(a) 语言侧：把调用点的期望类型传进调用**。`docs/design/training/type-arguments.cn.md:10` 现在明确写"不把调用结果的期望类型误传给每个实参"；要改的就是这句。需要定的语义至少有：调用点期望结果是否给**类型变量**定值（现在只对"只出现在返回类型里"的变量成立：`generic_explicit_types.cml:16` 的 `let xs: int[] = empty()` 是绿的，而 `func g2(): f32 { return firstf([1.0]) }` → `Return type 'float' does not match ... 'f32'`，因为实参先绑定了 `T`）；开放元素类型（裸 `Tensor`、`number[]`）是否同样被调用点填上。规模：动 `visitLinkNode` 的实参顺序/重试与内建算子的解析，影响面是全体泛型与内建算子调用，必须整层实测。
- **(b) 模块侧：让张量模块按自己的文档说话**。`stdlib/signatures/tensor.cml:16` 逐字写"元素类型跟随值：`f32` for floats"，但实测 `new([1.0, 2.0])` 与 `reshape([1.0, 2.0], [2])` 都是 `Tensor<float64, [2]>`（只有 `tensor:zeros` 是 `Tensor<float32, [2]>`）。若"浮点字面量数组 → f32"是模块契约，则 `tree_training.cml:8` 在**不改语言**的情况下就绿；这会与 5.1 的"字面量默认类型 `float`"（`number[]` 形参的元素类型是开放数值）产生要求澄清的边界：是模块的契约还是语言默认值说话。

建议：先落 `6c4b371e6`（裸 `Tensor` 形参按调用点实例化）再决定 (a)，理由是实例化之后图上只剩具体类型，双向传播的改写面与回归面都更窄（`tensor-reduction-element-type-slice.md` 的同一理由）。

## 同族但**未**落地、已实测的两处（都不在本车道目的内）

1. **形参位置也还没走到**（全字面量的能走到，有计算元素的走不到）：`func pick(xs: f32[])` 配 `pick([1.0000000596046447753906251, float(n)])` → `SemanticDiag::NoMatchingFunction ... (float[])`。原因是同一个：实参的复合字面量只要有计算元素就走 FILL，而形参类型在实参构建时不是期望类型通道。5.1 把"形参"列在"有期望类型的位置"里，所以这一处比内建算子那处更"该修"，但同样要动调用点的构建顺序。
2. **声明位置的元组字面量按 double 舍入**（另一个缺陷，我只测未改）：`let p: (f32, f32) = (1.0000000596046447753906251, 2.0)` 的 `p.0` 打印 `1`（精确值在 f32 里落在中点之上，应得 `1.00000012`；`1` 说明它是从被降下来的 double 转的，正好是 tie、偶舍入到 `1.0`），而**同一字面量**在返回位置（`return (...)`）、结构体字面量字段里、以及数组声明位置（`let xs: f32[] = [...]`）都是 `1.00000012`。即 5.1 的"按写下的精确值转换一次"在**注解元组**这条 GCT 路径上没走到。回归用例 `context.composite_rounding` 只覆盖了返回位置，所以这一处现在是没被测到的。

## 本轮实测（同一主机、同一时段、同一命令）

- 本车道最终构建，整层 `smoke,test/plans -Tier fast -Jobs 8`：`total 6027, pass 6010, fail 2, skip 11, known failures 4`。2 个红就是既有的 docs 债（`docs.links.citations`、`docs.snippets_compile`）。
- 基线（`h413-when-placement`，`a6f4cfbcf`，`node scripts/build-info.js check --tag latest` = 0）同一命令：`total 6023, pass 6006, fail 2, skip 11, known failures 4`，同样那 2 个 docs 债。两边的失败集合相同。
- `test/plans/gpu/tiles_transfer.plan.toml` 在本车道构建和基线构建上都是 `total 3, pass 2, fail 1`，红的都是 `tree_expansion_autodiff_and_device_update`，诊断逐字相同（`tree_training.cml:8:5` 的 `SemanticDiag::ReturnTypeMismatch`，`'{ weight: Tensor<float64, [2]>, nested: (Tensor<float64, [1]>, Tensor<float32, [0, 2]>) }'`）。落地本车道的那半**没有**改变这个红，这正是要裁定的地方。
- 实现层的一个坑，记在这里免得下一个人重踩：fill 的槽位是**位置类型**里的字段序号，而字面量自己的结构体类型可能用**另一个顺序**写同样这些字段（未命名结构体类型按字段名排序，具名类型按声明顺序）。只把 fill 的类型换成位置、不换槽位，就会把值写进错的字段：`showcase/d4-mcts` 的 `root_visits` 立刻是 2000→1999、0→-1（2000 次试走的根计数落在 `place` 上，`place` 是 -1）。所以 `compositeWithHolesInType` 同时报出"计算元素在位置类型里的槽位"，静态值本身也做成位置那个具名类型（`StructDataFactory::buildAs`，与 `compositeDataInType` 同一做法，5.2）。

## 我没有做

没改用例 `tree_training.cml`；没动内建算子解析、张量模块、`docs/design/training/type-arguments.cn.md`、`docs/design-principles*.md`；没动声明位置元组的精确值；没做形参位置/调用点的穿透；没动 §15 的任何一条。