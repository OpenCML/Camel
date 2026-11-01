# `jvp.cpp` 的 270 行脚手架，拆成照抄用的段

施工图。左边是 `modules/transforms/jvp.cpp` 的段（行号按该文件现文，共 319 行），中间是它做了什么，右边是转置版要怎么改。**三处一定会不同的地方**（逆序、k 份相加、换成 `transposeRuleOf`）单独标出落在哪一段。

> **复核 2026-11-01**：`modules/transforms/jvp.cpp` 现在 **1396 行**（写下这一页时是 319 行，段号按那时的文本编号；比上一次复核的 1315 行多的 32 行是 `c8bc3d87f` 让 `jvp(f)` 在调用方的图建起来时就实例化（再往前 1305→1315 的 10 行是 `ba0a37be9` 与 `c98dd492f` 让提升从调用方取源图的 id、并打印它建出的图）；最后的 1366→1372 的 6 行是本车道让 `lifted-at=compile` 的读数把 `liftedFrom=` 读作 `unassigned` —— 构建期提升没有运行期的图 id，写一个数字会被读成另一张图，所以那六行是说明为什么这里是词而不是数），段行号今天已经对不上文本，重读这一页要先按段名找位置。`.cpp` 的行数由 `node test/tools/check-doc-readings.mjs` 核对。

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

> **复核 2026-11-01**：现在读作 `transpose.cpp` **1881 行**、`transpose_operator.cpp` **169 行**（两笔合并——`lane-gs6` 与 `lane-reinterp2`——把它们的长处补完了；比上一次复核的 1707 行多的 22 行是 `070ef1540` 让一个入口点名函数值持有的图、并让经值的调用答出来）。这条读数涨了三次，都在同一个通道里：1159 → 1311 是 `f8de49a65` 让元组值逐槽带余切（比上一次复核多的 152 行；再往前那条 149 行是 `4278f02a3`、`68b524478`、`6b4068b7e` 让每个逃逸值各带一个累加器），1311 → 1345 是 `00f9dfa39` 补上元组套元组与那三条读数，1345 → 1467 是让"没有切空间的值"（整数）成为导数的零、并把停在哪打印出来（`contribution=0`），1467 → 1475 把同一条读法补到分支条件上（`bool` 也有一条 `contribution=0`），1475 → 1476 是 `lane-remat` 那一笔把签名拒绝挪到臂上（`skip=arm arm=0`）。反向循环因此从"一个状态一条余切"变成"每状态一条、累加器排在后面"，`runArm` 也从一进一出变成一串种子配一串停靠点；1476 → 1654 是 `lane-remat` 的转置臂两笔（残差读到的值把余切交回它被读出的那一槽并打印，臂里的调用转置成 callee 表里的成员、提升的调用看穿到它提升自的那张图）；1881 = 1729（develop 的函数值调用与整数零的读数）+ 146 行 `lane-remat` 的参与集。

**预计要改几段、几行：14 段里 4 段照抄、7 段改写、3 段删；约 260 行，其中照抄约 110 行、新写约 150 行。**

> **复核 2026-11-01**：按上表“转置版”一栏重数，照抄 4 段（1、3、7、14）、改写 7 段（2、4、6、8、10、11、12）、删 3 段（5、9、13）；原文把前两个数写反了（写成 7 段照抄、4 段改写），段数合计 14 不变。`node test/tools/check-doc-readings.mjs` 数这一页的 14 行与这三个数。
