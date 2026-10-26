# `onnx` 草稿要求的两处"实现做不到"，以及据此的落地形状

（施工方开工第一天就撞上，两条都不是迁移问题、是新功能或口径问题，所以先问你再动手。）

## 一、计算签名的原生算子上，命名 `with` 实参要"≥2 条同名规则"才可用

`export_model<out: X>(f, x)` 今天被拒：`bindArguments` 里 `signatureForArgumentContext()` 对"没有 `fixedSignature` 的算子组"返回 `nullptr`（`type_arguments.cpp:83-100`），`declarationsOfCallee()` 对"只有一条规则的组"返回 `nullopt`（`builder_calls.cpp:1132`），于是落到 `builder_calls.cpp:1334` 报 `SemanticDiag::WithArgumentCallee`（逐字："…the signature of '{1}' is not known where the binding is written."，`docs/diagnostics.md:6530`）。

今天能工作的两种形状是：**(a) 位置实参** `export_model<path>(f, x)`（`tiles` 的 `source`/`regions` 就这么调）；**(b) 该组规则 ≥2 条、且同一位置在所有规则里同名**——`len(x:'abc')`、`json.get<key:'a'>` 走的就是这条，`stdlib_registry.plan.toml:106 registry.overload_position_names_agree` 把它钉成了特性。现在 `onnx` 的 `{{0,{}},{-1,{}}}`（单条规则）两条都不满足。

**施工方的落地形状（我批准，等你一句话）**：把 `onnx:export_model` 注册成 **3 条规则**，`with` 元数 1/2/3，同名前缀 `out` / `out,dynamic_axes` / `out,dynamic_axes,target`，普通参数恒 2 个 ⇒ 172 个调用点里出现的全部形状（`out`、`out+[0]`、`out+[]+'none'`）逐字可用，位置实参也照旧。**代价**：草稿里 `dynamic_axes: int[]? = null` 那种"跳过 dynamic_axes、只给 target"的写法不成立（会报 `WithArgumentMissing`）；施工方已扫过全部调用点，**没有这种写法**。要严格支持就得改核心（让计算签名的组也带 names/defaults，即 `nativeDefaults_` 只认 `fixedSignature` 那条路）——是否要，你定。

## 二、遍参数管线读不出 `int[]?` / `string?` / `null`

`with <out, dynamic_axes, target>` 里的 `int[]?`/`string?` 今天**读不出来**：命令行遍参数只有 `bool/int/float/string`（`pass_spec.cpp:396 declarationType()`、`params.h:31 PassParamType`），`evalNode` 明确拒 `null`（`pass_spec.cpp:199`），`docs/cli.cn.md:163` 把这条限制写成了公开行为。声明里写 `dynamic_axes: int[]? = null` 会让 `readPassParams` 抛异常 ⇒ `onnx::export` 整体不可用（137 个文件在用）。

**而且这个遍今天没地方放这两个设置**：`exportEntry()` 只传 `out`，`translateEntryGraph(ctx, program)` 不接受 options（`export.cpp:684`）⇒ 所以"遍的 dynamic_axes/target"是**新功能**，不是迁移。

**我替施工方选 (A)（若你另有打算请说）**：遍保留 `out: string = ""`，`dynamic_axes`/`target` 记为缺陷＋后续项，行为零损失，`onnx::export out=...` 的调用点不动。**(B)** 是把管线扩成支持 `int[]`/`T?` 遍参数与 `null` 默认值并接到 entry 导出（核心新工作：`pass_spec.cpp`/`params.h`/`docs/cli.cn.md`/`test::params` 用例，且会改动一条**已写进公开文档**的行为）——要做的话请你点头，我们另开一条车道。