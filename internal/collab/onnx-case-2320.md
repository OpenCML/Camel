# `onnx.plan.toml` 的 `:2320` 断言：哪个用例、断言什么、为什么现在绿、命中的是哪条消息

只读静态核对，零构建、零改动。读的树 `D:\Projects\Camel-wt\misc` @ `c8c740774b`；下面所有行号都是这个基上的读数（基一变先重核）。

## (a) `:2320` 那句属于哪个 `[[tests]]`、哪个用例

- 块起始：`test/plans/feat/modules/onnx.plan.toml:2298`（`[[tests]]`）。
- 块内的名字：`:2299` `name = "export_loop_carried_array_field"`。
- 块尾：`:2325` `timeout_ms = 8000`；下一个 `[[tests]]` 在 `:2327`（`export_train_step_vjp`，`:2328`）。
- ⇒ `:2320` 的 `assert` 在 **`:2298`–`:2325`** 这个块里。上一个块的用例是 `export_loop_carried_array`（`:2274`）、下一个是 `export_train_step_vjp`（`:2328`），都不是它。

## (b) 名字是不是 `export_loop_carried_array_field`

**是。** `:2299` 原文 `name = "export_loop_carried_array_field"`，即计划 `modules.onnx` 下的 `modules.onnx.export_loop_carried_array_field`（也就是 72 表里唯一那条"乙"）。

## (c) 它断言的完整期望串（逐字，含引号类型与句内空白）

`:2320` 原文（注意这一行是**单引号**串，相邻两条 `:2318`/`:2319` 是双引号）：

```js
assert(text.includes('whose length changes from one round to the next (the body appends to it or concatenates it), and a Loop carries a state whose shape is the same on every round'), 'the diagnostic must state the fact: ' + text)
```

`includes` 的实参，逐字：

```
whose length changes from one round to the next (the body appends to it or concatenates it), and a Loop carries a state whose shape is the same on every round
```

它要求的三段特征（按串内顺序）：

1. `whose length changes from one round to the next` —— **事实**：长度逐轮变化；
2. `(the body appends to it or concatenates it)` —— **为什么**会变：体里 append 或 concat（括号与逗号都是串的一部分）；
3. `, and a Loop carries a state whose shape is the same on every round` —— **被 `D-1128-39` 撤回的那句前提**（"Loop 携带的状态每轮形状相同"）。

同块另外四条断言（`:2318`–`:2322`），作为上下文一起读：

- `:2318` `text.includes("a recursive call changes argument 2 of a recursion whose depth depends on the model input")`
- `:2319` `text.includes("the leaf 'xs', an int array")`
- `:2321` `text.includes('ExportUnsupported')`
- `:2322` `!fs.existsSync(path.join(input.test_tmp, 'export_loop_carried_array_field.onnx'))`

三段（`:2318`/`:2319`/`:2320`）是同一条格式串的三个落点：改措辞时它们会一起红。

## (d) 它现在为什么是绿的

块里**没有** `skip`、没有 `known_fail`、没有 `if`、没有 `expect_stderr`。它的"条件"就是期望字段本身：

```
:2305  expect_exit = 1
:2308  expect_diagnostic_name = "RuntimeDiag::UnhandledError"
:2309  expect_diagnostic_code = "0x04000051"
:2310  expect_diagnostic_line = 22
:2311  expect_diagnostic_column = 9
```

外加 `:2313`–`:2324` 的 `verify_script` 五条（`fs`/`path` 引入、`text = input.normalized.output`、上面四条 `includes`、一条"没有模型文件"），`:2325` `timeout_ms = 8000`。

⇒ 它绿的读数是：**导出确实拒绝了**（`expect_exit = 1`、`UnhandledError`、`0x04000051`）、**定位在入口调用**（`line 22 / column 9`，对应用例 `test/cases/modules/onnx/export_loop_carried_array_field.cml:22` 的 `walk(x, { xs: [0], k: 0 })`，缩进 8 空格 ⇒ 列 9），**消息里有那三段、且没有 `.onnx` 落盘**。不是跳过、不是 `known` 放行。

`:2306`–`:2307` 的注释写明位置为什么是入口那次调用：

```
# The exporter locates a refusal about the recursion as a whole at the call that entered it
# (exporter.cpp, `located`): the entry call `walk(x, { xs: [0], k: 0 })` in the closure body.
```

⇒ 一句话：**它绿在"拒绝仍在"，而拒绝的理由句仍是已撤回的那句前提。** 这正是 `baseline-73.md` 说的"过渡重钉"形态，不许当"修复"。

## (2) `exporter.cpp` 三条消息：实测命中哪一条

先说行号：你点的三处在这个基上是这些，**三处都不是消息**（消息在别处）：

| 你点的行 | 这个基上的原文 | 是不是这个用例的拒绝 |
|---|---|---|
| `src/stdlib/onnx/exporter.cpp:1433` | `/// Whether a Loop can keep the value the body gives the recursive call in the place of before:`（下面 `:1437` 才是 `bool shapeIsKept(const Value &before, const Value &next, std::string &why)`，`why` 是**出参**） | **否**。`shapeIsKept` 只在 `:3232` 被调用，那在 `Evaluator::translateLinear`（`:3089`）发 Loop 的那一步 |
| `:1873` | 注释 `// still has no state.` —— 属于 `Evaluator::mergeOutcomes`（`:1787`）里"两条路的状态叶必须 dtype 与 shape 相同"的检查 | **否**。它那条 `unsupported` 在 `:1887`–`:1896`，文本是 `…it is {}, and {}; a Loop carries a state whose dtype and shape are the same on every round`（多出 `dtype and`），与 `:2320` 断言的串**对不上** |
| `:3210` | `const std::vector<Value> full = place(f.args, f.carried, …)` —— `emitLoop(...)` 的续算 lambda 里 | **否**。是代码不是消息，同上在发 Loop 那一步 |

**实际命中的是 `src/stdlib/onnx/exporter.cpp:2088`**，在 `Evaluator::evalLoopBody`（`:2033`）里，`catch (const ExportError &)` 中、且 `growsItsExtent(g->view(), argNodes[k])` 为真时（`:2083`–`:2084`）：

```
:2088  "a recursive call changes argument {} of a recursion whose depth "
:2089  "depends on the model input: it is {}, whose length changes from "
:2090  "one round to the next (the body appends to it or concatenates "
:2091  "it), and a Loop carries a state whose shape is the same on every "
:2092  "round"
:2094  growingLeafName(g->node(loop.ports[k])->dataType(), loop.inputs[k])
```

按用例形状判为什么是它（三条独立证据）：

1. **实参形状**：用例 `:12` `type State = { xs: int[], k: int }`、`:15` 体里 `s.xs->append<s.k>`、`:22` 入口 `walk(x, { xs: [0], k: 0 })` ⇒ 第 2 个实参是**带 `int[]` 字段的 struct**，体对这个字段 append；`:2088` 的 `k + 1` 正好印成 `argument 2`。
2. **增长判定**：`growsArrayExtent`（`:553`）只认三种 id —— `oper::Append`、`oper::AppendOwned`、`concatArrOper()`；`:15` 的 `append` 命中第一种 ⇒ `growsItsExtent`（`:1413`）为真 ⇒ 走 `:2088` 这条替身诊断（原始 `ExportError` 被替换成这条更准的）。
3. **叶子命名**：`growingLeafName`（`:1621`）对 struct 找"长度开的字段"（`:1626`–`:1633`），命中 `xs`，印成 `the leaf 'xs', an int array`（`:2319` 断言的那句）；`arrayKindName`（`:1594`）对 `Int32`/`Int64` 元素返回 `an int array`（`:1600`–`:1602`）。

⇒ 另外两处（`:1890` 的 `mergeOutcomes` 版、`:2231` 的 `translateLoop` 版）文本与 `:2318`–`:2320` 的串都不吻合（`:2231` 那句是 `…which is {}; a loop carries tensors and scalars and tuples and structs of them`），`:1887` 多 `dtype and`；所以"绿"只能由 `:2088` 产生。

## 与 `D-1128-39` / `D-1128-52` 的接口（一句，给改它的人）

`:2320` 第 3 段正是 `D-1128-39` 撤回的前提：ONNX 的 `Loop` **可以**携带 sequence（或长度逐轮变化的张量），"这个导出器还不会降"是实现缺口。所以改这条用例时，动的是 `:2088` 的措辞（以及 `:2318`/`:2319` 两段同源串）与 `desc`，不是"要不要保留拒绝"。按 `D-1128-52`：改成正向用例（导出成功、模型里有 `Loop` 且携带量是 `sequence` 或长度变化的张量、ORT 与 `nvm` 逐值相同），降级落地前照旧红。

---

读数来源（全部只读）：`test/plans/feat/modules/onnx.plan.toml:2298`–`:2325`、`test/cases/modules/onnx/export_loop_carried_array_field.cml`（24 行全文）、`src/stdlib/onnx/exporter.cpp` 的 `:553`、`:1413`、`:1437`、`:1549`、`:1594`、`:1621`、`:1787`、`:1887`、`:2033`、`:2083`、`:2088`、`:2176`、`:3089`、`:3232`。