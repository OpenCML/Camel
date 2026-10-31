# `ASSERT` 普查：`src/passes`（逐块读，只算读过的）

> 判据：**一份分类表只算到"读过的那些"；表上必须写读了几个、还剩几个。**
> 三类的定义（用于本表，写清楚才可复现）：
> - **① 不该发生**：条件是"本函数/本编码器/类型系统建立的不变量"，违反＝编译器或前端有 bug，**任何输入都造不出来**。Release 下这种断言消失后行为只是"继续跑错"。
> - **② 可能为空**：检查的是指针/下标/长度，**一份畸形或被改坏的图能让它真的为空**，Release 下会崩（段错误或越界），不是"逻辑跑错"。
> - **③ 纯防御**：**同一事实在调用点或前几行刚刚检查过**，断言只是重复一次。

## 第 1 块：`src/passes/sched/fastvm/fastvm.cpp`（最大目录里最大的一个文件）

**读了几处：44**（43 个 `ASSERT` + 1 个 `CAMEL_ASSERT_NOT_VIEW`；`:42` 的 `static_assert` 不是运行期断言，不计）。
**本块三类：① 14 / ② 29 / ③ 1 = 44。**

### ① 不该发生（14）

| 行 | 断言在说什么 | 为什么任何输入都造不出来 |
|---|---|---|
| 174 | `srcArgs.size() == dstSlots.size()` | 直接调用的**编码布局**由编码器写死，调用点按同一张布局表生成 |
| 205 | `argValues.size() == dstSlots.size()` | 同上（第二个调用形态） |
| 230 | `closure->size() == closureNodes.size()` | 闭包的元组长度由前端按捕获表建 |
| 251 | `!wargs.empty()`（间接调用要求 `with-arg[0]` 是 Function） | `call` 的类型规则要求它；不合型的程序在类型检查就出不去 |
| 254 | `nargs.size() == normPorts.size()` | 被调图的端口表与调用点的实参表出自同一次编码 |
| 268 | `wargs.size() - 1 == withPorts.size()` | 同上 |
| 294 | `normArgs.size() == normPorts.size()`（带标记的调用） | 同上 |
| 301 | `withArgs.size() == withPorts.size()`（带标记的调用） | 同上 |
| 440 | `stackDepth_ > 0` | VM 主循环自己维持的深度，下溢＝调度器 bug |
| 634 | `ASSERT(false, "Unsupported marked operator")` | 只有被调度器判定为"可标记"的算子才会走到这里，走不到＝标记表与实现不一致 |
| 844 | `stackDepth_ == stackDepthBefore`（故障隔离边界） | 隔离层自己在边界上前后的深度不变，这是它的定义 |
| 1072 | `bc->opcode == OpCode::CALL`（JIT 出口） | 出口表与字节码生成同源 |
| 1097 | `ASSERT(false, "Unknown exit kind")` | 出口种类是枚举，穷尽 |
| 1244 | `graph->hasFrameLayout()` | 执行前必须先 materialize，这是调用顺序的前置条件 |

**这 14 处的共同点**：它们守的是**两个生成器之间的一致性**（编码器与执行器、标记表与实现、出口表与字节码）。**Release 下它们消失后不会崩，只会算错**——所以它们是最需要换成 `CAMEL_ASSERT` 一类的"Release 也在"的断言的地方（见 `assert-is-not-a-release-guard.md`）。

### ② 可能为空（29）

按位置分成五组，每组守的是同一件事：

| 组 | 行 | 可能为空的东西 |
|---|---|---|
| 调用帧 | 173, 191, 204, 218, 246, 247, 287 | `callerFrame` / `calleeFrame`——帧按序号从帧表取，**表比图短就是空** |
| `Function` | 219, 248, 288, 813, 1077, 1403 | `func`——payload 里的函数引用被改坏／未 materialize |
| 运行图 | 221, 250, 290, 1079, 1357, 1405, 1412, 1430 | `targetGraph` / `runtimeGraph`——**调用的图还没 materialize** |
| 闭包与端口 | 229, 239, 263, 277, 311, 316 | `closure` / `port`——闭包元组与端口表错位 |
| 程序根 | 340, 519 | `runtimeRoot` / `program->root()`——程序没有根图 |

**为什么算②不算①**：这些指针的**来源全是图里的表**，而图可以被 DECODE 出来（反序列化、宏克隆、pass 改坏）。**"不可能为空"只对前端刚建好的图成立，对解码来的图不成立**——而那正是 b6 之后所有图都会走的路。

### ③ 纯防御（1）

| 行 | 断言 | 为什么是重复 |
|---|---|---|
| 764 | `CAMEL_ASSERT_NOT_VIEW(next, accumulatorType, "the reduce accumulator")` | 同一函数的调用点已经在类型边上检查过同一条；**这是②里最不值钱的一类**——类型断言重复一次，不会拦住任何真实的错 |

## 进度

| 块 | 目录/文件 | 读了几处 | ① | ② | ③ |
|---|---|---|---|---|---|
| 1 | `sched/fastvm/fastvm.cpp` | **44** | 14 | 29 | 1 |

**`src/passes` 全量 305 处，读过 44，还剩 261。**
**本目录（`sched/fastvm/`，共 163 处）里读过 44，还剩 119。**

**下一块**：`sched/fastvm/compile.cpp`（18）与 `casevm.cpp`（14）——**同一个 fastvm 目录里，编码器那一侧**，正好和第 1 块的 14 条"生成器一致性"断言对照着读。

## 第 2 块：`compile.cpp`（18）+ `casevm.cpp`（14）——**编码器与执行器的两侧**

**读了几处：32**（`compile.cpp` 18 + `casevm.cpp` 14；`casevm.cpp:536` 是注释，不计）。
**本块三类：① 15 / ② 17 / ③ 0 = 32。**

### `compile.cpp`：① 10 / ② 8

| 类 | 行 | 说明 |
|---|---|---|
| ① | 126 | `graph->hasNodePayload()`：materialize 的前置条件 |
| ① | 358, 364, 376 | ACCS 源节点是 composite / `accsKind` 是 `TupleIndex` / 是 `StructKey`：**由类型规则选定** |
| ① | 393 | `ASSERT(false, "Unsupported ACCS source node type")`：类型规则穷尽 |
| ① | 411 | `armEntry != kInvalidNodeRef`：BRCH 臂表由本 pass 自己建 |
| ① | **471** | **`normOps.size() == dstSlots.size()`——这是与 `fastvm.cpp:174/205/254/268/294/301` 同一族的那条 arity**：编码器写下的实参数与执行器算出的槽位数必须相等 |
| ① | 601 | `ASSERT(false, …)`：编码器遇到编不了的操作符 |
| ① | 646, 649 | `brchTargetMap` / `joinTargetMap` 清空：配对过程自己的不变量 |
| ② | 33, 246, 355 | 运行时节点记录为 null（`header()` / 拓扑记录 / 数据类型） |
| ② | 38, 462, 757 | `targetRuntimeGraph` / `target` 为 null——**被调图还没 materialize** |
| ② | 125, 704 | 传入的 runtime graph 为 null（前置条件；来自图集合） |

### `casevm.cpp`：① 5 / ② 9

| 类 | 行 | 说明 |
|---|---|---|
| ① | 153, 159 | 元组/结构体下标 `< size()`：**下标是编码器按类型写进指令的** |
| ① | 212 | JOIN 选中的下标 `< bc.withCnt()`：同上 |
| ① | 232 | `isGCTraced(srcType->code())`：类型规则 |
| ① | 164 | `ASSERT(false, "ACCS opcode unsupported source type")` |
| ② | 234, 237 | FILL 的源对象 / 目标数据为 null |
| ② | 253 | CALL 解析出的 `Function` 为 null |
| ② | 255, 289, 302, 334, 353, 394 | `runtimeTarget` / `runtimeTailTarget` 为 null——**被调图还没 materialize** |

### 对照读出来的结论（这一块真正的产物）

**同一件事在两个文件里各有一条**：
* `compile.cpp:471`（编码器：实参数 = 槽位数）与 `fastvm.cpp:174/205/254/268/294/301`（执行器：同一等式）——**两侧各断言一次同一件事**。这正是"① 守在两个生成器之间"的意思：**任何一侧改了布局而另一侧没改，两边都不会崩，只会算错**。
* `casevm.cpp:153/159/212`（执行器按指令里的下标取值）与 `compile.cpp:358/364/376`（编码器按类型决定那个下标）——**同样一件事的一对**。

**而当两侧都断言同一件事时，有一个代价**：Debug 下真出错会在**先跑到的那一侧**停下，另一侧那条永远不会响。**它们不是两条证据，是一条证据在两个地方站岗**——所以这七对不能当成"七次验证"来数。

## 那 29 条 ② 的可执行分界

"来源是图里的表"**不等于**"可能为空"。判据要落到"这张表是谁写的"：

| 来源 | 判法 | 处理 |
|---|---|---|
| **穿过 `GraphTransaction::decode(...)`**（反序列化、宏克隆、`GraphDraftGroup`、pass 改写后的图） | **必须判**：外部字节能把它写成任何形状 | ②，且**断言要在 Release 里活着**（`ASSERT` 不够） |
| **同一函数内刚建的表**（`addNode` 之后立刻 `header(id)`，中间没有外部调用） | **不必判**：写入与读出在同一个函数体里，可证明非空 | ①（保留 `ASSERT` 即可），或直接去掉 |
| **编码器同一次运行写下的常量**（arity、指令里的下标、opcode） | **不必判**：生成它的代码与检查它的代码在同一次编译里 | ① |
| **只由前端建、今天还没有 decode 路径能到达**（如 `compile` 只跑在前端刚建好的图上） | **暂定①**，并**写一条"何时转②"的条件**：一旦有 decoder 能产出这种图，这一条就转② | ①（附条件） |

**机械版本（可以照着查）**：**沿调用链往上追，能追到 `decode(` 的一律按②；追不到、且写与读在同一函数体内的一律按①。** 中间那种（追不到但也不知道会不会有一天被 decode 出来的）**必须带上"何时转②"的句子**，否则下一次普查会被重新判一遍。

**用这条分界回看第 1 块**：那 29 条②里，**调用帧、`Function`、运行图、闭包端口、程序根**——五组的来源全是图里的表，而 `fastvm` **今天就能跑 decode 来的图**（`fastvm.cpp:1072` 的 JIT 出口就是从那类图里读字节码的），所以**五组全部成立为②**，一条都不该降回①。

## 进度

| 块 | 目录/文件 | 读了几处 | ① | ② | ③ |
|---|---|---|---|---|---|
| 1 | `sched/fastvm/fastvm.cpp` | **44** | 14 | 29 | 1 |
| 2 | `sched/fastvm/compile.cpp` + `casevm.cpp` | **32** | 15 | 17 | 0 |

**`src/passes` 全量 305 处，读过 76，还剩 229。**
**本目录（`sched/fastvm/`，共 163 处）里读过 76，还剩 87。**

**下一块**：`fastvm/gotovm.cpp`（17）+ `trampoline.cpp`（30）——**执行器的另外两个入口**，正好把"执行器一侧"读完。