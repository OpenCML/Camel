# `jvp.cpp` 的 270 行脚手架，拆成照抄用的段

施工图。左边是 `modules/transforms/jvp.cpp` 的段（行号按该文件现文，共 319 行），中间是它做了什么，右边是转置版要怎么改。**三处一定会不同的地方**（逆序、k 份相加、换成 `transposeRuleOf`）单独标出落在哪一段。

## 一、段

| # | 行 | 它做了什么 | 转置版 |
|---|---|---|---|
| 1 | 1–28 | 版权、文件说明、include（`draft.h`、`draft_session.h`、`mm`、`rtdata`）、`namespace camel::transforms` 与 `rt`／`mm` 别名 | **照抄**，只改文件说明 |
| 2 | 30–55 | `jvpSignature(F)`：把 `F` 的类型抬成「每个参数两倍、结果是 `(value, tangent)` 对」；只接受一个 `float` 参数、`float` 结果、无 `with` | **改写**：`transposeSignature(F)` = `(ct: float) => (float)` —— 一个余切进、一个余切出，`FunctionType::create({}, {float}, float)` |
| 3 | 57–74 | 入口：`program`/`function` 为空返回空；签名算不出就 `skip=signature`；参数不是一个就 `skip=params` | **照抄**，换打印词 `TRANSPOSE` |
| 4 | 76–97 | 开 `GraphDraftGroup`、`reserve` 新 member（`jvp_of_<name>`）、`setFuncType`、两个 `addPortNode`（value／tangent）+ 两个 `appendNormPort` | **改写**：**一个**端口（余切）+ 一次 `appendNormPort`；名字 `transpose_of_<name>` |
| 5 | 98–107 | 复制骨架：`copyOf`／`copied` 两张表 + `bool complete`（值按原样复制一遍） | **删**。转置不复制值：余切是新的数据流，值只在法则里当 `c` 用（见第 6、8 段） |
| 6 | 108–115 | 切向骨架：`tangentOf` 表 + 一个共享的 `zeroConstant` | **改写，k 份相加在这里**：`cotangent` 表 + `hasCotangent` 位图 + `addCotangent(ref, node)`（第一次写、第二次 `add_d` 相加）+ 每个常数节点的 `constantOf` 缓存 |
| 7 | 116–125 | `primitive(uri, inputs)` 造一个 `:op/*` 节点并 `setNormInputs`；`noteRule(name)` 去重记下用过的法则 | **照抄** `primitive`；`noteRule` 改成记**索引给的名字**（`add_t` 这类），不是 URI |
| 8 | 126–167 | `tangentRule`：按 `uri == ":op/mul_d"` 的 if 链，四条法则手写在 C++ 里（注释说明「从索引读是前路，但还缺两样」） | **改写，`transposeRuleOf` 整段替换这里**：先 `transposeRuleOf(uri)`，再按回来的**法则名**（`add_t`／`sub_t`／`mul_t`／`div_t`／`neg_t`）建图；非常数操作数要它的值 → `skip=residual` |
| 9 | 168–180 | 数一遍原图有哪些 `GCNodeKind`（`seenKinds`），给报告行的 `kinds=` 用 | **删**（报告行不写 kinds）或照抄 |
| 10 | 181–261 | 主循环：`ref` 从 0 到 `nodeCount` **正序**，`Port`／`Data`／`Oper` 三个 case；`Oper` 复制节点、取输入、调 `tangentRule`，查不到就 `skip=rule uri=` | **改写，逆序在这里**：`for (ref = nodeCount; ref-- > 0;)`；`Data` 不携带余切；`Gate` 把余切转给它的输入；`Port`（`normPorts().front()`）收到答案；`Oper` 调第 8 段 |
| 11 | 262–265 | `!complete` → `skip=body` | **改写**：`skip=residual`／`skip=arity`／`skip=kind`（分支、循环、调用在这里被拒） |
| 12 | 266–297 | 追出口的 `Gate` 链拿到「产出值的那一个节点」，取它的 `copyOf` 与 `tangentOf` | **改写，且要挪到第 10 段之前**：先追 Gate、把出口的余切设成入口端口，再开始倒着走；循环结束时 `Port` 给的那一个就是结果 |
| 13 | 298–311 | 元组出口：`Fill` 节点把 `(chain, tangent)` 两半装成一个对 | **删**：出口就是那个余切节点，`setExitNode`／`setOutputNode`／`setReturnNode` 直接给它 |
| 14 | 312–325 | `group.encode()`、取 `group.encoded(member)`、拼报告行（`graph=`、`type=`、`nodes=`、`rules=`）、返回 `JvpGraph{program, graph}` | **照抄**，换词 `TRANSPOSE`，去掉 `kinds=` |

## 二、三处差异落在哪几段

| 差异 | 落在 | 具体 |
|---|---|---|
| **逆序** | 第 **10** 段（181–261），并把第 **12** 段（266–297）**前置** | 正序改倒序；出口的余切必须在走图之前种下，否则第一次走到出口时它没有余切 |
| **k 份相加** | 第 **6** 段（108–115）+ 第 **8** 段（134–167）每条法则 | 一个值被用 k 次，倒过来就有 k 份余切：`addCotangent` 第二次起建 `:op/add_d`。这正是 `2*x + 3*x` 里参数被用两次、答案 5.0 的来源 |
| **`transposeRuleOf`** | 第 **8** 段整段（126–167），第 **7** 段配合 | 手的 if 链换成「问索引要法则名，再按名字建图」；问不到就按两种理由拒绝（家族没 import／家族没这条） |

## 三、实测

今天写出来的那一版（跑出 `5.0` 与 `20.0` 两个数）：`modules/transforms/transpose.cpp` **261 行**，`modules/transforms/transpose_operator.cpp` **146 行**（操作符那半：`TransposeResolver` 抄 `JvpResolver`，`transposeKernel` 抄 `jvpKernel`）。

**预计要改几段、几行：14 段里 7 段照抄、4 段改写、3 段删；约 260 行，其中照抄约 110 行、新写约 150 行。**