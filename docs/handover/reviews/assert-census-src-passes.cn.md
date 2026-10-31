# `ASSERT` 普查：`src/passes`（逐块读，只算读过的）

> 判据：**一份分类表只算到"读过的那些"；表上必须写读了几个、还剩几个。**
> 三类的定义（用于本表，写清楚才可复现）：
> - **① 不该发生**：条件是"本函数/本编码器/类型系统建立的不变量"，违反＝编译器或前端有 bug，**任何输入都造不出来**。Release 下这种断言消失后行为只是"继续跑错"。
> - **② 可能为空**：检查的是指针/下标/长度，**一份畸形或被改坏的图能让它真的为空**，Release 下会崩（段错误或越界），不是"逻辑跑错"。
> - **③ 纯防御**：**同一事实在调用点或前几行刚刚检查过**，断言只是重复一次。

## 先看这两条——它们决定这张表怎么读

**算法（② 与 ① 的分界，机械版本）**：

> **沿调用链往上追，追得到 `decode(` 的一律按 ②；追不到、且写与读在同一个函数体内的一律按 ①；中间态（追不到，但也不知道会不会有一天被 decode 出来）必须带一条"何时转 ②"的句子，否则下一次普查会被重新判一遍。**

**报数口径（证据数 ≠ 断言数）**：

> **两条断言守同一件事时，先问"是不是同一条证据在两次站岗"；是的话，数字要按证据数报，不按断言数报。**

**为什么这条口径必要**：Debug 下真出错只会停在**先跑到的那一侧**，另一侧那条永远不响。所以 `compile.cpp:471` 与 `fastvm.cpp:174/205`、`trampoline.cpp:90` 对外开放的是**同一条等式**，不是三份保证。**全仓库的断言总数里，有一族的实际验证次数比断言条数少一半**——凡是要报"断言覆盖率"的地方，都得先扣掉这一半。

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

## 第 3 块：`gotovm.cpp`（17）+ `jit/runtime/trampoline.cpp`（30）

**读了几处：47**（`gotovm.cpp` 17 + `trampoline.cpp` 30；两处 `:926`/注释不计）。
**本块三类：① 19 / ② 28 / ③ 0 = 47。**

* `gotovm.cpp`：**① 7 / ② 10**
  * ①：`36/43` 嵌套 JIT save 栈的界与下溢、`379/385` 访问下标、`390` 不支持的 ACCS、`471` JOIN 臂下标、`498` `isGCTraced`
  * ②：`500/503` FILL 源对象与目标数据、`520` `Function`、`522/565/585/636` `runtimeTarget`、`666/692/770` `runtimeTailTarget`
* `trampoline.cpp`：**① 12 / ② 18**
  * ①：**`90` 直接调用 arity**、`490/496/502` 下标、`507/563/673/724` 四种"不支持的 opcode/源类型"、`517/542` 的 `!nargs.empty()`/`!wargs.empty()`（FILL/CALL 的形状由编码器决定）、`521` `isGCTraced`、`699` JOIN 臂下标
  * ②：`89` 目标帧、`218/252/287/306/332/353/546` `runtimeTarget`、`390` `runtimeGraph`、`523/525` FILL 源与目标、`544` `Function`、`684` JIT helper 的源、`758/769/779` `bc.extra()->loop`、`329/350` `bc` 本身为 null

## 成对表：**编码器侧 vs 执行器侧，谁先响**

**左列是编码器写下的检查，右列是执行器对同一件事的检查**。中间一列说明 Debug 下真出错时**先响的是哪一侧**——**先响的那条才是"这一次验证"，另一条只是备份**。

| # | 编码器侧（先响 →） | 同一等式 | 执行器侧（后备） |
|---|---|---|---|
| 1 | **`compile.cpp:471`** `normOps.size() == dstSlots.size()` | 直接调用的实参数 = 目标槽位数 | `fastvm.cpp:174`、`:205`；**`trampoline.cpp:90`** |
| 2 | `compile.cpp:358/364/376`（源是 composite、kind 是 `TupleIndex`/`StructKey`） | 访问下标是类型给的，且 `< 长度` | `casevm.cpp:153/159`、`gotovm.cpp:379/385`、`trampoline.cpp:490/496/502` |
| 3 | `compile.cpp:601`（编不了的操作符） | 每个 opcode 都有实现 | `casevm.cpp:164`、`gotovm.cpp:390`、`trampoline.cpp:507/563/673/724` |
| 4 | `compile.cpp:411`、`:646/649`（BRCH/JOIN 配对） | JOIN 臂下标 `< withCnt()` | `casevm.cpp:212`、`gotovm.cpp:471`、`trampoline.cpp:699` |
| 5 | （前端捕获表 → 闭包长度） | 闭包元组长度 = 捕获端口数 | `fastvm.cpp:230`、`trampoline.cpp:542/544` |
| 6 | **`compile.cpp:126`**（materialize 前置） | 执行前必须有节点负载/帧布局/循环体 | `fastvm.cpp:1244`、`trampoline.cpp:758/769/779` |

**读这张表要知道的三件事**：

1. **第 1 行是这一族里最典型的一条**：同一等式在 **3 个文件、4 处**各写了一遍（`compile.cpp:471`、`fastvm.cpp:174`、`fastvm.cpp:205`、`trampoline.cpp:90`），**实际只验一次**。
2. **"谁先响"不是固定的**：第 1、2、6 行编码器先响（它在前面）；**第 3 行执行器先响**——编不了的操作符在 `compile.cpp:601` 那条反而**最先**（编码器遇到不认识的类型就停），而 `casevm/gotovm/trampoline` 那四条的是一直没轮到。
3. **所以"加了 30 条 `trampoline` 断言"不等于"多了 30 次验证"**：这 30 条里 **12 条属于①，其中大半在成对表里已经有一份**。**报覆盖率之前，先按这张表把重复的扣掉。**

**"`fastvm` 今天就能跑 decode 来的图"的出处**（免得下一个人重新问）：`fastvm.cpp:1072` 的 JIT 出口按 `bc->opcode == OpCode::CALL` 读**解码图的字节码**；`fastvm.cpp:1244` 的 `hasFrameLayout()` 也说明它按"已经 materialize 过的图"工作。**这两处就是第 1 块那 29 条②不降回①的理由。**

## 第 4 块：`sched/fastvm` 收口的 41 处（`bytecode.h` 20、`runtime_support.cpp` 4、`bytecode.cpp` 2、`fastvm.h` 2、三个 dump 6、`x64_backend.cpp` 3、`bcdump.cpp` 1、`hosted.cpp` 1、`optimize.cpp` 1、`optimize.h` 1）

**读了几处：41。本块三类：① 27 / ② 14 / ③ 0。**

| 类 | 处 | 说明 |
|---|---|---|
| ① | `bytecode.h` 14 条 | **按 opcode 种类取数的前置条件**（`isDirectCallOpcode`、`extraWords`、`== OPER`、`LBEG/LTST/LEND`）：调用者刚 decode 过那个 opcode，种类是可证的 |
| ① | `bytecode.cpp:137/155` | `ASSERT(false, "Unknown OpCode")`：枚举穷尽——**与成对表第 3 行同一条** |
| ① | `runtime_support.cpp:28`、`optimize.h:51` | BRCH 恰一个 norm 操作数 / 查询只对 BRCH 有效——**成对表第 4 行** |
| ① | 三个 dump 的 `hasFrameLayout` ×3 | **成对表第 6 行**（对方 `compile.cpp:126`、`fastvm.cpp:1244`） |
| ① | `x64_backend.cpp:125/264/430` | `graphEnd <= unit.bytecodes.size()`：链接器自己算出的界 |
| ① | `hosted.cpp:40`、`optimize.cpp:45` | 生命周期"只建一次"、`from > to` 的调用方不变量 |
| ② | `bytecode.h` 6 条 | **`extraWords >= 2/3`**：读第 2/3 个附加字前检查长度——**解码来的字节码可能更短，这是真的会越界读** |
| ② | `fastvm.h:429/438`、`bcdump.cpp:61`、三个 dump 的 `runtimeGraph` ×3 | 运行图为 null |
| ② | `runtime_support.cpp:24/67` | 分支选择的帧、写捕获变量时被调图的图 |

### 按纪律标出的重复（7 条，不计入"需要活在 release"的数）

| 本块的重复 | 对方 |
|---|---|
| 三个 dump 的 `hasFrameLayout` ×3 | `compile.cpp:126`、`fastvm.cpp:1244` |
| `bytecode.cpp:137/155` | `compile.cpp:601` |
| `runtime_support.cpp:28`、`optimize.h:51` | `compile.cpp:411`、`casevm.cpp:212` |

### `gotovm`/`trampoline` 那 5 条重判（照算法追它们的来源）

| 处 | 原判 | 重判 | 理由 |
|---|---|---|---|
| `trampoline.cpp:758/769/779` `bc.extra()->loop != nullptr` ×3 | ② | **①（附条件：一旦有 decoder 能产出没有 `ExecLoop` 的字节码，转②）** | 那个循环体是**同一次编译里 `compile.cpp` 填进 `extra()` 的**，写与读在同一次 decode/链接之内 |
| `trampoline.cpp:329/350` `bc` 非空 ×2 | ② | **①（同上）** | 同上：`prepareDirectJit*` 收的是同一次链接产出的字节码引用 |

**所以这 5 条不是"必须判"，是"同一次 decode 内可证非空"**——把它算进 Release 必须活的数，会让那个数虚高 5。

### 一条判据：**成对断言的价值在"哪一处永远不会响"**

> **"成对断言的价值不在于'两处都守'，而在于'知道哪一处永远不会响'；不会响的那一处，要么删，要么降成 debug-only。"**

**从这一块起的纪律**：凡判为 ①、**且已在成对表里有一份**的，一律在表里标 **"重复（对方 ile:line）"**，**不再计入"需要活在 release 的断言数"**。

## `sched/fastvm` 整目录完成

**这一目录 163 处，全部读过。三个累计数**：

| 数 | 值 |
|---|---|
| **断言数**（`\bASSERT` 出现次数） | **163** |
| **证据数**（扣掉成对表里重复的 33 条） | **130** |
| **两者差** | **33** |

三类：**① 80 / ② 83 / ③ 0**（另有一处 `CAMEL_ASSERT_NOT_VIEW`，不在这 163 的计数口径里，单列为③）。

**33 这个差就是这个目录的用处**：**每六条断言里有一条永远不会说话。** 而 ① 的 80 条里，**33 条在成对表里有对方**——**真正"只在执行器这一侧、违反就两边都不知道"的证据是 47 条**。要换成 Release 也在的断言，**该换的是这 47 条，不是 80 条**。

## 进度

| 块 | 目录/文件 | 读了几处 | ① | ② | ③ |
|---|---|---|---|---|---|
| 1 | `sched/fastvm/fastvm.cpp` | **43** | 14 | 29 | 1 |
| 2 | `sched/fastvm/compile.cpp` + `casevm.cpp` | **32** | 15 | 17 | 0 |
| 3 | `sched/fastvm/gotovm.cpp` + `jit/runtime/trampoline.cpp` | **47** | 24 | 23 | 0 |
| 4 | `sched/fastvm` 其余 11 个文件 | **41** | 27 | 14 | 0 |
| — | **合计** | **163** | **80** | **83** | **1** |

**`src/passes` 全量 305 处，读过 163，还剩 142。**
**本目录（`sched/fastvm/`，共 163 处）读完，剩 0。**

**下一块**：出目录，进 `sched/nodevm`（47）与 `sched/taskflow`（71）——**这两块归另一条车道**；本车道接着读 `sched` 剩下的与 `trans`/`opt`/`test`（142 处里除去 118）。