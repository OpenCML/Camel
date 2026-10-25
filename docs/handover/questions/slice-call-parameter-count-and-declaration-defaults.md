# 切片调用的参数个数：声明上的默认值到不了原生算子调用

> 说明（协调者）：提出问题时的代码在 `handover/h585-slice-all-calls`（基点 = 翻转线上的切片提交 `c9be5d619`；核心已接管所有 `slice(...)` 调用）。页里引用的行号以那条分支为准；三页都是规格问题，不是实现缺陷。


车道：`handover/h585-slice-all-calls`（族 = 核心接管全部 `slice(...)` 调用）。

## 一、是什么

作者 2026-10-25 的裁定（`slice-function-form-and-what-the-core-answers`，第 4 条）要求：函数形式的参数表统一成六个词（值、轴、起点、终点、步长、written 掩码），函数形式自己填"两界都写"的掩码，且**参数个数不再区分两种拼写**。

今天这条只做到一半：`slice(t, axis, a, b, s)` 是六词，`slice(t, axis, a, b)` 仍是四词、`slice(t, axis, a, b, s)` 是五词——缺的 `step`/`written` 由**内核按实参个数补默认值**（`step = 1`、`written = 3`）。于是"少写词的调用也拼出六词节点"这件事没有编译器通路，而内核按个数补值恰好是裁定要取消的东西。

根因有两条，都要回答：

1. `stdlib/signatures/globals.cml` 的文件头逐字写着 **"For reference only: these declarations are not compiled."** 也就是说，`export func slice(..., step: int = 1, written: int = 3)` 这行**不参与编译**，它只喂 `docs/stdlib-reference.md` 与 `--verify-ops`。原生算子（`:op/slice_sub`）的定签名只能来自 C++ 侧注册（`src/builtin/modules/globals.cpp`）。
2. 即使声明被编译，声明的参数默认值也到不了调用：`defaultsOfCallee`（`src/compile/gir/builder_calls.cpp:841-898`）对该算子组返回 `nullptr`。

## 二、逐字证据

1. 声明（`stdlib/signatures/globals.cml:391-392`，本轮唯一被授权改动的一行；文件头声明它不编译）：

```
#op<':op/slice_sub'>
export func slice(t: T[], axis: int, start: int, end: int, step: int = 1, written: int = 3): T[]
```

2. 组里唯一的定签名（`src/builtin/modules/globals.cpp`，`sliceParameterTable()`）与函数形式规则：

```
{{0, {}}, {-1, {}}},
"(t: T[], axis: int, start: int, end: int, step: int, written: int) => T[]",
```

3. 填默认值的入口（`src/compile/gir/builder_modules.cpp:490-498`）：只有 `GCT::Tree::of(gct)->parameterDefaultsOf(gct) != nullptr` 时才 `evaluateDefaults` 并写 `nativeDefaults_[uri]`；`defaultsOfCallee` 再要求组的每个非空定签名一致且 `nativeDefaults_` 里有该 URI。

4. 实验（把函数形式规则收紧成必须六词，`{{0, {}}, {6, {false × 6}}}`，`-Tier fast` 全量实测）：

```
(slice_one_node)/.../slice_one_node.cml:23:18: [Error]: No matching function found for argument types: <> (f32[4], int, int, int).
    <:struct/slice_arr>: <start: int, end: int> (xs: T[]) => T[]
    <:op/slice_sub>: (t: T[], axis: int, start: int, end: int, step: int, written: int) => T[]
 (name=SemanticDiag::NoMatchingFunction, code=0x0300000F)
```

`slice(x, 0, 1, 4)` 写在 `slice_node_function.cml:12` 与 `slice_one_node.cml:23`，实参个数停在 4——声明的两个默认值没有把调用补到六词。（该实验表现为 2 条新红，随后按今天的状态改回。）

5. 反向证据：模块自己的切片曾按个数读（`modules/tensor/ops/layout_ops.cpp`，已随裁定删除）：

```
const int64_t step = hasArg(norm, 4) ? intArg(norm, 4) : 1;
```

## 三、今天的读数

- 内核按实参个数补默认值（`src/builtin/operators/struct.cpp`，`__slice_sub__`）：

```
const size_t words  = norm.size();
const Int64 step    = words >= 5 ? norm.get<Int64>(4) : 1;
const Int64 written = words >= 6 ? norm.get<Int64>(5) : 3;
```

- 静态检查同样按个数取（`src/builtin/modules/globals.cpp`，`staticSliceRejection`）：`norm.size() < 4 || norm.size() > 6` 不查，掩码缺省读作 3。
- 伴随规则（`src/builtin/operators/derivative.cpp`，`sliceSubArrayVjp`）本轮改成接受 4–6 词，缺 `step`/`written` 同样补 1/3。**它是被删模块切片的实测顶出来的**：删除态下 `feat.modules.autodiff` 的 `slice_grad.runtime.nvm` 逐字报

```
():?:?: [Error]: Pass 'std::nvm' failed with an internal error: Derivative of ':op/slice_sub' expects 6 inputs (value, axis, start, end, step, written), got 5.
```

  也就是说：只要核心的函数形式答一个少写词的调用，正反向的读词口径必须一致。
- 四/五词的调用今天照常工作：`slice_one_node` / `slice_node_function`（`test/plans/feat/modules/tensor/subscript.plan.toml`）全绿。
- 词表：`:op/slice_sub` 六词 `(t, axis, start, end, step, written)`；掩码两位（bit0 = end，bit1 = start，`src/compile/gct/builder.cpp:2516` 逐字 `intConstant((written[1] ? 1 : 0) | (written[0] ? 2 : 0))`），两界都写 = 3，步长不入掩码。

## 四、选项与代价

**选项 A：让原生算子也能有一份被编译的声明（带默认值）。** 例如让 `#op<...>` 声明进入一个被编译的模块，或让 C++ 注册侧带上默认值，再让 `defaultsOfCallee` 能取到。代价：这是所有原生算子共用的一条通路，今天 `stdlib/signatures/*.cml` 里没有第二个 `= 默认值` 的声明，等于开一条没有先例的路；要有自己的用例（一个带默认值的原生算子声明，四/五词的调用落成六词节点，且正反向一致）；`registry.reference_is_generated` 与 `docs.ai_reference.current` 的字符数要跟着重出。

**选项 B：把裁定落成"同一节点、同一读法、同一掩码"，不要求字面六词。** 保留内核与伴随按个数补默认值（今天的读数），文档里写明参数个数仍区分两种拼写。代价：裁定第 4 条落空；`slice(t, axis, a, b)` 与 `slice(t, axis, a, b, s)` 在 `std::gir` 里仍是一个四词、一个六词，任何按节点形状做的 pass 都要记着；`--verify-ops` 里"参数个数比对"继续拿 C++ 侧的 6 段签名与声明的 6 段比（本轮实测前后都是 `19 documentation problem(s) in 425 compiled example(s)`，不新增问题行）。

**选项 C：把 `step`/`written` 从参数表拿掉，两种拼写都写四词。** 代价：掩码无处安放，下标路径的"未写端点"信息（`builder.cpp:2516` 的两位掩码）必须有别的载体，等于推翻裁定第 2 条的六词表，下标与函数形式将不再是同一节点——本车道的整轮改动（同一节点、同一伴随、任意轴）都以此为前提。