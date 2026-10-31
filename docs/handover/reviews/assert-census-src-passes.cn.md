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