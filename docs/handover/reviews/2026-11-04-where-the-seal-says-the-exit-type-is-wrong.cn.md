# 出口类型对不上：已按 B 落到 `RuntimeDiag::ExitTypeMismatch`，请作者裁这个码与措辞

## 现在的状态（一句话）

封存层判"图形参声明的出口类型"时，**程序自己的错**不再走 `PassInternalError`，而是走一条新码
`RuntimeDiag::ExitTypeMismatch`（`0x040001AE`），句子同时说清**手上是什么类型、声明要什么、这个值
是图里哪个值**；真的"pass 把图写坏了"仍然走 `PassInternalError`，两条通道按**异常类型**分开
（`camel::runtime::GraphExitTypeMismatch` 与 `std::runtime_error`）。**行号给不出**，原因在下面第四节。

## 一、新码

| 项 | 值 |
| --- | --- |
| 名字 | `RuntimeDiag::ExitTypeMismatch` |
| 值 | `0x040001AE`（registry 里写 `0x0001AE`，前两位是 section） |
| 严重度 | error |
| message | `The value the graph '{0}' hands on has type '{1}', and the graph declares '{2}'; the value is {3}.` |
| help | 一个 `return` 是有期望类型的位置（设计原则 5.1），数字之间不转换；图就是函数，所以声明就是要对齐的类型；被规则替换的返回值取规则右值的类型；这个值由哪个节点、哪个源名承担 |
| fix 栏 | `none: choice: hand on a value of the declared type, or declare the type the value has.` |

实测原文（2026-11-04，`test/cases/runtime/exit_type/float_declared_array_returned.cml`）：

```
error[RuntimeDiag::ExitTypeMismatch]: The value the graph 'target' hands on has type 'float[]', and the graph declares 'float'; the value is node 3.
help: A return is a position with an expected type: what a function hands on is of the type the function declares, and nothing is converted on the way (design principles 5.1) -- an int is not read as a float, and a float[] is not read as a float. ... A return a rewrite rule replaced takes the type of the rule's right side, so a rule whose right side is of another type than its left is the place to look. The message names the value by its node and, when it has one, by its source name: a node a rewrite built carries no source position of its own, and that is why this report can stand without a line of its own.
```

同一条规则的另一个方向（`: int` 收一个 `float`）是 `... has type 'float', and the graph declares 'int'; the value is node 3.`

## 二、两条通道怎么分开的（实测）

- **程序的错**（出口值的类型进不了声明）：`ExitTypeMismatch`。判词来自
  `src/runtime/graph_edges.cpp#checkReturnValueType`，它把事实装进
  `include/camel/runtime/graph_exit_type.h#GraphExitTypeMismatch`，由
  `src/execute/pass.cpp#reportPassFailure` 认出并提交新码；`validateRuntimeGraphEdges` 只在**没有结构
  性违规**时才抛它（有结构性违规时先报结构性那条——缺陷优先）。
- **真的内部不一致**（图本身破了不变量，例如节点把 `int` 转成 `float!`）：仍然是
  `PassInternalError`。实测同一套探针里的 tagged 形状今天打印
  `RuntimeDiag::PassInternalError ... converts a int to a float!, which is forbidden.`，通道没动。
- 两个禁形程序今天都打新码（改前分别打印 `6.3956244425295e-310` 与 `4612811918334230528`，exit 0）。

## 三、四处同步（2026-11-04，本树）

1. `diagnostics/registry.tsv` 一行（`runtime` 段，`PassBoundaryPorts` 之后，号码留空）；
2. 用例 + 计划条目：`test/plans/feat/runtime/exit_type.plan.toml` 的两条 `negative` 现在写
   `expect_diagnostic_name = "RuntimeDiag::ExitTypeMismatch"` 并引用句子；
3. `node scripts/diagnostics-catalog.mjs --write`（**在计划条目之后**跑）：编号 `0x0001AE`、生成
   `include/.../messages/runtime.h`、`src/.../messages/runtime.cpp`、`docs/diagnostics.md`、
   `diagnostics/examples.tsv`（例子自动钉到上面那个用例）；
4. `node scripts/camel-for-ai.mjs --write`：`docs/reference/camel-for-ai-diagnostics.md` 的生成区。

## 四、为什么这一处给不出行号

封存的输入是冻结图，位置只有一条：`GCGraph::nodeOrigin(ref)` → `origin_id_t`，**0 表示没有**；图本身
（`GCGraph`）没有"函数声明在哪"的记录。而**这两个程序里被拒的节点是规则建出来的**（规则库把被返回
的算子换成另一种类型的值），规则建节点时不带来源，于是 `origin == 0`，无法解析成 `file:line:col`。

能给的都给了：句子点出**图名＝函数名**（`target`）和**值**（`node 3`，有源名时是
`node 3, the value 's'`）；渲染侧的机制是通的——`atOrigin(origin)` 在 `origin != 0` 时会打出
`file:line:col`（同一个 sink 上 `UnhandledError` 的位置就是这么打的，见
`test/plans/feat/linear/entry.plan.toml` 里 `error_from_operator.cml:6:1` 那条断言），所以**将来**
rewrite 引擎给建出来的节点带上来源（或封存前把源上下文带进 `EdgeChecker`），这一条会自动有行号，
不用再改诊断。

## 还剩什么要作者裁

- **码名与措辞**：`ExitTypeMismatch` 这个名字、`0x040001AE` 这个号，以及 help 里"图就是函数"的说法，
  作者若换词，改 `diagnostics/registry.tsv` 一行 + `--write` 即可，句子结构不用动。
- **要不要把来源补进 rewrite 引擎**（另一个车道）：那是让这一条永远有行号的唯一办法，且改动在
  `modules/rewrite` / `src/passes/opt/generic`，不在本轮的可写范围。
- 本页记录的是"已经这样做"，不是"要不要这样做"：若作者要求只改 `PassInternalError` 的 help，
  新码可整体退回，`checkReturnValueType` 的判据与事实结构照用。