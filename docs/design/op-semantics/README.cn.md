# 算子语义协议与图事实（设计稿）


> **2026-10-15 已裁定**：第 15 节的 Q1–Q11 均按本稿建议（矩阵乘的规范算法取乙，按 k 递增的 FMA 链）；见设计原则 2.13。
>
> **实施状态（第一阶段已实现，`lane/opsem-p1`）**：CPU 内核按 2.13 改正（`max`/`min` 用 IEEE `maximum`/`minimum`，`relu = maximum(x, +0)`，GEMM 是按 k 递增的显式 FMA 链，`softmax` 与定义逐位一致）；`OperatorSemantics` 记录与查询接口（`camel/core/operator_semantics.h`，`OperatorTable::declareSemantics`/`semantics`/`key`/`bind`/`declarationEpoch`），内建标量算术与第一批张量算子的数值声明；`@numeric(...)` 标注（函数与块）、每节点一字节的许可列、改写时的传播（内联、克隆、CSE、`tensor::fuse`）与在 `std::rgir`/`inspect` 里的显示。`LOOP` 的消费接口（第三阶段的 3a）已实现，见第 11 节开头的实施状态；事实库（3b）已实现，见第 10 节开头的实施状态；资源与定义图（第四阶段）已实现，见下面的第四阶段一段与第 8、9 节开头的实施状态。给 Tiles 的说明见 14.1 节。下文是设计稿原文，状态描述以这一段为准。
>
> **第二阶段已实现（`lane/opsem-p2`）**：下标语义的数据结构与查询（`camel/core/index_semantics.h`：准仿射表达式、迭代域、访问与写映射、四种越界策略、间接访问的形式、归约律与 `reorderFreedom`、`splitDims`、`rowMajorView`；`camel/runtime/operator_instance.h`：冻结图节点上的实例）；第一批声明（逐元素族连同广播、`transpose`/`permute`/`slice`/`reshape`/`flatten`/`unsqueeze`/`broadcast_like`/`expand_axis`、`sum`/`mean` 全量与按轴、`max`/`min` 按轴（张量模块没有全量的 `max`/`min`）、`sum_to`、`matmul`、`nn` 的两种池化作为窗口的例子、`unordered_reduce` 的律）；`camel inspect --instances`；声明求值器 `tensor::test_index_eval`（与 CPU 内核逐位比较，见第 12 节）。给 Tiles 的说明见 14.2 节。与本稿字面不同的几处（均为窄读，见 14.2 与各处注释）：实例只有一个 `law` 下标（`Write` 不另存）；`Domain::guard` 表示分块尾部不存在的点；`LawInit::Operand`（`unordered_reduce` 的 `initial`）；`LawEmpty::Fault` 指归约维为空即故障（`max_axis` 的内核在结果为空时也拒绝空轴）；声明附在张量算子的 `OpDef::index` 上，与 `infer` 并列。
> **第四阶段已实现（`lane/opsem-p4`）**：资源的声明与查询（`camel/core/resource_semantics.h`、`camel/runtime/resources.h`；事实种类 `core.resources`），见第 8 节开头；定义图的登记、构建与检查、展开（`OperatorTable::declareDefinition`、`camel/runtime/definitions.h`；`stdlib/tensor_definitions.cml`），见第 9 节开头。给 Tiles 的说明见 14.4 节。没有改变图的转储；程序的结果只有 `log_softmax` 的变了（内核改为照它的定义，Q10）；没有遍或后端默认展开定义图。`Indirect` 访问的 gather/scatter 算子仍不存在（数据结构自第二阶段就有），本阶段没有声明。

状态：**设计稿，待语言作者过目。本稿不含任何源码改动。**
依据：`docs/design-principles.cn.md`（2.9–2.13，尤其 2.13 的"浮点的数值契约"与 `do` 式循环头；第 6 节）；[图的数据模型](../graph-model/README.cn.md)（3.5 节点卡片、3.8 算子表、3.10 不变量、第 4 节存储、5.1 C++ 接口、第 7 节迁移计划）；[循环](../loops/README.cn.md)；[Indexed Computation, Schedules, and Resource Graphs](../indexed-computation/README.md)（下称 **IC 稿**）；[`modules/tiles`](../../../modules/tiles/README.md)。
基线提交：`6bf31d1f`（`fsh-dev`，v0.2.12 合入之后）。
相关：物理映射、`std::tiles` 遍的入口与块级内核语言见 [Tiles 的路线](../tiles-route/README.cn.md)（`std::tiles` 遍的入口与块级内核语言；下称 tiles-route），本稿不涉及。
提出者：Tiles（CUDA 编译模块）的开发方，两轮需求；分工已经商定：**核心提供通用的协议与图结构；CUDA 映射、布局、Tensor Core、调度、流与事件、内存池、编译缓存、自动调优由 Tiles 实现。**

文中每条事实标注来源：

- 【实测】在本仓库构建出的 `camel` 上实际运行看到的（附录 A 给出命令）；
- 【读码】读代码得到、没有运行验证的（给出文件与行号）；
- 【未查】没有核实的。

C++ 声明与 Camel 示例都是**拟议**的写法，现有编译器不接受；带"示意"字样的 Camel 写法以负责人的裁定为准。

---

## 0. 摘要

**要解决的问题。** 今天一个遍或后端能从核心问到的、关于一个算子的全部信息，是算子表里的五个位（`pure`、`elementwise`、`stateOnly`、`mayFault`、`callsGraphs`）加一个类型规则【读码：`include/camel/core/operator_table.h:116-138`】。Tiles 要把张量图编译成 GPU 内核，需要知道的远不止这些：输出的每个元素读了输入的哪些元素（下标映射）、哪些维度是归约、归约按什么顺序用什么精度累加、允许不允许重排、可能在哪里出故障、写了什么。今天这些全由 Tiles 按算子名字自己写死（2.2 节）：一份新的后端就要再写一遍，一个新的模块算子 Tiles 就不认识。本稿给出一份**所有遍与后端都经由它理解模块算子的通用接口**，以及让优化器永远不会用到过期分析结果的**图事实机制**，再加上 `LOOP` 节点的消费接口。

**七个决定。**

1. **算子表旁边加一份带版本的 `OperatorSemantics` 记录**，与 `OperatorTraits` 并存：traits 是热路径上的五个位，semantics 是按需查询的富信息，各部分都可以缺省，缺省一律按保守处理。算子的**稳定标识**是 `protocol:name@主.次`（如 `tensor:matmul@1.0`）；`OperId` 仍是进程内的编号，离开进程的东西（`.cmo`、Tiles 的编译缓存）按稳定标识加版本绑定（第 4 节）。
2. **算子的原始语义与程序授予的许可分开。** 算子的声明只说它自己做什么（规范算法、累加类型、顺序、特殊值规则、故障条件）；五项数值许可只能由程序写在代码上，存在图上与节点平行的一列里，模块无从扩大（第 6 节）。
3. **下标模型是"迭代域 + 每个操作数的访问映射 + 输出的写映射"，表达式是封闭的准仿射语言**（加、减、乘常数或参数、整除与取模），einsum 串只是简写；数据相关的地址（gather/scatter）是一种显式的"不可分析但声明了边界"的访问。声明是秩多态的，所以算子注册的是一个**实例化函数**：给定实参的类型与静态实参，得出这个节点上的具体迭代域（第 5 节）。这一点与 IC 稿"映射是一张纯子图"不同，理由在 1.3。
4. **数值契约照 2.13 的裁定实施**：规范算法 + 五项互不蕴含的许可；归约是否可以重排，看的是"这个 dtype 与累加类型下的确切代数律 + 节点上的许可"，不是"加法有结合律"。`max`/`min` 的 NaN 与 ±0 规则今天在 CPU 上是位置相关、不交换的【实测】，建议改为 IEEE 754-2019 的 `maximum`/`minimum`，这使最大值归约在任何顺序下逐位相同而不需要许可（第 6、7 节；Q5）。
5. **图事实是挂在快照上的旁表，每条事实带一个印记**：它所依赖的每张图的修订号、算子声明的纪元、目标配置的散列。读的时候逐项比较整数，发布构建里也查。不能从图上恢复的语义信息（数值许可、用户约束、定义图的绑定）**不是事实而是图的内容**，随图块进缓存；可推导的事实重算，缓存要验印记；与目标相关的结果不进核心（第 10 节）。
6. **`LOOP` 的消费者拿到一个统一的访问器**（初值、条件图与循环体图的形参、下一轮状态、捕获的对应关系），提交时核心校验状态类型、子图接口与效应摘要；**降为尾递归只发生在各后端自己的入口、在它私有的事务里**，不得在共享的程序上不可逆地提前做（第 11 节）。
7. **定义图**（算子的完整语义展开，如带数值稳定化的 softmax）由核心管理生命周期、版本与递归检测；后端可以保留高层算子，也可以展开，核心从不强迫提早展开（第 9 节）。

**对 Tiles 最重要的三件事**：（一）第一阶段就能拿到注册与查询接口、稳定标识与数值契约的位（第 12 节）；（二）Tiles 今天 matmul 里显式的 `fmaf` 与它的容差测试是**审计项**（6.9、第 14 节）；（三）`relu` 在 NaN 与 −0 上 Tiles 与 CPU 的结果不同【读码 + 实测】，在逐位相同的契约下必须先定一个（Q6）。

**需要负责人确认的问题**（第 15 节，每题附建议）：许可的名字与 (e) 的粒度；授予的作用范围（函数、块、分支臂、循环体、lambda）；模块级策略；每个函数的 ulp 上界；`max`/`min` 的 NaN 与 ±0；`relu` 的特殊值；`matmul` 的规范算法是"分开的乘与加"还是"按 k 顺序的乘加融合链"；后端入口能否接受许可；提高累加精度算不算许可 (d)；定义图与融合内核谁是规范。

---

## 1. 与 IC 稿的关系

IC 稿（2026-10-15，`wzj-dev`）是一份**拟议的目标架构**：计算图、调度图、资源图三层。它不是语义裁定。本稿是其中**计算图语义层**在核心里的落地协议，外加 IC 稿没有细化的图事实机制。不重复 IC 稿的内容，只说对应关系。

### 1.1 本稿实现 IC 稿的哪些部分

| IC 稿 | 本稿 | 状态 |
|---|---|---|
| 3.1 `Domain`（有限、有序的下标元组集合） | `IndexedInstance::domain`：维度、范围、符号参数、线性约束（5.1） | **实现**（矩形域加线性约束；`SegmentDomain` 不在内） |
| 3.1 `IndexMap`、`Predicate` | 访问映射（准仿射表达式）与有效条件（线性不等式的合取）；数据相关的地址是单独的一类（5.1、5.6） | **实现，形式不同**（1.3 D2） |
| 3.1 三种映射：逻辑访问、存储、执行布局 | 只有**逻辑访问映射**进核心；存储映射与执行布局归 Tiles（5.7） | 按分工 |
| 3.2 `scatter_reduce` 的"重复目标要有合并策略" | 写映射的三种模式：唯一、按归约律合并、按逻辑顺序覆盖（5.6） | **实现**（作为声明的词汇；算子本身不在本稿） |
| 3.3 构造的装配模式（唯一放置、归约、有序覆盖） | 同上，写映射的模式 | **实现**（词汇） |
| 3.4 归约器、有序折叠 / 有序树 / 无序 | `ReductionLaw`（第 7 节）；三种顺序映射到"许可 (a) + 律的确切交换性" | **实现，按 2.13 重述**（1.3 D4） |
| 4 每个操作的算术契约（存储、计算、累加类型，输出转换，重排、FMA、近似、特殊值、复现范围） | `NumericSemantics`（算子的原始语义）+ 节点上的许可列（第 6 节） | **实现，按 2.13 裁定**（1.3 D1） |
| 4 故障契约（不能忽略、不能投机执行会故障的操作） | `FaultSpec`：故障的种类与条件（4.2） | **实现**（声明层面；"并行时报哪个故障"仍是第 6 节的未决项） |
| 6.2 资源的读写区域、别名、逃逸 | `ResourceSpec`：资源身份、区域、访问方式、结果与输入的别名（第 8 节） | **实现语义层**；能力、版本、事件不实现 |
| 7 表的前五项接口：Shape/domain、Indexing、Effects/resources、Algebra/numerics、Decomposition | `OperatorSemantics` 的五个部分（4.2） | **实现** |
| 7 "提交时重新校验结构类型与效应摘要；变换使域、别名、布局、数值、资源事实失效或重建" | 图事实的登记、印记、失效与重建（第 10 节） | **实现机制** |
| 2 "`LOOP` 按已裁定的状态表达式设计；运行期枚举还没有它" | `LoopView` 访问器、校验、降级的时机（第 11 节） | **实现消费接口**（节点本身由图模型第 10 步做） |

### 1.2 仍是 IC 稿里拟议的

- **调度图**（IC 5）与**资源图**（IC 6）：全部。物理映射与块级内核语言见 tiles-route。它们是 Tiles 一侧（或将来的通用调度层）的表示；本稿只保证它们需要从计算图读的语义事实有地方读。
- **集合算子**（IC 3.2 的 `tabulate`、`reindex`、`gather`、`fold`、`reduce`、`scan`、`scatter_reduce`、`construct`、`contract`、`random_at`）：它们将来是模块算子，用本稿的协议声明自己；本稿不新增任何算子。
- 车道谓词化的子图、`SegmentDomain` 与不规则域、计数器式随机数、MLIR/Triton 的选择、IC 第 7 表的后四项接口（Differentiation、Scheduling、Target lowering、Cost）。其中 Differentiation 今天已经在张量模块的 `OpDef::vjp` 里【读码：`modules/tensor/ops/op_def.h:140`】，本稿不动它；后三项按分工归 Tiles，Tiles 可以用本稿的稳定标识做键自建登记表。

### 1.3 本稿与 IC 稿不同的地方（附理由）

- **D1 数值策略。** IC 稿第 4 节拟议三个命名策略 `source`/`reproducible`/`relaxed`，并说"不把跨后端逐位相同的保证追加到现有的 `sum`、`matmul` 上"。负责人 2.13 的裁定取代了这一点：基准是规范算法，加减乘除与开方逐步正确舍入、按书写顺序、不收缩，**在这个范围内各后端、各设备逐位相同**；放宽是五项互不蕴含的显式许可。所以本稿没有命名策略，只有"原始语义 + 许可集合"；`sum`、`matmul` 各自要声明一个规范算法，现有的实现要按它审计（6.9）。IC 稿"测试的误差容限不是重排的许可""调度不能悄悄削弱数值许可"两条与裁定一致，照用。
- **D2 映射的形式。** IC 稿把 `IndexMap` 定为"一张把下标元组与参数映射到坐标的纯子图"。本稿建议**主形式是一个封闭的准仿射表达式语言**（5.1），纯子图只作为"数据相关 / 不可分析"时的退路。理由：(1) 可分析——依赖、越界、单射、覆盖的判定落在线性整数算术上，有确定的算法与超时退回"未知"的分级（IC 5 自己也这样分级）；(2) 可序列化——描述是驻留的数据，进缓存键、比较相等都是常数时间，不占图的 65,535 容量，不需要图引用；(3) 可组合——Tiles 今天已经在把 transpose、slice、broadcast 的映射复合成一条地址式【读码：`modules/tiles/src/codegen.cpp:39-49`】，描述语言上的复合是代入。子图形式这三样都要另做一套。
- **D3 维度的第三种角色。** IC 稿的角色只有并行与归约。本稿加 **Sequential**（有序递推）：`LOOP` 的 `for`、`fold`、`scan` 的那一维。它不能并行、也不是归约（每一步都有可观察的中间状态），但它的下标映射与域照样有用（5.1、11.3）。
- **D4 有序树与无序。** IC 稿区分"保持元素顺序、允许重新加括号"与"允许置换与重新加括号"。2.13 的许可 (a) 只有一项"改变归约的顺序或结合方式"。本稿的对应：许可 (a) 授予**重新加括号**；**置换**另外要求这个律在这个 dtype 上是**确切可交换**的（浮点加、乘、IEEE `maximum` 是，位置相关的 `max`、字符串连接、矩阵积不是）。不可交换的律在许可 (a) 下只能保序分组。这样不需要第六项许可，又不会把"可以改舍入"误读成"可以改数学结果"。
- **D5 描述放在哪。** IC 稿说域、布局、输出规格、数值策略是"类型化的值或驻留的描述"。本稿：算子的**声明**是算子表里驻留的描述（不是图上的值）；一个节点上**实例化**后的域是可推导的事实（第 10 节的第 (ii) 类），按需算、带印记缓存；只有程序写下的、图上推不出来的东西（数值许可等）是图的内容。布局不进核心。
- **D6 接口的边界。** IC 第 7 表把 Scheduling、Target lowering、Cost 也列为算子接口。按分工，它们不进核心的 `OperatorSemantics`；Tiles 用 `OperatorKey` 做键自己登记，核心不读它们。
- **D7 资源。** IC 6 的能力、内存版本、完成事件、屏障、集合通信都留在拟议。核心只给语义层的声明（第 8 节）与保守的缺省；`sync`、`wait` 的含义不变，不成为 GPU 的同步原语（IC 2 与 Tiles 的要求一致）。

---

## 2. 现状

### 2.1 核心里关于算子的全部信息

【读码：`include/camel/core/operator_table.h`】

- `OperId` 是 32 位的表下标，**进程内**有效；离开进程的东西"必须存 URI、装载时重新驻留"（文件头注释）。核心认得的 11 个算子有编译期常量编号（`oper::VarGet`、`oper::UnorderedReduceArr` 等）。
- 每个表项：URI、族标志（内建、`:mark/`、赋值）、一个原子字节的 traits、类型规则（resolver）、类型折叠函数、就地形式。
- `OperatorTraits` 五个位：`pure`、`elementwise`（"输出元素 i 只依赖输入元素 i（广播之后）"）、`stateOnly`、`mayFault`（缺省为真）、`callsGraphs`。没有声明的算子按"有效应、可能故障"处理（2.9）。
- 张量模块的 `OpDef` 带自己的 `OpTraits{pure, elementwise}`、`infer`（形状推断）、`vjp`、`foldFromTypes`【读码：`modules/tensor/ops/op_def.h:127-145`】；注册时只把 `pure`、`elementwise` 写进核心的算子表【读码：`modules/tensor/ops/registry.cpp:183-185`】，所以张量算子的 `mayFault` 一律是缺省的"可能"。
- 图上：节点 16 字节定长，每图一张图引用表（图模型第 3 步的 3b、3c 已合入）；每张图一个四位的效应摘要（图模型 3.3）。**运行期的节点枚举里还没有 `LOOP`**【读码：`include/camel/runtime/graph.h:93-109`，0–14，没有 15】。

### 2.2 Tiles 今天自己算的东西

【读码：`modules/tiles/src/*.cpp`，基线 `6bf31d1f`】Tiles 的编译器把一张函数图在调用时快照成私有的 IR（`Program`），再调度、生成 CUDA、用 NVRTC 编译。下表是它今天**自己写死**、本协议要接过来的东西：

| 方面 | Tiles 今天怎么做 | 位置 |
|---|---|---|
| 认算子 | 内建标量算子：把五个 URI（`:op/add_d` 等）各驻留一次，按 `OperId` 查表；张量算子：`OpRegistry::find(OperId)` 取 `OpDef`，之后**按 `def->name` 的字符串**分派 | `compiler.cpp:292-300`、`:326-327`，之后各 `n.op == "..."` |
| 纯度 | 要求 `def->traits.pure`，否则拒绝 | `compiler.cpp:327` |
| 逐元素 | 两份写死的名字列表 `"|neg|abs|exp|log|sqrt|rsqrt|relu|"` 与二元的一份；与核心的 `elementwise` 位无关 | `compiler.cpp:418-419`、`schedule.cpp:42-48` |
| 形状 | 复用张量模块的 `infer`，要求结果是静态形状的 f32 | `compiler.cpp:336-346` |
| 下标映射 | `reindex` 节点：源地址 = 偏移 + Σ 输出坐标 × 步长（元素为单位）；transpose、permute、正步长 slice、`broadcast_like`、`expand_axis` 都翻译成它；广播另由 `broadcastIndex` 生成；reshape 是线性下标不变 | `compiler.cpp:367-397`、`:438-490`；`codegen.cpp:20-49` |
| 归约 | `sum_axis`/`mean_axis`：每个输出串行、f64 累加；`sum_to`：f32、行主序累加（"与 `sum_axis` 的 f64 策略不同"）；`sum`/`mean` 全量：f64；`max_axis`：串行，或宽度 ≥ 128 时带原下标的块内并行，保留首个 NaN 与 ±0 的平局规则；空轴上的 `max` 拒绝 | `codegen.cpp:103-112`、`:118-140`、`:234-285`；`compiler.cpp:436` |
| matmul | 16×16 共享内存分块；K 尾部按 `base+t<k` 排除；**累加用显式的 `fmaf`** | `codegen.cpp:144-168` |
| 数值 | NVRTC 选项 `--fmad=false`（禁止收缩）；`expf`、`logf`、`sqrtf`；`rsqrt` 写成 `1.0f/sqrtf(a)`；`relu` 写成 `a<0.0f?0.0f:a`；融合时每个节点保留 f32 舍入点；"测试用容差比较浮点" | `cuda_backend.cpp:256`；`codegen.cpp:175-226`、`:310-318`；README |
| 故障 | 标量 f64 除法：除数为 ±0 时 `atomicMin` 写一个设备上的故障字，完成时检查，发布结果之前报出；张量逐元素除法保持 IEEE 语义 | `codegen.cpp:91-93`；`cuda.cpp:101-200`；README |
| 调用 | 直接调用递归展开；用 `active` 集合检测递归，深度上限 64；值的上限 4,096 | `compiler.cpp:62`、`:145` |
| 分支 | 只特化：条件在特化时已知才行，只翻译选中的臂 | `compiler.cpp` 的 `Brch`/`Join` |
| 编译缓存 | 进程内、选定设备的上下文里，按**生成的源码文本**查，32 项，LRU | `cuda_backend.cpp:234-275` |
| 存储与生命周期 | 区间分配、单流有序复用、借用的设备输入不可写 | `schedule.cpp` |

最后两行与 CUDA 相关的全部（启动维度、共享内存、流、模块租约）按分工留在 Tiles；其余各行是本协议要提供的。

### 2.3 核心 CPU 内核在数值契约下的现状

2.13 的契约对核心自己的 CPU 实现同样成立。以下是写本稿时顺带查到的，列为核心一侧的审计项（6.9）：

- **GEMM 打开了收缩。** 全项目以 `-ffp-contract=off` 编译，但张量的 GEMM 与一元运算单元"opt back in"：`gemm_generic.cpp`、`unary_generic.cpp` 用 `-ffp-contract=on`，AVX2/AVX-512 单元用 `-mfma -ffp-contract=on`；注释说没有 FMA 的 x86-64 上通用单元"rounds twice"【读码：`CMakeLists.txt:48-54`、`modules/tensor/CMakeLists.txt:37-52`】。也就是说**今天 CPU 的 `matmul` 在不同的处理器上已经可能不逐位相同**。GEMM 是 Goto 式分块（KC 深度的块）【读码：`modules/tensor/kernels/gemm_blocked.inl:25-33`】；每个输出元素沿 k 的累加次序是否严格递增、块与块之间是否从零开始再相加【未查】。
- **`exp` 是自写的多项式**："few ulp over the normal float range"，"NaN inputs are not propagated faithfully"【读码：`modules/tensor/kernels/vmath.h:20-29`】；`tanh`、`sigmoid`、`gelu` 由它导出，误差是绝对误差约 1e-7，不是 ulp。
- **`max`/`min` 的特殊值规则是位置相关的**【实测，附录 A.1】：逐元素 `maximum(a, b)` 有 NaN 时返回 `a`、±0 平局时返回 `a`，所以 `maximum(a, b)` 与 `maximum(b, a)` 不同；`max(x, 0)` 只在**第一个元素**是 NaN 时得 NaN（`[1, NaN, 3]` 得 3，`[NaN, 1, 3]` 得 NaN），`[0, −0]` 得 0、`[−0, 0]` 得 −0。实现是 `std::max(a, b)` 与 `v > best`【读码：`modules/tensor/kernels/elementwise.cpp:263-266`、`reduce.cpp:163-174`】。
- **`relu(NaN) = 0`、`relu(−0) = +0`**【实测】；实现是 `x > 0.0f ? x : 0.0f`【读码：`modules/tensor/kernels/unary_loops.inl` 的 `Relu` 分支】。**Tiles 写的是 `a<0.0f?0.0f:a`，给出 NaN 与 −0**【读码：`modules/tiles/src/codegen.cpp:226`】。两者在逐位相同的契约下不能并存（Q6）。
- **融合的 `softmax` 内核与"max、减、exp、sum、除"的组合不是同一个算法**【读码：`modules/tensor/kernels/reduce.cpp:211-229`】：最大值从 −∞ 起用 `std::max` 累计（于是 NaN 一律被忽略，与 `max_axis` 的"首个 NaN 传播"不同）；`exp` 用 `vmath::exp`；和在 f64 里累加；最后乘以 f32 舍入过的倒数 `1/sum`，而不是除以和。Tiles 把 softmax 写成组合（README），所以"CPU 的 `tensor:softmax`"与"Tiles 的 softmax"在位上不同是预期的；问题在于 `tensor:softmax` 的规范算法到底是哪一个（第 9 节，Q10）。
- **标量浮点除以零是故障**【实测：`let nan = zero / zero` 报 `RuntimeDiag::DivisionByZero`】，**张量逐元素除以零按 IEEE 得 NaN/±∞**【实测：附录 A.1 用 `m / m` 造出 NaN】。两者都与 2.9"除零是故障"的字面不完全一致，但这是张量算子既有的语义（Tiles README 也这样说），本稿不改；声明里如实写（4.2 的 `FaultSpec`）。

---

## 3. 总体结构

```
算子表的表项（进程内，OperId 寻址）
 ├─ uri、族标志、traits（五个位，热路径）、resolver、type folder、就地形式      ← 现有
 └─ semantics → OperatorSemantics（只读、驻留、带版本；可以为空）               ← 新增
        key            protocol:name@主.次
        indexing       实例化函数：（实参类型，静态实参）→ 迭代域 + 访问映射 + 写映射
        laws           归约律（累加类型、初值、顺序、确切的结合与交换性）
        numeric        原始数值语义：规范算法、舍入点、ulp 上界、特殊值、对哪些许可敏感
        faults         故障的种类与条件
        resources      读写的资源、区域、结果的别名
        definition     定义图（可选）

图（每张冻结图）
 ├─ 节点、输入表、负载、图引用表、位置列 ……                                     ← 图模型第 3、4 节
 └─ 许可列（可选，每节点 1 字节：五项数值许可的位集；全为 0 时不存）              ← 新增，第 (i) 类信息

快照（Program）
 └─ 事实库 FactStore：（事实种类，锚点）→ 值 + 印记                              ← 新增，第 (ii) 类信息

Tiles（不在核心）
 └─ 布局、调度、内核、编译缓存：键里含 OperatorKey、许可、目标、编译器版本 ……      ← 第 (iii) 类信息
```

查询一个节点"能做什么"时，三样东西合起来看：**算子的原始语义**（声明）+ **这个节点被授予的许可**（许可列）+ **这个节点上已知的事实**（事实库，印记有效）。没有声明的部分、读不到的事实，一律按保守处理。

---

## 4. 注册与查询接口（C++）

### 4.1 稳定标识与版本

```cpp
namespace camel::core::sem {

/// The identity of an operator's semantics outside the process: "tensor:matmul@1.0".
/// protocol and name are the two halves of today's URI (":op/add_d" has an empty protocol).
struct OperatorKey {
    SymId protocol;
    SymId name;
    uint16_t major = 1; // a change that alters results, faults, accesses or effects
    uint16_t minor = 0; // added facts that are true of every earlier minor version
};

} // namespace camel::core::sem
```

- **主版本**变：结果、故障、访问、效应的任何一项改变（例如 `max` 的 NaN 规则按 Q5 改了，`tensor:max_axis` 从 1.x 变成 2.0）。**次版本**变：只是多声明了事实，旧版本声明的都仍然成立（例如给 `tensor:slice` 补上下标映射）。
- 依赖算子语义的持久化结果（`.cmo` 里的事实节区、Tiles 的编译缓存）记下它用到的每个算子的 `OperatorKey`；装载时按 `protocol:name` 重新驻留得到本进程的 `OperId`，**主版本不同即失效，本进程的次版本低于记下的也失效**（记下的结果可能用了本进程没有的事实）。这是图模型 4.5 的 `OPRS` 节区加一个版本字段（10.7）。
- `OperId` 的含义不变：进程内编号，热路径上比较它。`OperatorKey` 只在跨进程的边界上用。

### 4.2 记录

```cpp
namespace camel::core::sem {

enum Permission : uint8_t {          // design principles 2.13; names are provisional (Q1)
    kReassociate      = 1 << 0,      // (a) reorder or reassociate a reduction
    kContract         = 1 << 1,      // (b) contract a multiply and an add into an FMA
    kApproxMath       = 1 << 2,      // (c) approximate math functions
    kChangePrecision  = 1 << 3,      // (d) reduced input or accumulation precision (TF32, bf16)
    kAssumeSpecials   = 1 << 4,      // (e) extra assumptions about NaN, Inf, -0, subnormals
};
using PermissionMask = uint8_t;

enum class NumericClass : uint8_t {
    Exact,             // no rounding at all: moves, selections, integer operations
    CorrectlyRounded,  // a fixed sequence of correctly rounded IEEE steps (the canonical algorithm)
    Bounded,           // transcendental: within ulpBound of the exact result
    Opaque,            // nothing declared: every permission is assumed to matter
};

struct NumericSemantics {
    NumericClass cls = NumericClass::Opaque;
    float ulpBound = 0;                 // Bounded only
    type::TypeCode compute = {};        // type in which the steps are rounded (f32, f64)
    SpecialValues specials;             // NaN propagation, signed zero, infinities, subnormals
    PermissionMask sensitiveTo = 0xFF;  // permissions that can change this operator's result
    std::string_view algorithm;         // the canonical algorithm, in words, for the reference
    ReferenceFn reference = nullptr;    // optional canonical evaluator used by property tests
};

enum class FaultKind : uint8_t {
    DivisionByZero, IndexOutOfRange, EmptyReduction, ConversionRange, ShapeMismatch, Other };
struct FaultSpec {
    FaultKind kind;
    uint8_t operand;                    // the operand whose value triggers it, 0xFF if none
    // IndexOutOfRange: which access (5.1) is checked; the fault is raised before any write
};

struct OperatorSemantics {
    OperatorKey key;
    InstantiateFn indexing = nullptr;   // 5.1; nullptr: no index facts (an opaque operator)
    std::vector<ReductionLaw> laws;     // 7; indexed by IndexedInstance::law
    NumericSemantics numeric;           // 6.2
    std::optional<std::vector<FaultSpec>> faults;   // nullopt: "may fault, anyhow" (= mayFault)
    std::optional<ResourceSpec> resources;          // 8; nullopt: conservative
    std::optional<Definition> definition;           // 9
};

} // namespace camel::core::sem
```

每一部分都可以缺省，缺省的读法都是保守的那一侧：没有 `indexing` 就当它读整个输入、写整个输出、元素间关系未知；没有 `numeric` 就当所有许可都会改变它的结果、不知道它有没有舍入；没有 `faults` 就照 `mayFault` 位；没有 `resources` 就照 traits（`pure` 的不读写资源；其余当作对所有实参做读-改-写、结果可能与任何实参别名）。这与图模型 3.11"加算子性质：缺省值取保守的一侧"一致。

### 4.3 注册与查询

```cpp
class OperatorTable {
  public:
    // ---- Semantics (module load time) ---------------------------------------------------
    /// Declares the semantics of `uri`. Checked before it is published (4.4); a declaration
    /// that fails the checks is rejected with a diagnostic naming the module and the operator.
    /// Replacing a declaration bumps the table's declaration epoch (10.4).
    void declareSemantics(std::string_view uri, sem::OperatorSemantics semantics);

    // ---- Queries (any thread, lock-free after publication) ------------------------------
    /// The declared semantics, or nullptr when nobody declared any.
    const sem::OperatorSemantics *semantics(OperId id) const;
    /// The stable key of an operator that has semantics; nullopt otherwise.
    std::optional<sem::OperatorKey> key(OperId id) const;
    /// The process-local id of a stable key, for loading a cache: nullopt when the operator is
    /// unknown or its version cannot satisfy `key` (4.1).
    std::optional<OperId> bind(const sem::OperatorKey &key) const;
    /// Incremented whenever a declaration is added or replaced (module load).
    uint64_t declarationEpoch() const;
};

namespace sem {
/// What is known at one OPER node: the operand types as the node sees them (a TensorType
/// carries dtype and shape; an unknown extent is -1), the constant operands, and a source
/// of fresh symbolic parameters for what is not known.
struct InstanceContext {
    std::span<type::Type *const> norm, with;
    type::static_args_t statics;
    ParamAllocator &params;
};
using InstantiateFn = std::optional<IndexedInstance> (*)(const InstanceContext &);

/// The index semantics of one node, or nullopt when the operator declares none or the
/// context does not fix it (an unknown rank, say). Pure function of its arguments.
std::optional<IndexedInstance> instantiate(OperId id, const InstanceContext &cx);

/// The permissions granted to `node` by the program (6.4); 0 when none.
PermissionMask granted(const GraphView &graph, NodeId node);

/// May the reduction `law` of `node` combine its contributions in another order or
/// grouping, and may it permute them? (7.3)
ReorderFreedom reorderFreedom(const OperatorSemantics &s, const ReductionLaw &law,
                              PermissionMask granted);
} // namespace sem
```

- 声明在模块装载时登记，与今天的 `set`、`setResolver` 同时；登记之后不可变（"替换"只发生在装载期，与 type folder 的规则相同）。查询经由一个发布后的指针，读者不加锁，与 traits 的原子字节同一种做法。
- 实例化是一个**纯函数**：同样的实参类型与静态实参得到同样的实例。它的结果是可推导的事实（第 10 节的 (ii) 类），由事实库缓存，不在算子表里缓存。
- `OpDef` 增加一个 `semantics` 字段，张量模块的注册代码把它连同 traits 一起交给算子表；内建的 `:op/` 标量算子由核心在 `src/builtin` 里声明。

### 4.4 登记时的检查

`declareSemantics` 在发布之前检查一张声明自身的一致性，失败即拒绝（模块装载报错，指明模块与算子）：

1. `key` 的 `protocol:name` 与 `uri` 一致；同一个 URI 不能以更低的版本替换。
2. traits 与 semantics 不矛盾：`elementwise` 位为真的算子，`indexing` 若声明，必须在每个实例上是"输出与每个输入的映射都是恒等或广播"；`pure` 为假的算子不能声明 `resources` 为空。
3. `numeric.cls == Bounded` 时 `ulpBound > 0`；`Exact` 的算子 `sensitiveTo == 0`。
4. `laws` 的下标、每条律的累加类型、初值的位宽与 dtype 一致。
5. 有 `definition` 的：定义图的签名与算子的签名一致（9.2）；定义图里不经由任何路径调用到本算子（递归检测，9.4）。

实例化函数是代码，它产出的实例是否与内核一致，登记时查不出来；由第 12 节每一阶段的**声明的性质测试**负责。这是本稿最不放心的一处（第 16 节）。

---

## 5. 迭代域与下标映射

### 5.1 模型

```cpp
namespace camel::core::sem {

enum class DimRole : uint8_t { Parallel, Reduction, Sequential };

/// A quasi-affine integer expression over dims and parameters, interned (ExprId).
/// Forms: constant | dim d | param p | a + b | a - b | c * a | p * a | a floordiv k | a mod k,
/// where c is a constant, p a parameter, k a positive constant or a parameter that the
/// domain's constraints require to be positive. Arithmetic is on mathematical integers;
/// a consumer that lowers it to machine integers proves the range or checks it.
using ExprId = uint32_t;

struct Param {
    SymId name;
    enum class Source : uint8_t { Extent, Static, Runtime } source;
    uint8_t operand; uint8_t axis;      // Extent: shape(operand)[axis]; Runtime: scalar operand
    std::optional<int64_t> value;       // known when the type or the constant fixes it
};

struct Constraint { ExprId lhs; enum class Rel : uint8_t { Eq, Le, Lt } rel; ExprId rhs; };
using Predicate = std::vector<Constraint>;   // a conjunction; empty means "always"

struct Dim { SymId name; ExprId extent; DimRole role; };

struct Domain {
    std::vector<Dim> dims;               // canonical order: the lexicographic order of tuples
    std::vector<Param> params;
    Predicate constraints;               // facts about the params: n >= 0, step > 0, ...
};

enum class OnInvalid : uint8_t {
    Unreachable,   // the declaration claims the predicate always holds inside the domain
    Fault,         // reaching an invalid coordinate is a fault (FaultKind::IndexOutOfRange)
    Excluded,      // the contribution does not exist (a reduction skips it)
    Padding,       // the read yields `padding` (exact bits)
};

struct Access {
    enum class Kind : uint8_t { Affine, Indirect, Whole, None } kind;
    std::vector<ExprId> coords;          // Affine: one expression per axis of the operand
    Predicate valid;                     // where the coordinates are meaningful
    OnInvalid onInvalid = OnInvalid::Unreachable;
    uint64_t padding = 0;
    Indirect indirect;                   // Indirect only, 5.6
};

enum class WriteMode : uint8_t { Unique, Combine, OrderedOverwrite };

struct Write {
    std::vector<ExprId> coords;          // output coordinate per output axis
    Predicate valid;
    WriteMode mode = WriteMode::Unique;
    uint8_t law = 0xFF;                  // Combine: index into OperatorSemantics::laws
};

struct IndexedInstance {
    Domain domain;
    std::vector<Access> norm, with;      // one per operand
    Write output;
    uint8_t law = 0xFF;                  // the law that combines over the Reduction dims
};

} // namespace camel::core::sem
```

含义：对域里满足约束的每个下标元组 `t`，按每个操作数的 `coords(t)` 读出（`valid(t)` 不成立时按 `onInvalid` 处理），算出一个贡献，写到输出的 `output.coords(t)`；多个 `t` 写到同一个输出坐标时按 `output.mode`：`Unique` 声称不会发生，`Combine` 按律合并（归约维就是这种情形的常见来源），`OrderedOverwrite` 按域的规范顺序最后一个为准。`Sequential` 维上的贡献按顺序进入一个递推（11.3），既不并行也不重排。

**einsum 只是简写。** `"ij,jk->ik"` 展开为三维的域（`i`、`k` 并行，`j` 归约）、两个仿射访问与一个合并写；它表达不了步长、偏移、整除取模与有效条件，所以不作为模型本身。核心提供 `sem::einsum(spec, extents)` 生成实例，供简单算子的声明少写几行。

**参数从哪来。** 三种：实参类型里的维度（`Extent`；张量类型里写 `-1` 或命名维度 `constas n` 的位置得到一个没有值的参数，命名维度之间的线性关系成为 `constraints`）；静态实参（`Static`，如 `axis`、`start`）；运行期的标量实参（`Runtime`）。**动态参数不必是编译期常量**：实例里就是一个符号，分析把它当作不定的整数；Tiles 在特化时知道值，就把它代入。

### 5.2 五个例子

下面的 Camel 是示意；C++ 是该算子的实例化函数在某个具体上下文里产出的实例，用数学记法写。

**例 1：广播。**

```camel
import { Tensor } from tensor
func add_bias(x: Tensor<f32, const [constas m, constas n]>, b: Tensor<f32, const [n]>): Tensor<f32, const [m, n]> {
    return x + b
}
```

`tensor:add@1.0` 在 `x: [m, n]`、`b: [n]` 上：

```
domain   (i, j) : 0 <= i < m, 0 <= j < n          roles (P, P)       params m, n (Extent)
x        (i, j)
b        (j)                                     // 维数少的操作数：缺的前导维丢掉
output   (i, j)  Unique
```

若 `b: [1, n]`，`b` 的访问是 `(0, j)`：维度为 1 的那一维映射到常数 0。若两边的维数都是未知（`-1`）而等不等于 1 不知道，实例化**给不出仿射映射**：这一维要么是恒等、要么是常数 0，取决于运行期的值。此时返回两个候选的守卫形式还是返回空（保守），见 15 的 Q9；本稿第一阶段返回空，Tiles 特化时形状已知，不受影响。

**例 2：切片。** `slice(x, axis = 0, start, stop, step)`，`x: [n, c]`，`step > 0`：

```
params   n, c (Extent); start, stop, step (Static 或 Runtime)
domain   (i, j) : 0 <= i < L, 0 <= j < c       其中 L = max(0, (stop - start + step - 1) floordiv step)
         constraints: step > 0, 0 <= start <= n, start <= stop <= n     （2.11：起点在终点之后得空）
x        (start + step * i, j)
         valid: start + step * i < n           onInvalid: Unreachable（由 constraints 推出）
output   (i, j)  Unique
```

`L` 里有 `max`，不在准仿射语言里：实例把 `L` 本身登记为一个参数，并加约束 `step * (L - 1) < stop - start <= step * L`（或 `L = 0` 当 `stop <= start`）。`start`、`stop` 是运行期值时，越界在算子入口处是故障（2.10"运行期的 `slice` 越界是故障"），声明成 `FaultSpec{IndexOutOfRange}` 加上述约束作为前置条件；进入域之后的访问是 `Unreachable`。Tiles 今天对静态 slice 正是这样做的：入口处检查、空域把步长规范为 0【读码：`compiler.cpp:446-468`】。

**例 3：reshape。** `reshape(x, [p, q])`，`x: [a, b]`，`a·b = p·q`：

```
domain   (u, v) : 0 <= u < p, 0 <= v < q          roles (P, P)
let l  = q * u + v                               // 输出的行主序线性下标
x        (l floordiv b, l mod b)
output   (u, v)  Unique
constraints: a * b = p * q                       // 两边都是参数时这是一个非线性等式：作为前置条件，不参与分析
```

reshape 的访问是逻辑上的"线性化再反线性化"，与存储无关；Tiles 今天把它当"线性下标不变"处理，是因为它的存储一律是连续行主序——那是**存储映射**与逻辑映射复合之后恰好消掉了，属于 Tiles（5.7）。

**例 4：窗口。** 一维卷积 `conv1d(x, w)`，`x: [L]`，`w: [R]`，步长 `s`、膨胀 `d`、填充 `p`（都是静态实参）：

```
domain   (o, r) : 0 <= o < O, 0 <= r < R          roles (P, R)       O = (L + 2p - d(R-1) - 1) floordiv s + 1
x        (o * s + r * d - p)
         valid: 0 <= o * s + r * d - p < L        onInvalid: Padding(+0.0)
w        (r)
output   (o)  Combine(law 0)                     law 0 = 乘后加，累加 f32，k 递增（第 7 节）
```

同样的窗口用于最大池化时，越界的贡献是 `Excluded`，不是填 0：0 不是 `max` 的单位元，负数的窗口会被它改掉（IC 8.2 说的就是这件事）。

**例 5：分块的尾部 `i < N`。** 分块是调度，不是算子的语义；但协议要让 Tiles 把"分块之后的尾部"表达成逻辑的有效条件，而不是自己发明。核心提供**代入**：

```cpp
/// Rewrites `inst` under dims := f(new dims): every access, write and predicate is substituted,
/// and the original domain's bounds become a predicate on the new dims.
IndexedInstance substitute(const IndexedInstance &inst, std::span<const DimSplit> splits);
```

对 `matmul`（5.3）做 `m = 16·tm + lm`、`k = 16·tk + lk`：新的域是 `(tm, lm, tn, ln, tk, lk)`，`lm < 16` 等是新维的界，原来的 `m < M` 变成谓词 `16·tm + lm < M`，`k < K` 变成 `16·tk + lk < K`。前者是**写**的有效条件（尾部的线程不写），后者是**贡献**的有效条件：原声明里 `k` 维的越界贡献按律是 `Excluded`，所以尾部的 `k` 不参与累加。这正是 Tiles 今天的 `if(base+t<k)`【读码：`codegen.cpp:163-164`】。用 0 填充而不排除会改结果：`acc + (+0.0)` 把 −0 变成 +0，`∞ × 0` 是 NaN；排除才与规范算法逐位相同。

### 5.3 matmul 与归约的实例

```
tensor:matmul@1.0     A: [M, K], B: [K, N]
domain   (m, n, k)    roles (P, P, R)
A        (m, k)       B (k, n)
output   (m, n)  Combine(law 0)
law 0    见 7.2（规范算法是 Q7）

tensor:sum_axis@1.0   x: [a, w, c], axis = 1, keepdims = false
domain   (i, r, j)    roles (P, R, P)
x        (i, r, j)
output   (i, j)  Combine(law "sum_f32_via_f64")
```

### 5.4 动态参数与保守情形

实例化与分析遇到下列情形，结果是"未知"，消费者按保守处理：

| 情形 | 结果 | 消费者应当 |
|---|---|---|
| 算子没有声明 `indexing` | 无实例 | 当作读整个输入、写整个输出 |
| 实参的秩未知 | 无实例 | 同上；特化之后再问 |
| 某一维是否为 1 未知（广播） | 无实例（第一阶段；Q9） | 同上 |
| 映射里有非线性项（参数乘参数、按非常数取模），或约束不可判定 | 实例存在；依赖、单射、越界的判定可能是"未知" | 保留越界检查；不并行写；不重排 |
| 分析超时 | "未知" | 同上；绝不把"没算完"当作"独立" |
| 下标算术是否放得进 64 位 | 实例里是数学整数 | 降到机器整数之前证明范围或加检查（Tiles 今天就检查溢出【读码：`compiler.cpp:452-456`】） |
| 数据相关的地址 | `Access::Indirect`（5.6） | 不做依赖分析；按声明的边界处理 |

### 5.5 访问映射与 traits 的关系

`elementwise` 位是"每个访问都是恒等或广播"的一个缓存。有了实例之后，Tiles 不再需要自己的名字列表（2.2 的第 3 行）：逐元素的判定、融合时坐标的传递（Tiles 今天 `emit` 里的递归代入【读码：`codegen.cpp:181-201`】），都是在实例上做代入。

### 5.6 gather 与 scatter

地址取决于数据时，映射不可分析，但声明仍然要说清楚边界：

```cpp
struct Indirect {
    uint8_t indexOperand;               // the operand holding the indices
    std::vector<ExprId> indexCoords;    // where in indexOperand the index for this point is read
    uint8_t axis;                       // which axis of the data operand it addresses
    // the other axes of the data operand are given by Access::coords (affine)
};
```

`embedding(table: [V, D], ids: [B])`（`gather`）：

```
domain   (b, d) : b < B, d < D                    roles (P, P)
ids      (b)                                      Affine
table    (ids[b], d)                              Indirect{indexOperand = ids, indexCoords = (b), axis = 0}
         valid: 0 <= ids[b] < V                   onInvalid: Fault
output   (b, d)  Unique
```

`scatter_add(base: [V, D], ids: [B], src: [B, D])`：

```
domain   (b, d)                                   roles (P, P)
src      (b, d)                                   ids (b)
output   (ids[b], d)  Combine(law add)            // 写坐标是间接的：用 Write 的同一种 Indirect
         valid: 0 <= ids[b] < V                   onInvalid: Fault（先于任何写入，4.2 的 FaultSpec）
initial  base                                     // 输出从 base 开始；Combine 的初值按律
```

由此 Tiles 得到需要的五样：输出的域（`[V, D]`，与 `ids` 的内容无关）、读的是哪个输入的哪些维、**写会冲突**（`Combine`，而不是 `Unique`）、**需要原子的或有序的合并**（合并的律与它允许的顺序由第 7 节决定：浮点的 `add` 没有许可 (a) 时，冲突的写必须按 `b` 的顺序合并，这排除了原子加）、**越界的处理**（故障，且在写之前）。

### 5.7 逻辑映射与物理布局

核心只有逻辑访问映射。Tiles 的 `reindex` 节点（偏移 + 步长）是"逻辑映射 ∘ 行主序存储映射"的结果；以后的布局（交错、分块存储、Tensor Core 的片段布局）是 Tiles 的存储映射与执行布局（IC 3.1 的后两种），用 5.2 的代入与 Tiles 自己的映射复合即得，不回写到核心。物理映射与内核语言见 tiles-route。

---

## 6. 数值契约

### 6.1 裁定（2.13，照录要点）

- **基准是规范算法**，不是某台机器的实现：加、减、乘、除、开方每一步按 IEEE 正确舍入，严格按程序书写的顺序，不收缩为乘加融合。在这个范围内，各后端、各设备的结果逐位相同。
- **超越函数**（`exp`、`log`、`sin` 等）不要求跨设备逐位相同，只承诺一个写明的误差上界（以 ulp 计）。
- **放宽必须显式授予，五项许可互不蕴含**：(a) 改变归约的顺序或结合方式；(b) 收缩为乘加融合；(c) 使用近似数学函数；(d) 降低输入或累加的精度（如 TF32、bf16）；(e) 对 NaN、无穷、负零、次正规数作额外假设。**模块只能声明算子本身做什么，不能替调用者扩大许可。**
- **`unordered_reduce`** 只授予"这次归约的合并顺序可以改变"。
- **许可写在哪段代码上就只对那段代码生效**：被调用的函数不继承；内联、展开定义图时许可随节点保留，并随缓存保存。授予的写法倾向于函数或块上的标注，如 `@numeric(reassociate, contract)`。

### 6.2 算子的原始数值语义

每个算子在 `NumericSemantics` 里声明自己是哪一类、怎么算：

| 类 | 例子 | 声明的内容 | 对许可敏感 |
|---|---|---|---|
| `Exact` | `reshape`、`transpose`、`slice`、`broadcast`、整数加减（回绕）、`neg`、`abs`、比较 | 无舍入 | 无（`sensitiveTo = 0`） |
| `CorrectlyRounded` | `add`、`multiply`、`divide`、`sqrt`、`rsqrt`、`sum_axis`、`mean_axis`、`matmul` | 规范算法：每一步的运算、舍入类型、顺序；输入转换与输出转换 | 视算法：含归约的对 (a) 敏感；含乘后加的对 (b)；有累加类型的对 (d)；全部对 (e) |
| `Bounded` | `exp`、`log`、`tanh`、`sigmoid`、`gelu`、`erf` | `ulpBound`；特殊值的规则（Q4） | (c)、(e) |
| `Opaque` | 没有声明的 | — | 全部 |

几个要写清楚的规范算法（第一批声明，第 12 节第 2 阶段）：

- `rsqrt(x)` = `1 / sqrt(x)`，两步各自正确舍入。CPU 与 Tiles 今天恰好都这样做【读码：`unary_loops.inl` 的 `Rsqrt`；`codegen.cpp:224`】，所以它是 `CorrectlyRounded`，不是近似函数。
- `sum_axis`（f32 输入）= 从 +0.0（f64）开始，按归约维的下标递增，每步把元素提升为 f64 后相加并在 f64 里舍入，最后一次舍入为 f32。`mean_axis` 在最后一步之前用 f64 除以个数。CPU 与 Tiles 一致【读码：`reduce.cpp:145-159`；`codegen.cpp:262-285`】。
- `sum_to`（梯度用）= f32 累加、行主序；与 `sum_axis` 是**两个不同的规范算法**，Tiles 已经注意到并分别实现【读码：`codegen.cpp:234-235` 的注释】。
- 张量的 `divide` 按 IEEE 给出 ±∞ 与 NaN，不故障；标量 f64 的 `/` 除以 ±0 是故障（2.3）。

`sensitiveTo` 让消费者知道某项许可对这个算子**没有作用**，从而不必找它：整数的 `sum` 在回绕加法下确切地可结合可交换，重排不需要许可 (a)；IEEE `maximum`（Q5 选 A 时）同理。

### 6.3 许可放在图的哪里

**每个节点一个字节的许可列**，与位置列平行（图模型 3.8、4.1）；全图为 0 时不存（图头一个标志位），所以没有用到许可的程序不付任何代价。

- **为什么是每节点，不是每图。** 许可可以写在块上（Q2），一张图里可以有的节点有许可、有的没有；内联之后，调用者与被调者的节点混在一张图里，各带各的许可。每节点存储是唯一不丢信息的做法。
- **为什么是位集，不是驻留的策略编号。** 五项许可就是五个位；(e) 若按 Q1 拆成几个子项也放得下一个字节。将来要带参数的许可（例如"可以降到 bf16，但不低于"）再升为编号。
- **哪些节点带许可。** 只有自己做算术的节点有意义：`OPER`、`LOOP`（作为"这个循环的状态递推"本身没有算术，循环体是另一张图，带它自己的列）。`FUNC`、`CALL` 上的许可**不传给被调图**（"被调用的函数不自动继承"）。
- **前端怎样写入。** 建图时，处在一个 `@numeric(...)` 标注范围里的每个节点把列置为该集合；标注嵌套时取并（内层再授予）。词法上属于同一函数体的分支臂图、循环的条件图与循环体图、函数体里定义的 lambda 是否算"这段代码"，是 Q2。
- **前端支持标注之前**，列全为 0：一切按规范算法。

### 6.4 许可怎样随改写传播

| 改写 | 规则 |
|---|---|
| 内联 | 被调图的节点带着**被调图自己的**许可拷入；调用节点的许可不加到它们身上 |
| 定义图展开（第 9 节） | 展开出的节点：属于"这个算子本身"的那些带**被展开节点的**许可（同一个操作，只是换了表示）；定义图里对别的函数的 `FUNC`/`CALL` 照内联的规则，不传 |
| 公共子表达式合并 | 两个节点许可不同时，保留的节点取两者的**交**（合并后的值必须同时满足两处的要求）；不愿取交就不合并 |
| 常量折叠 | 按规范算法折叠（不用任何许可）——永远合法 |
| 融合（Tiles） | 融合组里每个节点保留自己的许可。把 `a * b` 与 `+ c` 收缩成一条 FMA，要求**两个节点都有** (b)；把一串 `+` 重新加括号，要求**链上每个节点都有** (a) |
| 复制、特化、求导生成的新节点 | 取触发它的节点的许可（与位置列"新造的节点取触发它的节点的位置"同一规则，图模型 3.8） |
| 缓存 | 许可列是图块的一部分，随 `.cmo` 保存（10.6 的 (i) 类）；Tiles 的编译缓存把用到的许可放进键 |

### 6.5 授予的写法（示意，细节是 Q1–Q3）

```camel
import { Tensor, sum, matmul } from tensor

@numeric(reassociate)                                    // 示意
func loss(x: Tensor<f32, const [constas n, 3]>, w: Tensor<f32, const [3, constas k]>): float {
    return sum(matmul(x, w))      // sum 可以重排；matmul 的内积也是归约，同样可以重排
}

func caller(x: Tensor<f32, const [8, 3]>, w: Tensor<f32, const [3, 4]>): float {
    return loss(x, w) + sum(x)    // 这里的 sum(x) 没有许可：按规范算法
}
```

`loss` 里的 `matmul` 节点有 (a)：Tiles 可以做 Split-K 或树形累加；没有 (b)，不能用 FMA（除非 Q7 定为 FMA 链是 `matmul` 的规范算法）；没有 (d)，不能用 TF32 的 Tensor Core。

### 6.6 `unordered_reduce`

`para` 模块的 `unordered_reduce<f, initial>(xs)` 今天的实现：把数组切成块，每块从块的第一个元素起按顺序折叠，块的部分结果再从 `initial` 起按块序折叠【读码：`src/passes/sched/taskflow/taskflow.cpp:2733-2830`】。签名的文档说"合并顺序不固定，所以 f 应当可结合可交换"【读码：`stdlib/signatures/para.cml:20-27`】。

在本协议里：`:mark/unordered_reduce_arr` 的声明是一条 `ReductionLaw`，`combine` 是传入的函数值（`Custom`），`order = Unordered`。它对这次归约的作用**等同于节点上的许可 (a)**，但它来自算子本身（程序选择了这个算子），不来自许可列，也不是"模块替调用者扩大许可"——调用它就是程序写下的授予。它**不**授予 `f` 内部的任何东西：`f` 是被调函数，其中的加乘仍按规范算法，`f` 自己的标注才算数。初值 `initial` 只参与一次，所以律不要求它是单位元。

### 6.7 `max`、`min` 的特殊值

今天（2.3）：逐元素的 `maximum(a, b)` 有 NaN 或 ±0 平局时返回 `a`；`max_axis` 只在第一个元素是 NaN 时得 NaN，平局取先出现的【实测】。这个规则**可结合但不可交换**：结果取决于元素的位置，所以任何并行的最大值都要像 Tiles 那样带着原下标比较【读码：`codegen.cpp:118-140`】。

三个候选（Q5）：

- **A. IEEE 754-2019 的 `maximum`/`minimum`**：任一操作数是 NaN 则结果是 NaN；−0 < +0。确切可结合、可交换，所以最大值归约在任何顺序、任何分块下**逐位相同，不需要许可**；Tiles 的并行最大值不再需要带下标（`argmax` 仍需要，见下）。CUDA 的 `fmaxf` 不是它（`fmaxf` 忽略 NaN），要用比较序列实现【未查：CUDA 是否有直接对应的内建函数】。
- **B. `maximumNumber`**：忽略 NaN（全是 NaN 时才是 NaN）；−0 < +0。同样确切可结合可交换；但"数据里有 NaN 而结果看不出来"与 2.13 不对特殊值作额外假设的精神不合。
- **C. 保持现状**：位置相关。不改语义，但每个并行实现都要带下标，并且 `softmax` 内核里的 `std::max` 从 −∞ 起累计（忽略 NaN）与 `max_axis`（传播首个 NaN）这两个现存的不一致都要按 C 的规则修。

建议 A。平局时的**下标**（`argmax`、`max` 的梯度）仍取第一个出现的位置：`argmax` 的律是"(值, 下标) 对上的字典序最大值，值相等取下标小的"，它也确切可结合可交换（因为下标唯一），并行时照样带下标，这不受 Q5 影响。

### 6.8 超越函数的上界

每个 `Bounded` 算子在声明里写一个 `ulpBound`，并写明特殊值的规则（NaN 传播、±∞、±0 的结果）。特殊值不属于"近似"的自由：近似（许可 (c)）只放宽精度；对 NaN、无穷、负零、次正规数的额外假设是许可 (e)，两者互不蕴含。上界的具体数值与它覆盖的输入范围是 Q4。

### 6.9 审计项

**Tiles**（交给 Tiles，第 14 节同列）：

1. **matmul 的 `fmaf`。** 在 2.13 的字面下，没有许可 (b) 时 `matmul` 的规范算法是"先乘、舍入、再加、舍入"，`fmaf` 是一次没有被授予的收缩。若 Q7 定为字面读法，Tiles 改用 `__fmul_rn` 与 `__fadd_rn`（CUDA 规定这两个内建函数不被收缩【未查：以 CUDA 文档为准】），有许可 (b) 的节点再用 `fmaf`。若 Q7 定为"按 k 顺序的 FMA 链"是 `matmul` 的规范算法，Tiles 现有代码即符合，要审计的是 CPU。
2. **容差测试不是逐位相同的证明。** README 说"浮点的一致性以容差测试"；基准测试里"所有输出逐位相同"是样本，不是证明。2.13 之后，`CorrectlyRounded` 类的算子在 Tiles 与 CPU 之间应当**逐位比较**；容差只适用于 `Bounded` 类，且容差应当由声明的 `ulpBound` 推出，而不是一个经验数。
3. **`relu` 的 NaN 与 −0**：与 CPU 不同（2.3）。按 Q6 的裁定改一边。
4. **NVRTC 的精度选项要写死。** 今天只传了 `--fmad=false`【读码：`cuda_backend.cpp:256`】。除法与开方的正确舍入、次正规数不冲刷依赖 NVRTC 的缺省（`-prec-div`、`-prec-sqrt`、`-ftz`）【未查：缺省值以 NVRTC 文档为准】；契约要求的东西不应依赖缺省值，显式传入，并进编译缓存的键。
5. **`expf`、`logf` 的误差上界**要不超过声明的 `ulpBound`【未查：CUDA 文档给出的这两个函数的上界】；`--use_fast_math` 一类选项只能在节点有许可 (c)（与 (e)，若它还改变特殊值）时使用。
6. **并行最大值的合法性**今天来自"带下标的比较保持了位置规则"；Q5 若选 A，这条实现仍然正确（它比需要的更严），可以简化。
7. 编译缓存要持久化时，键要加上第 10.6 节 (iii) 列出的各项。

**核心**（不在 Tiles，列在这里因为同一份契约）：

8. CPU GEMM 打开了收缩，并且没有 FMA 的 x86-64 处理器上舍入两次（2.3）：与契约不符，无论 Q7 怎么定都要改其中一种情形。
9. `vmath::exp` 只说"few ulp"，NaN 不忠实：要给出上界、修 NaN（或在声明里如实写出，等 Q4）。
10. `softmax` 融合内核与它的定义不是同一个算法（2.3、第 9 节）。
11. `max`/`min` 的规则按 Q5 统一；`softmax` 内核与 `max_axis` 今天对 NaN 的处理互相不一致。

---

## 7. 归约律

### 7.1 不能只说"加法可结合"

一条律的可重排性取决于 dtype、累加类型与语义细节，同一个名字的"加"在不同 dtype 上是不同的律：

| 律 | dtype / 累加 | 确切可结合 | 确切可交换 | 单位元（位） | 没有许可时能否重排 |
|---|---|---|---|---|---|
| 整数加（回绕） | i64 / i64 | 是 | 是 | 0 | **能**，逐位相同 |
| 浮点加 | f32 / f32 | 否 | 是 | −0.0（`−0 + x = x` 对一切 x 成立；+0 不是：`+0 + −0 = +0`） | 不能 |
| 浮点加 | f32 输入 / f64 累加 | 否 | 是 | −0.0（f64） | 不能 |
| 浮点乘后加（matmul 的 k 维） | f32 | 否 | — | 见 7.2 | 不能 |
| IEEE `maximum`（Q5 A） | f32 | 是 | 是 | −∞ | **能** |
| 位置相关的 `max`（现状） | f32 | 是 | 否 | 无（首元素） | 只能保序分组 |
| `argmax` 的字典序 | (f32, i64) | 是 | 是 | (−∞, +∞) | **能** |
| 用户函数（`unordered_reduce` 的 f） | 任意 | 未知 | 未知 | 不要求 | 只因算子是 `Unordered` 才能 |

内建的律是核心审定的声明；**用户函数的律不推断**——不从名字、不从随机测试（IC 3.4 的"随机测试是证据，不是证明"）。

### 7.2 记录

```cpp
namespace camel::core::sem {

enum class LawKind : uint8_t {
    Add, Mul, MulAdd /* acc = acc + a*b */, FmaChain /* acc = fma(a, b, acc) */,
    Max, Min, ArgMax, ArgMin, And, Or, Custom };

struct ReductionLaw {
    LawKind kind;
    type::TypeCode input, accumulator, result;   // f32, f64, f32 for sum_axis on f32
    enum class Init : uint8_t { Constant, FirstContribution } init;
    uint64_t initBits = 0;                       // Constant: the exact starting value
    enum class Empty : uint8_t { Initial, Fault } empty;   // what an empty domain gives
    enum class Order : uint8_t { Ascending, Unordered } order;  // Ascending: canonical domain order
    enum class Finalize : uint8_t { None, DivideByCount } finalize;
    bool exactAssociative, exactCommutative;     // for these types, under the declared specials
    uint64_t identityBits = 0; bool hasIdentity; // the exact identity, if there is one
    std::variant<std::monostate, OperId, GraphRef> custom;  // Custom: the combining operator
};

} // namespace camel::core::sem
```

规范算法由 `init`、`order`、`kind` 与类型完全确定：例如 `sum_axis` 的律是 `{Add, f32, f64, f32, Constant(+0.0), Empty=Initial, Ascending, None}`（今天的 CPU 与 Tiles 都从 +0 开始【读码：`reduce.cpp:149`；`codegen.cpp:271`】，所以 `sum([−0.0]) = +0.0`；这是声明里写明的事实，不是 bug——但并行实现给每个分块补的初值必须与它一致，否则会出现不同的零号）。`matmul` 的律是 `{MulAdd 或 FmaChain（Q7）, f32, f32, f32, Constant(+0.0), Initial, Ascending, None}`。

### 7.3 什么时候可以重排、换精度

```
granted = 节点的许可列 ∪ (law.order == Unordered ? {a} : ∅)

可以重新加括号  ⇔  law.exactAssociative  ∨  a ∈ granted
可以置换次序    ⇔  law.exactCommutative ∧ (law.exactAssociative ∨ a ∈ granted)
可以换初值的放法（分块各自从单位元起） ⇔  可以重新加括号 ∧ law.hasIdentity ∧ initBits == identityBits
                                     （否则只有第一块带初值）
可以收缩为 FMA  ⇔  b ∈ granted           （MulAdd；FmaChain 本来就是）
可以降低累加或输入精度 ⇔  d ∈ granted
```

"提高累加精度"（例如把 `sum_to` 的 f32 累加换成 f64）同样改变结果的位，五项许可里没有它（(d) 说的是降低）。本稿按字面：**不允许**，除非负责人把 (d) 定为"改变精度"（Q8）。

---

## 8. 资源

> **实施状态（2026-10-16，`lane/opsem-p4`）**：8.1 的声明是 `sem::ResourceSpec`（`camel/core/resource_semantics.h`），作为 `OperatorSemantics::resources`，由 `OperatorTable::declareResources` 登记（没有记录的算子得到 `uri@1.0`，已有记录的次版本加一）；登记检查的第 5 条拒绝自身不一致的声明与和 traits 矛盾的声明。与下文的差别（都是窄读）：
>
> - **资源的类**：`Variable`（`var` 单元）、`Object`（就地改的对象）、`Foreign`（模块包装的对象，带名字，如 `tiles:device`）之外，加了 **`Ambient`**：没有值指名的状态（文件系统 `fs`、控制台 `io:stdout`/`io:stdin`、模块的随机数发生器 `rand:generator`、时钟、进程环境），以名字为身份。下文的 `Binding` 只有三类有身份的值；文件与随机数状态没有值指名它，不加这一类就无法声明（任务要求声明它们）。`Unknown` 只是未声明时的保守读法，不能声明。
> - **区域**：`Mapped` 不另存表达式，而是指明节点下标实例里的哪一张映射（某个操作数的访问映射或写映射），区域就是该映射在域上取到的元素；`regionBounds` 给出包围盒，`regionsMayOverlap` 判断重叠（算不出就按整体）。今天没有声明 `Mapped` 区域的算子（张量没有就地算子），由测试用手造的实例覆盖。
> - **结果**：`ResultAlias` 照 8.1；另有 `allocates`：结果是新的资源（`:var/new` 新建单元，`tiles:upload` 新建设备内存）。
> - **外部效应与 traits**：声明的 `externalEffect` 只能在 traits 允许外部效应时为真（不是 pure、不是 stateOnly）；反过来可以细化：赋值族在 traits 里算外部效应（没有"写变量"这一位），资源声明说它只写程序自己的单元。效应摘要（`effectSummaryOf`）仍由 traits 算出，不变（8.3）。随机数的生成照 8.1 的注释算外部效应，见 14.4 的待问。
> - **查询**（`camel/runtime/resources.h`）：`resourcesOf`（每个节点：OPER 读声明；FUNC、LOOP、被调函数可知的 CALL 读所运行的图的足迹并映射到节点的输入）、`footprintOf`（一张图对它收到的资源、环境状态与程序外做了什么；递归按最小不动点，按图的序号与声明纪元缓存）、`originOf`/`mayAlias`（同一次激活里两个不同的新建、或新建与收到的值，不是同一资源；两个端口可能是）、`resourceConflict`（`Resource`：重叠区域且至少一写；`External`：都有外部效应）、`dependsOn`（只看数据边）与 `orderedBefore`（含控制边）分开（8.2）、`requiredOrderings`（一张图里必须保持先后的成对节点，分支的不同臂不算一对；图没有排序的一对即设计原则第 6 节未决的情形）。
> - **已声明**：`:var/*` 与赋值族、控制台与 profiler、`fs`、`random`、`time`、`os`、`sys`、`nn:save_params`、`para:rendezvous`、`algo` 的就地形式、张量的有效应算子、Tiles 的算子（14.4）。`:mark/` 高阶算子、`python`、`pyplot`、`onnx`、`autodiff` 未声明，照保守读法，与今天一样排序。
>
> 细节见 `docs/technical/41_resources_and_definitions.md` 第 1 节。

### 8.1 声明

```cpp
namespace camel::core::sem {

struct Binding {                         // which value names the resource
    enum class Kind : uint8_t { Norm, With, Capture } kind;
    uint8_t index;
};
struct Region {                          // which part of it
    enum class Kind : uint8_t { Whole, Mapped, Unknown } kind;  // Unknown is read as Whole
    std::vector<ExprId> coords; Predicate valid;               // Mapped: via the instance's domain
};
enum class AccessMode : uint8_t { Read, Write, ReadModifyWrite };
struct ResourceAccess { Binding binding; Region region; AccessMode mode; };

struct ResultAlias {
    enum class Kind : uint8_t { Fresh, Same, View, MayAlias, Unknown } kind;
    uint8_t operand;                     // Same/View/MayAlias
    // View: the result is a window of `operand` given by the instance's output access
};

struct ResourceSpec {
    std::vector<ResourceAccess> accesses;   // empty and pure: touches no resource
    ResultAlias result;
    bool externalEffect;                    // visible outside the program (I/O, clocks, RNG)
};

} // namespace camel::core::sem
```

- **身份。** 2.12 列明了有身份的东西：`var` 单元、经 `var` 参数就地修改的对象、外部库包装的对象；其余的值没有可观察的身份，后端可以随意复制或共享。所以 `Binding` 只会指向这三类：`var` 形参、写了的捕获单元、外部资源对象（如 Tiles 的 `Device<Tensor>` 底下的设备内存）。张量值本身不是资源。
- **区域。** 不知道时是整个对象。张量的就地算子（2.10 保留的显式就地算子）的区域可以是一个映射出来的窗口。
- **访问方式**：读、写、读-改-写。
- **结果与输入的别名**：新分配、就是某个输入（今天的就地形式：`setInPlaceForm` 声明的算子"改它的第一个实参并返回它"【读码：`operator_table.h:190-206`】，即 `accesses = [{Norm 0, Whole, ReadModifyWrite}]`、`result = Same(0)`）、某个输入的视图、可能别名、未知。

没有声明的算子：`pure` 的不碰资源、结果新分配；其余按"对每个实参整体读-改-写、结果可能与任何实参别名、有外部效应"。

### 8.2 三种关系要分开

| 关系 | 是什么 | 谁表达 |
|---|---|---|
| 计算依赖 | 一个值用到另一个值 | 图上的数据边（`Norm`、`With`） |
| 资源冲突 | 两次访问同一资源的重叠区域，至少一次是写 | 前端按 2.5、2.6 插入的控制边（`sync` 块、`wait`、写入追踪）；图上的效应摘要（图模型 3.3）；本节的声明让分析能判断"两次访问是否真的重叠" |
| 异步完成 | 一次操作的结果什么时候可见 | **不在核心。**核心的执行模型里，一个节点执行完它的结果就可用（规则 E1）；设备上的异步是 Tiles 的实现细节，用 Tiles 自己的事件与流表达，在交出 Camel 值之前完成（Tiles 今天就是同步完成后才发布【读码：README "Completion is synchronous"】） |

`sync` 与 `wait` 是源码到图的规则（第 4 节："源码层的限制只在源码翻译到图这一步生效"），它们产生控制边，**不是 GPU 的同步原语**，本稿不给它们任何新含义。

### 8.3 与效应摘要的关系

图的效应摘要（四个位：出口依赖外部效应、写了捕获的变量、读了捕获的变量、可能故障）是由节点算出来的图级概括（图模型 3.10 的 I14）。资源声明让这个概括可以细化：一个只读捕获单元的算子不必按"写"处理；一次 `Mapped` 区域的写只与重叠的区域冲突。细化的结果是可推导的事实（第 10 节），不改变效应摘要本身的定义。

---

## 9. 定义图

> **实施状态（2026-10-16，`lane/opsem-p4`）**：`sem::Definition{module, function, params}`（`camel/core/operator_semantics.h`）作为 `OperatorSemantics::definition`，由 `OperatorTable::declareDefinition` 登记（次版本加一：多了一条事实）；`params` 给出算子的每个操作数绑定定义函数的哪个参数，可省略的操作数带缺省值（`softmax` 的轴为 `-1`）。构建与展开在 `camel/runtime/definitions.h`：
>
> - **构建**（`Context::definitions().definitionOf(oper)`，第一次被问到时）：定义库是普通的 Camel 模块（张量的是 `stdlib/tensor_definitions.cml`），程序已导入就用程序编好的，否则由一个**自己的图构建器**单独编译（`UserDefinedModule::compileAsLibrary`、`Builder::buildLibrary`：从库而不是入口模块构建，不调用 `main`）；它只能导入 C++ 写的模块。导出函数的图单独编码，连同它调用的图**钉在上下文的图管理器里**（`GCGraphManager::pin`），随上下文存活、被回收器追踪，不属于程序；展开时提交把它拷进程序。这是 9.2"生命周期"一行在第 5 步之前的形态：不是"模块装载时建出"，而是第一次被问到时建出，因为在装载时编译会让每个导入 `tensor` 的程序多编一个库（改变编译的代价与诊断）。
> - **检查**（拒绝时给出理由）：签名（恰好是声明的参数，没有 with 参数与捕获，不是泛型）；效应（纯算子的定义不得有外部效应或写变量；声明不会故障的算子，定义不得可能故障）；递归（沿"定义图里用到的算子 → 它们的定义"做环检测，环上的每个定义都被拒绝；声明纪元变了就重查，后来声明的定义可以把先前的接成环）。
> - **版本**：定义随算子的键（次版本）；建好的定义记下图的可移植内容散列（`portableGraphHash`），缓存展开结果或"已对照定义检查过的内核"时以它为键。
> - **展开**（`expandDefinition(session, draft, node, {inlineBody})`）：把一个 OPER 节点换成对定义图的直接调用（值、控制边、源码位置照旧），省略的操作数换成常量；默认再内联。定义自己的运算节点加上被展开节点的许可（6.4）。操作数的类型须是定义接受的（`Tensor<f32>`；元素类型未知的 `Tensor` 不展开）。**没有任何遍或后端默认调用它**（9.3）；测试遍 `test::expand_definitions` 用它对照内核。
> - **第一批定义**：`softmax`（带稳定化与轴；f64 累加的 `sum`；没有元素的张量就是自己的结果）、`log_softmax`、`mean`（全量：f64 的和除以个数）、`relu`（`maximum(x, +0)`）、组合激活 `sigmoid`（`1 / (1 + exp(-x))`）。
> - **与内核的对照**：`softmax`、`log_softmax`、`mean`、`relu`、`sigmoid` 的内核与展开后的定义在 nvm/fvm/jit/prl 上逐位一致（含 NaN、无穷、±0、次正规数、空张量；NaN 只按"是 NaN"比较）。为此 **`log_softmax` 的内核改了**（Q10：定义是规范）：以前是 `(x - m) - f32(log_f64(sum_f64))`，在 f64 里取对数；张量没有 f64 的元素类型，定义写不出这一步，所以内核改为照定义：和舍入为 f32，用 `log` 算子的循环取对数，再做一次 f32 减法。`log_softmax` 的结果可能在最后几位变化；仓库里没有测试的期望值因此改变（张量、自动微分、ONNX 的计划照旧通过）。经定义求导与算子自己的导数规则相对差小于 1e-7（契约：1e-5，不是逐位，因为是不同的算法）；先展开再求导两边完全相同。
>
> 细节见 `docs/technical/41_resources_and_definitions.md` 第 2 节。

### 9.1 是什么

一个高层算子的**定义图**是一张普通的 Camel 图，给出这个算子的完整语义：轴、数值稳定化、累加类型、中间的转换，一样不少。它的用途：自动微分在没有登记 VJP 时从真实的定义求导；导出器与不认识这个算子的后端可以展开它；性质测试拿它当规范。

```camel
// tensor 模块随附的定义（示意；写法依现有张量 API）
/// tensor:softmax@1.0 的定义
func softmax_def(x: Tensor<f32>, axis: int): Tensor<f32> {
    let m = max(x, axis, true)       // keepdims；NaN 规则见 Q5
    let e = exp(x - m)               // Bounded：ulp 上界见 Q4
    let s = sum(e, axis, true)       // 规范算法：f64 累加、最后舍入为 f32（6.2）
    return e / s                     // f32 的逐元素除法
}
```

今天的 CPU 内核与这张图不是同一个算法（2.3：倒数乘法、max 的 NaN 规则、`exp` 的实现）。定义图与融合内核谁是规范，是 Q10；建议**定义图是规范，内核是它的一个实现**，由性质测试逐位比较（`Bounded` 部分按上界比较）。

### 9.2 要规定的

| 项 | 规定 |
|---|---|
| 类型与形状 | 定义图的签名与算子的签名逐项相同：普通形参对应 `Norm` 实参，`with` 形参对应 `With` 实参；静态实参以 `with` 形参出现。命名维度（`constas n`）照常检查 |
| 参数的绑定 | 展开一个 `OPER` 节点 = 以它的 `Norm`/`With` 输入作为实参的一次直接调用（`FUNC`），随后可以内联。绑定记在 `Definition{graph, normMap, withMap}` 里 |
| 许可 | 定义图里的节点带定义图自己的许可列（通常全 0：库作者不能替调用者扩大许可）；展开时属于这个算子的节点再加上被展开节点的许可（6.4） |
| `mayFault` 与效应 | 定义图的效应摘要必须是声明的子集：声明说不会故障的算子，定义图里不能有可能故障的节点；纯的算子，定义图必须纯。登记时检查（4.4 第 5 条） |
| 递归 | 登记时沿"定义图里出现的算子 → 它们的定义图"做环检测，有环拒绝；展开是一个有轮数预算的遍，并受单图容量（65,535）约束（2.9：容量不放宽，装不下就不展开） |
| 版本 | 定义图的内容散列计入算子的语义版本：改定义图而结果不变（重构）是次版本，结果变了是主版本 |
| 生命周期 | 由核心管理：定义图在模块装载时建出，放在全程序的图表里，归声明它的模块所有；图模型第 5 步之后有稳定的 `GraphId`，之前由算子表持有一个冻结图并作为回收的根 |

### 9.3 不强迫提早展开

核心与标准流水线从不自动展开有定义图的算子。展开是一个**被选择**的改写（一个遍，或后端在自己的私有事务里做）。Tiles 可以保留 `tensor:softmax` 作为一个整体（映射到它自己的内核），也可以展开后与周围融合；两者都要与定义图的语义一致。这与 IC 7"分解是选择的改写，不是语义身份的提早丢失"相同。

---

## 10. 图事实

> **实施状态（2026-10-15，`lane/facts`；第三阶段的 3b）**：种类登记、事实库、印记、保守失效与锥散列、改写时的修订号、持久化的挂钩都已实现，见 `include/camel/runtime/facts.h`、`core_facts.h`、`fact_persist.h` 与 `docs/technical/40_graph_facts.md`；给 Tiles 的读法见 14.3。与下文的差别（都是第 5 步之前的过渡或窄读）：
>
> - 命名空间是 `camel::runtime::fact`；锚点里的图是 `GCGraph *`。**`GraphId` 与修订号的替身**：`GCGraph::lineage()`（新图得到新的，提交把它传给它编码出的副本）与 `GCGraph::serial()`（全进程序号；提交对没变的图保留，对变了的图给新的）。10.4 说的过渡方案是"每个对象一个序号"；因为今天每次提交都重新编码整个闭包，那样任何提交之后所有事实都过期，所以改为"按内容是否变化保留序号"：被 `edit()` 或消解了门、并且编码出的内容确实不同（`sameContent`：记录、边、负载、端口、许可、帧与静态区布局、静态值、按副本对应的被引用图；源码位置不算）的图，以及**直接或间接引用它的图**，得到新序号。
> - **序号是传递的**，所以印记只记锚点所在各图的 `(lineage, serial)`：事实经由锚点图读到的图都在它下面。这比 10.3 的"每张读过的图各记修订号"粗：被调图改了，调用者上的事实也过期。第 5 步有了快照的图表之后换成逐图的修订号，接口不变。
> - 每次读都比较（发布构建里也比较）；过期读为"没有"并删除。`Dependency::Cone` 的种类（`core.shape`）在序号变了时重算锥散列，相同即重新盖印（`revalidated`）。
> - 10.5 的 `tx.keep` 与提交时对 `keep` 下来的事实的两级校验**没有做**（本轮的范围是登记、存放、印记、改写时的失效与重算）；校验器是 `FactStore::verify`（重算并比较），测试用它确认改写之后没有任何过期事实被沿用。
> - 第 (i) 类的种类（`core.permissions`）每次从图上读，事实库拒绝存放；第 (iii) 类必须带目标散列，登记时不许有编码与解码（核心从不持久化它）。
> - 持久化（10.6）：没有 `.cmo` 缓存，只实现挂钩与往返测试：可移植的图内容散列（含下面所有的图；算子按 URI、语义键与版本、traits）、`PortableFact` 的文本形式、导出与装载时的核对。
> - 种类的键是 `owner.name`，`core.` 归核心，`tiles.` 归 Tiles 模块（任何模块的前缀都归它自己）。

### 10.1 什么是事实，什么不是

| 类 | 是什么 | 放在哪 | 进缓存 |
|---|---|---|---|
| **(i) 不能从图上恢复的语义信息** | 程序写下的数值许可、用户写的约束（命名维度的关系已经在类型里）、定义图的绑定 | **图的内容**：许可列、类型、算子表的声明 | 必须：随图块保存（`.cmo`），参与图的内容散列 |
| **(ii) 可推导的事实** | 节点的实例（域、映射）、值域、形状的细化、别名分析、资源区域的细化、循环不变量 | **事实库**（本节） | 一般重算；缓存时必须带依赖印记，装载时验 |
| **(iii) 与目标相关的结果** | CUDA 布局、分块、调度、调优结果、生成的代码 | **不在核心**：Tiles 自己的编译缓存 | Tiles 的缓存，键见 10.6 |

设备地址、流与事件句柄、活着的分配**永不持久化**。

把 (i) 做成图的内容而不是事实，是这一节最重要的选择：它们没有"过期"一说——图变了它们跟着变（改写规则见 6.4），图没变它们就对。只有 (ii) 需要失效机制。

### 10.2 事实的登记

```cpp
namespace camel::core::fact {

struct Kind {
    SymId name;              // "core.instance", "core.alias", "tiles.layout" (Tiles may register too)
    uint16_t version;        // bumped when the meaning or the encoding changes
    enum class Scope : uint8_t { Node, Edge, Graph, CrossNode, CrossGraph } scope;
    RecomputeFn recompute;   // optional: recompute from the program, for verification (10.5)
    EncodeFn encode; DecodeFn decode;   // optional: only kinds that may be cached across processes
};
using KindId = uint32_t;
KindId registerKind(Kind kind);

/// What a fact is about. A fact may have several anchors: alias(n1, n7); "the body port p of
/// loop L has the domain of outer node n" anchors two graphs.
struct Anchor { GraphId graph; NodeId node = kNoNode; /* kNoNode: the graph as a whole */ };

} // namespace camel::core::fact
```

**存储与表达力分开。** 事实库内部按锚点的第一个 `(GraphId, NodeId)` 建索引（一张节点旁表），这只是存取的入口；一条事实可以有任意多个锚点，跨节点、跨子图都能表达。

### 10.3 印记：依赖记录与保守失效

每条事实带一个印记：

```cpp
struct Stamp {
    SmallVector<std::pair<GraphId, Revision>, 2> graphs; // every graph the fact read
    uint64_t declarationEpoch;                           // OperatorTable::declarationEpoch()
    uint64_t targetHash;                                 // 0 for target-independent kinds
};
```

**保守规则**（缺省，每种事实都适用）：事实读过的每张图都记在印记里——它的锚点所在的图，以及推导时读过的被调图、循环子图、定义图；其中任何一张图的修订号变了，或算子声明的纪元变了，或目标配置变了，事实即过期。这覆盖了 Tiles 要求的各种原因：

| 变化 | 怎样使事实过期 |
|---|---|
| 被改写的节点本身 | 它所在的图修订号变 |
| 上游节点（相关输入）、属性（静态实参）、类型 | 同一张图：修订号变。函数的形参类型变了（例如另一组输入形状的特化）：那是一张新图（新的 `GraphId`，`lineage` 指向原图），旧图上的事实与它无关 |
| 子图（被调图、循环体、分支臂）的内容 | 印记里记着那张图 |
| 算子的声明 | 纪元变 |
| 目标配置 | `targetHash` 变（只对与目标相关的事实种类） |

**精确规则**（可选，给代价高的事实）：事实另记一个**依赖锥散列**——推导时读过的那些节点的种类、算子编号与版本、类型、静态实参、以及沿边到达的端口上的调用方事实——修订号变了之后不直接丢弃，而是重算这个散列（与锥的大小成线性），相同即仍然有效。它把"图的任何地方改了就全部重算"降为"读过的地方没改就不必重算"。

**输入形状变化的例子。** `f(x: Tensor<f32>)` 被特化为 `x: [128, 257]` 得到图 `f'`，Tiles 在 `f'` 上算出每个节点的实例与布局。改成 `[64, 257]` 再特化，得到另一张图 `f''`：`f'` 上的事实不会被错用在 `f''` 上（图不同）。若某个遍就地把 `f'` 的端口类型改了并提交，`f'` 的修订号变，它上面所有的实例、存储计划事实都过期。

### 10.4 发布构建里也查

读一条事实时比较印记：每个 `(GraphId, Revision)` 一次整数比较（从快照的图表取当前修订号），外加两个 64 位比较。常见的事实只有一两张图，所以一次读是几次整数比较——**发布构建里始终检查**，不靠断言。过期的事实读出为"没有"，不报错；调用方重算。

修订号从哪来：

- **图模型第 5 步之后**：每个 `GraphId` 的表项带一个修订号，每次提交换掉它的冻结块时加一。快照之间共享表段（图模型 4.3），所以旧快照上的事实对旧快照仍然有效。
- **第 5 步之前（过渡）**：今天每次改写都会重新编码出新的 `GCGraph` 对象，冻结图不可变。给每个 `GCGraph` 在编码时分配一个全进程单调递增的 64 位序号，以 `(序号)` 代替 `(GraphId, Revision)`：对象没变就是同一个序号；对象被释放后地址复用也不会撞上旧序号。Tiles 今天在一次调用里快照、编译、执行，不跨调用保存事实，所以过渡期的事实只需要进程内有效。

### 10.5 提交时

`Rewrite::commit()`（图模型 5.1）对事实做三件事：

1. 被 `edit()` 过的图修订号加一。**事实不随拷贝、克隆、改写自动存活**（IC 7："派生的事实不是碰巧在克隆后留下来的陈旧注记"）。
2. 遍可以在事务里**重新确立**它知道仍然成立的事实：`tx.keep(kind, anchors, value)`，提交时以新的修订号盖印。一个遍只能 `keep` 它登记过"会维护"的种类。
3. 校验分两级，与图模型 I10 的类型校验同一种分法：结构级每次提交都做（印记里的图都存在）；在调试构建与 `--verify-graphs` 下，对 `keep` 下来的、登记了 `recompute` 的事实重算并比对，不同即提交失败并指明遍与事实。

### 10.6 三类信息的持久化

| 类 | 做法 |
|---|---|
| (i) | 许可列是图块里的一列（图模型 4.1 的排布加一个可选的列；4.5 的 `GRPH` 节区随之多一列）；定义图的绑定在 `OPRS` 的算子项里。它们是图的内容，进内容散列，所以不需要单独的失效规则 |
| (ii) | **第一阶段不缓存**，每个进程重算。以后若要缓存：一个可选的 `FACT` 节区，每条事实以"图块的内容散列 + 锥散列 + 用到的 `OperatorKey` 与版本 + 事实种类与版本"为印记（进程内的 `GraphId`、修订号、`OperId` 都不能进文件）；装载时逐条验，不符即丢弃。注意 `.cmo` 存的是前端建成的图、不是任何遍之后的图（图模型 4.6），所以可缓存的只是关于前端产出的事实 |
| (iii) | Tiles 的编译缓存，与 `.cmo` 分开。键至少包括：特化后程序的内容散列（今天的生成源码文本可以充当）、用到的每个 `OperatorKey` 与版本、每个节点的许可、硬件（计算能力、设备型号）、驱动与 NVRTC 版本、Tiles 的版本、编译选项（包括 6.9 第 4 条的精度选项）、调度选项 |

### 10.7 与图模型第 5、9 步的衔接

- **第 5 步（快照与局部提交）**给出稳定的 `GraphId` 与修订号，事实库挂在快照上，随快照共享与释放。本稿的事实库可以先按 10.4 的过渡方案实现，第 5 步之后把印记的键换掉，接口不变。
- **第 9 步（`.cmo`）**：`OPRS` 节区的每个算子项加 `OperatorKey` 的版本（4.1 的装载规则）；`GRPH` 的图块加许可列；`FACT` 节区是可选的后续项。图模型 4.7 的失效条件第 5 条"`OPRS` 里每个算子在本进程里存在且签名相同"扩充为"且语义版本兼容"。

---

## 11. `LOOP` 的消费接口

> **实施状态（2026-10-15，`lane/loop`）**：`LOOP` 节点、11.1 的访问器（`include/camel/runtime/loop.h`，`camel::runtime::LoopView`、`loopOf`、`effectSummaryOf`）、11.2 的 I18、I19（每次提交，`validateRuntimeLoopInterface`）、11.4 的私有降级（`include/camel/runtime/loop_lowering.h`，`lowerLoopsToTailRecursion`，结果是脱离主程序的副本）都已实现。与下文的差别：命名空间是 `camel::runtime` 而不是 `camel::core`；`GraphId` 是 `GCGraph *`、`NodeId` 是 `gc_node_ref_t`、`TypeId` 是 `Type *`（图模型第 5 步之后换）；`condGraph()` 在 `For` 时返回 `nullptr` 而不是空的 `optional`；`EffectSummary` 是四个布尔量（外部效应、写变量、可能故障、可能不终止），每次询问时由节点算出，所以 I20 不需要另外比对；11.3 的 `core.loopDomain` 事实已随事实库（3b）实现（第 10 节开头）。给 Tiles 的读法见 11.5。

`LOOP` 节点本身（语法、建图、编码、校验、打印、降级）是图模型第 10 步与循环设计稿第 7 节的工作（实施情况见图模型第 7 节）。本节规定的是消费者看到的接口，按图模型 3.5 的卡片与 2.12、2.13 的裁定（条件图与循环体图共用一份捕获表；`for … while` 是同一种节点的第三种形式；多个状态时循环体返回元组，循环的值也是这个元组；`do` 式的写法）。

### 11.1 访问器

```cpp
namespace camel::core {

enum class LoopKind : uint8_t { While, For, ForWhile };

class LoopView {
  public:
    LoopKind kind() const;
    size_t stateCount() const;                       // k
    TypeId stateType(size_t j) const;

    // Inputs of the LOOP node (outer graph)
    std::optional<NodeId> iterable() const;          // For/ForWhile: Norm input 0
    NodeId initialState(size_t j) const;             // the Norm input after the iterable
    std::span<const NodeId> captures() const;        // With inputs; shared by cond and body

    // The two graphs
    std::optional<GraphId> condGraph() const;        // While/ForWhile
    GraphId bodyGraph() const;

    // Correspondence between the node and the graphs' ports
    NodeId condStateParam(size_t j) const;           // cond's j-th Norm port
    std::optional<NodeId> bodyElementParam() const;  // For/ForWhile: body's first Norm port
    NodeId bodyStateParam(size_t j) const;
    NodeId condCapturePort(size_t c) const;          // captures()[c] <-> cond capture port c
    NodeId bodyCapturePort(size_t c) const;          //               <-> body capture port c

    // Next state
    NodeId nextState() const;                        // the body graph's result node
    /// k > 1 and the body builds its result with a tuple FILL: the node that fills slot j;
    /// otherwise nullopt (the consumer projects with ACCS).
    std::optional<NodeId> nextStateComponent(size_t j) const;

    EffectSummary condEffects() const, bodyEffects() const;
};

LoopView loopOf(const GraphView &graph, NodeId node);   // asserts kind == LOOP

} // namespace camel::core
```

例：

```camel
func row_sums(rows: Tensor<f32>[]): (Tensor<f32>, int) {
    return loop acc = zeros([4]), n = 0 for r in rows while n < 100 do (acc + r, n + 1)
}
```

`kind() == ForWhile`；`iterable()` 是 `rows`；`initialState(0)`、`initialState(1)` 是 `zeros([4])` 与 `0`；`captures()` 为空；`bodyElementParam()` 是 `r` 的端口；`nextStateComponent(0)` 是 `acc + r` 那个节点。

### 11.2 核心在提交时校验的

图模型 I13 已经要求"`LOOP` 的入边数与两张子图一致、子图的角色是 `LoopCond`/`LoopBody`"。本稿补上：

- **I18 状态类型**：每个初值的类型可以赋给对应的状态类型；循环体的返回类型等于状态类型（k = 1 时是那个类型，k > 1 时是 k 元组）；条件图返回 `bool`；`For` 形式的元素端口类型等于被遍历数组的元素类型。
- **I19 子图接口**：条件图与循环体图的捕获端口个数、次序、类型都相同，并与节点的 `With` 输入一一对应；条件图的普通形参是 k 个状态；循环体图的普通形参是（元素）+ k 个状态。
- **I20 效应摘要**：`LOOP` 节点的效应是两张子图摘要之并，加"可能故障"（迭代可能不终止，循环设计稿 4.8）；与图级摘要一起重新计算比对（I14）。

### 11.3 迭代域

`For` 形式的循环有一个 `Sequential` 维 `t ∈ [0, len(xs))`；元素端口的访问是 `xs(t)`；状态是一个递推。核心把它作为 `LOOP` 节点的一条可推导事实（`core.loopDomain`）给出；`While` 形式的维度上界未知（参数，没有值）。

### 11.4 降为尾递归的时机

- 核心提供 `lowerLoopToTailRecursion(Rewrite &tx, GraphId graph, NodeId loop)`（图模型 3.5 卡片末尾的固定改写）。
- **它只在各后端自己的入口、在后端私有的事务里调用**：后端从程序的快照开一个事务、降级、在自己的副本上工作，从不把降级提交回共享的程序。标准流水线里的遍（`std::opt`、`std::inline` 等）**不得降级 `LOOP`**；需要它的遍把它当不透明节点（循环设计稿第 7 节第 1 步）。这样 Tiles 在流水线的任何位置看到的都是结构化的 `LOOP`。
- 展开、分块、流水化一个循环是否合法，由 Tiles 判断；核心提供的是上面的访问器、11.3 的迭代域、循环体节点"只依赖捕获端口"（循环不变量）这类可推导事实。

### 11.5 给 Tiles 的说明：怎样读一个 `LOOP`

```cpp
#include "camel/runtime/loop.h"
namespace rt = camel::runtime;

for (rt::gc_node_ref_t ref = 0; ref < g->nodeCount(); ++ref) {
    if (g->node(ref)->kind != rt::GCNodeKind::Loop) continue;
    const rt::LoopView loop(*g, ref);
    // 形式与状态
    loop.kind();                 // While / For / ForWhile
    loop.stateCount();           // k；loop.stateType(j)；loop.valueType() 是状态或 k 元组
    // 外层图里的输入
    loop.iterable();             // For/ForWhile：被遍历的数组（Norm 0）
    loop.initialState(j);        // 第 j 个状态的初值
    loop.captures();             // With 输入，两张子图共用
    // 两张子图与端口的对应
    loop.condGraph();            // While/ForWhile；For 时为 nullptr
    loop.bodyGraph();
    loop.condStateParam(j);  loop.bodyStateParam(j);  loop.bodyElementParam();
    loop.condCapturePort(c); loop.bodyCapturePort(c);  // 对应 captures()[c]
    // 下一轮的状态
    loop.nextState();            // 循环体图的结果节点（k > 1 时是元组）
    loop.nextStateComponent(j);  // k > 1 且结果是元组 FILL 时，填第 j 个位置的节点
    loop.effects();              // 两张子图之并，加"可能故障""可能不终止"
}
```

- 语义：每一轮先求条件（`While`、`ForWhile`；条件看得到状态，看不到元素），再取下一个元素（`For`、`ForWhile`），再求循环体；条件为假或元素取完即结束；循环的值是最后的状态。`camel/core/loop_kind.h` 写明了这一点。
- Tiles 在流水线的任何位置拿到的程序里 `LOOP` 都是结构化的：任何遍都不降级它。**运行时 `tiles.run<f>` 等在每个后端上收到的都是程序原来的 `f` 的图**（`lane/facts` 起）：`std::nvm` 原生执行 `LOOP`；`std::fvm`、`std::jit`（默认）、`std::prl`、`std::cpp` 在私有副本上降级，副本里每张图记着它复制自程序的哪张图（`GCGraph::originalGraph()`），声明了 `inspectsGraphs` 的算子（`OperatorTraits::inspectsGraphs`：Tiles 的 `run`、`run_device`、`source`、`inspect`、`benchmark`，`onnx:export_model`，`autodiff:grad`、`value_and_grad`）的每个函数类型的操作数在副本里先经过 `:graph/original`，把副本的函数值换回原图、闭包不变。所以在降级的后端上 Tiles 看到的也是带 `LOOP` 的 `f`，不是 `f.loopN.rec`、`.next`、`.done`。新的模块算子若读函数值的图，同样在登记时声明 `inspectsGraphs`。
- Tiles 自己决定原生处理还是在自己的入口降级：降级用 `lowerLoopsToTailRecursion(context, root)`，它返回一个脱离主程序的副本，不改共享的程序。
- 校验已保证：初值能赋给状态类型、循环体返回状态类型、条件返回 `bool`、元素端口类型是数组的元素类型、两张子图的捕获端口个数、次序、类型相同并与 `captures()` 一一对应（I18、I19）。

---

## 12. 分阶段计划

按 Tiles 的次序。每一阶段可以独立合入；核心一侧在 CPU 后端上测试（核心没有 GPU 测试环境）。测试照仓库规则：先在命令行手工运行、看原始输出，再写自动断言（`AGENTS.md`）。

| 阶段 | 内容 | Tiles 得到 | 核心怎样测 |
|---|---|---|---|
| **1** | `OperatorKey` 与版本；`OperatorSemantics` 的记录与 `declareSemantics`/`semantics`/`bind`；4.4 的登记检查；`NumericSemantics` 与五个许可位；许可列（存储与转储，全为 0；前端标注等 Q1–Q3）；`OpDef` 加 `semantics` 字段 | 稳定的头文件与查询接口；可以把按名字字符串的分派换成按声明（第 13 节）；知道每个算子是哪一类数值、对哪些许可敏感 | 登记检查的单元测试（每条检查一个被拒绝的声明）；`bind` 的版本规则；许可列在 `std::rgir` 里可见、内联与克隆后保持（6.4 的表逐行一例） |
| **2** | 第一批声明：逐元素族（`add`、`subtract`、`multiply`、`divide`、`maximum`、`minimum`、`neg`、`abs`、`exp`、`log`、`sqrt`、`rsqrt`、`relu`）连同广播；`transpose`、`permute`、`slice`、`reshape`；`sum`/`mean`/`max`/`min` 的按轴与全量、`sum_to`；`matmul` 及其 k 维的律；内建 `:op/` 的 f64 标量算子 | 这些算子的实例（域、映射、律）；Tiles 的 `reindex`、广播、归约的累加选择都从声明推出 | **声明的性质测试**：一个只用于测试的"声明求值器"——按实例遍历域、按访问映射取值、按律与规范算法合并——对随机形状（含空、1、奇数、负轴、广播）与特殊值（NaN、±∞、±0、次正规数）算出结果，与 CPU 内核**逐位**比较（`Bounded` 类按 ulp 上界）；映射的健全性测试：小形状上穷举域，验证声称 `Unreachable` 的访问都在界内、`Unique` 的写确实单射。这一步会把 2.3 与 6.9 的核心审计项变成失败的测试，先登记为已知缺陷，等 Q4–Q7 裁定后修 |
| **3a** | `LOOP` 的消费接口（第 11 节），随图模型第 10 步 | 结构化的 `LOOP` 与访问器；降级只在入口 | 前端产出的三种形式各一例，访问器逐项核对；I18–I20 各一个违反的变异；降级后四个后端输出一致 |
| **3b**（已实现，`tx.keep` 与两级校验除外，见第 10 节开头） | 事实库：种类登记、印记、过渡期的图序号、`tx.keep`、两级校验 | 在核心的事实库里存放自己的分析结果，并得到"永不读到过期事实"的保证 | 变异测试：算一条事实 → 改上游节点 / 改形参类型 / 改被调图 / 换声明 → 读出为"没有"；发布构建下同样的测试；`--verify-graphs` 下一个故意 `keep` 错误事实的遍被提交拒绝 |
| **4**（已实现，gather/scatter 的声明除外：没有这样的算子；见第 8、9 节开头） | 资源区域与别名的声明（第 8 节）；`Indirect` 访问与 gather/scatter 的声明；定义图（第 9 节）的登记、递归检测、展开遍 | 资源与别名事实；有定义图的算子可以展开或保留 | 就地算子与 `var` 形参的声明与效应摘要一致；定义图与内核的逐位比较（这会暴露 `softmax`，见 Q10）；有环的定义图被拒绝 |

先把接口的形状与语义的边界定稳（第 1 阶段），再铺声明。第 1、2 阶段不依赖图模型的第 5 步（用进程内的 `OperId` 与过渡期的图序号）；第 3b 阶段的完整形态在第 5 步之后；事实的跨进程缓存在第 9 步之后，并且不急。

---

## 13. Tiles 现有代码怎样迁移

| Tiles 今天（`modules/tiles/src`） | 迁移到 | 说明 |
|---|---|---|
| `OpRegistry::find(id)` 后按 `def->name` 的字符串分派（`compiler.cpp:326` 起） | `table.semantics(id)` 与 `key(id)`；分派的依据是声明的类与实例 | 名字仍可用于诊断 |
| 逐元素名字列表（`compiler.cpp:418-419`、`schedule.cpp:42-48`） | `numeric.cls` 与实例的"全是恒等或广播"判定（5.5） | 新的逐元素算子不必改 Tiles |
| `broadcastIndex`、`mappedIndex`、`reindex` 的偏移与步长（`codegen.cpp:20-49`；`compiler.cpp:367-397`、`:438-490`） | `instantiate` 得到逻辑映射，再与 Tiles 自己的行主序存储映射复合 | `reindex` 节点可以保留为 Tiles 的物理表示 |
| 融合时的坐标代入（`codegen.cpp:181-201`） | 实例上的 `substitute`/复合 | |
| `sum_axis` f64、`sum_to` f32、全量 f64 的选择（`codegen.cpp:103-112`、`:234-285`） | 读 `ReductionLaw` 的累加类型、初值、顺序 | 新的归约不必改 Tiles |
| 并行最大值的合法性（`schedule.cpp`、`codegen.cpp:118-140`） | `reorderFreedom`：Q5 选 A 后最大值可以任意分组，不再需要带下标 | `argmax` 仍带下标 |
| `fmaf`（`codegen.cpp:164`） | 有许可 (b) 才用，或按 Q7 | 6.9 审计项 1 |
| `--fmad=false` 之外的精度选项 | 显式传入、进缓存键 | 6.9 审计项 4 |
| 标量除法的故障字（`codegen.cpp:91-93`、`cuda.cpp`） | 机制留在 Tiles；**何时**要检查读 `FaultSpec{DivisionByZero, operand 1}` | 张量除法没有这条 `FaultSpec`，与今天一致 |
| 空轴上的 `max` 拒绝（`compiler.cpp:436`） | 读律的 `Empty::Fault` | |
| 递归检测与深度 64（`compiler.cpp:145`） | 不变（这是 Tiles 对直接调用的展开策略）；定义图的递归由核心在登记时拒绝 | |
| 形状推断复用 `infer` | 不变 | |
| 布尔分支的特化 | 不变 | |
| `LOOP`（今天不支持） | 第 11 节的访问器；Tiles 自己决定原生降级还是在入口降为尾递归 | |
| 编译缓存按源码文本（`cuda_backend.cpp:234-275`） | 进程内不变；持久化时按 10.6 的 (iii) 扩充键 | |
| 存储计划、启动维度、共享内存、流、模块租约 | 不变，属于 Tiles | |

---

## 14. 留给 Tiles 的待办（含审计项）

1. **审计：matmul 的 `fmaf`**（6.9 第 1 条）。等 Q7；在此之前，`benchmark` 的报告里把 `fmad=false` 与"matmul 用显式 FMA"并列写明，不要让读者以为整份内核没有收缩。
2. **审计：容差测试**（6.9 第 2 条）。`CorrectlyRounded` 类改为逐位比较；`Bounded` 类的容差由声明的 ulp 上界推出。
3. **审计：`relu` 的 NaN 与 −0**（6.9 第 3 条，Q6）。
4. **NVRTC 精度选项写死并进缓存键**（6.9 第 4 条）。
5. **`expf`、`logf` 对照声明的上界**（6.9 第 5 条，Q4）。
6. 迁移分派与逐元素判定（第 13 节前两行），在第 1、2 阶段合入之后。
7. 持久化编译缓存时的键（10.6 (iii)）；设备地址、流、事件、活分配不持久化。
8. 第 3b 阶段之后，Tiles 自己的分析结果（布局、分块）若放进核心的事实库，种类登记为 `tiles.*`，带 `targetHash`。
9. 调度、布局、Tensor Core、流与事件、内存池、自动调优：按分工，不需要核心做任何事（入口与内核语言见 tiles-route）；需要的语义事实缺了，提给核心。

### 14.1 第一阶段对 Tiles 改了什么（`lane/opsem-p1`）

**CPU 的结果变了，Tiles 的参考随之变：**

- `max`/`min`（逐元素的 `maximum`/`minimum`、`max_axis`/`min_axis`）是 IEEE 754-2019 的 `maximum`/`minimum`：任一边是 NaN 得**规范的 quiet NaN**（`0x7fc00000`，符号位为 0，无载荷；与输入是哪个 NaN 无关，所以任意次序、任意分块逐位相同），`-0 < +0`。Tiles 的 `max_axis` 今天保留"首个 NaN 与 ±0 的平局规则"并带下标比较，要改为这条规则，带下标的并行比较可以去掉（6.9 第 6 条）。CUDA 的 `fmaxf` 不是它：用"两边有 NaN 则规范 NaN，否则相等时按位与（取 `+0`）、不等时取大"的比较序列。
- `relu(x) = maximum(x, +0)`：NaN 得规范 NaN，`-0` 得 `+0`。Tiles 今天的 `a<0.0f?0.0f:a` 对 NaN 给原 NaN、对 `-0` 给 `-0`，两处都要改（6.9 第 3 条）。
- `matmul` 的规范算法是按 k 递增的 FMA 链，从 `+0` 开始（Q7 取乙）：Tiles 现有的 `fmaf` 累加符合；K 尾部"排除而不是补 0"仍是必要的。CPU 的 GEMM 已改为同一算法（以前每 256 个 k 一块分别从 0 累加再相加），`linear`/`matmul_add` 先算完链再加偏置。
- `softmax` 的 CPU 内核与定义 `exp(x - m) / sum(exp(x - m))` 逐位一致（除法，不是乘倒数；和在 f64 里累加、最后舍入为 f32）。Tiles 把 softmax 写成组合，逐位比较时以定义为准。

**查询一个算子的数值语义：**

```cpp
#include "camel/core/operator_table.h"   // 带 camel/core/operator_semantics.h
using namespace camel::core;
const OperatorTable &table = OperatorTable::instance();
if (const sem::OperatorSemantics *s = table.semantics(operId)) {   // 无锁；nullptr = 未声明，按 Opaque 处理
    s->key.toString();               // "tensor:matmul@1.0"，跨进程的键（编译缓存用它，不用 OperId）
    s->numeric.cls;                  // Exact / CorrectlyRounded / Bounded / Opaque
    s->numeric.compute;              // 舍入所在的类型（Float32、Float64）
    s->numeric.ulpBound;             // Bounded 才有：exp、log 为 2
    s->numeric.specials;             // 与 IEEE 不同之处：exp 把次正规数结果冲为 0（subnormals = false）
    s->numeric.sensitiveTo;          // 哪些许可会改变它的结果；不在其中的许可对它无作用
    s->faults;                       // nullopt = 未声明（看 mayFault 位）；标量除法有 {DivisionByZero, 1}
}
table.bind(sem::OperatorKey::parse("tensor:matmul@1.0").value());  // 读缓存：主版本相同且次版本不高于本进程的才得到 OperId
table.declarationEpoch();            // 声明有增改时加一
```

已声明的：内建 `:op/` 标量的加减乘除、取模、取负、比较（i32/int/f32/float）；张量的 `add`、`subtract`、`multiply`、`divide`、`maximum`、`minimum`、`neg`、`abs`、`relu`、`sqrt`、`rsqrt`、`exp`、`log`、`sum`、`sum_axis`、`mean`、`mean_axis`、`max_axis`、`min_axis`、`sum_to`、`matmul`、`reshape`、`flatten`、`unsqueeze`、`transpose`、`permute`、`slice`、`broadcast_like`、`expand_axis`。`softmax`、`matmul_grad_*`、`cast_like` 等未声明（Opaque）。张量算子的声明描述 f32 的情形；整数与布尔元素一律精确。`camel inspect --ops <程序>` 在每个已声明的算子上打印 `"semantics": {...}`，可以直接核对。下标映射与归约律是第二阶段：Tiles 在此之前仍按今天的方式（`reindex`、名字）取得映射与累加类型。

**查询一个节点被授予的许可：**

```cpp
#include "camel/runtime/graph.h"
uint8_t granted = graph->nodePermissions(nodeRef);   // sem::PermissionMask；没有许可列的图一律 0
bool any        = graph->hasNodePermissions();
// 位：sem::kReassociate、kContract、kApproxMath、kChangePrecision、kNoNans、kNoInfs、kNoSignedZeros、kFlushSubnormals
// （源码里的 finite_math = 后四位）；sem::permissionsText(mask) 给出名字。
```

- 许可只由程序写 `@numeric(...)` 授予（函数或块，词法上嵌在其中的臂、循环体、lambda 一起），被调用的函数不继承；没有模块级默认、没有命令行开关，**`tiles.run` 等入口不接受许可**。
- 许可在 OPER 节点上；内联与克隆后各节点带自己的许可；`std::opt::cse` 与 `tensor::fuse` 不把许可不同的节点合并（融合出的节点带它们共同的许可）。Tiles 自己融合时同样按 6.4 的表：收缩 `a * b + c` 要两个节点都有 `contract`，重新加括号要链上每个节点都有 `reassociate`。
- 编译缓存的键要包含用到的每个 `OperatorKey` 与每个节点的许可（10.6 的 (iii)）。
- 许可只允许、不要求：没有许可时必须给规范结果；有许可时给规范结果也总是对的。

### 14.2 第二阶段给 Tiles 的（`lane/opsem-p2`）

**CPU 的结果、图的转储、程序的输出都没有变**；只增加了声明与查询。已声明下标语义的张量算子的键升为 `@1.1`（次版本：只多了事实，4.1）；内建 `:op/` 仍是 `@1.0`；新声明的 `nn:max_pool2d`、`nn:avg_pool2d`、`:mark/unordered_reduce_arr` 是 `@1.0`。

**已声明的实例**（`camel inspect --ops` 里 `"indexing": true` 的算子）：逐元素的 `add`、`subtract`、`multiply`、`divide`、`maximum`、`minimum`（广播，数作为 0 维操作数）、`neg`、`abs`、`exp`、`log`、`sqrt`、`rsqrt`、`relu`；`transpose`、`permute`、`slice`（正步长；负的起止点从末尾数，之后仍在 [0, n] 之外是故障 `SliceOutOfRange`、没有实例，与数组的 `slice` 一致，设计原则 2.10；起点在终点之后是空的范围，2.11）、`reshape`、`flatten`、`unsqueeze`、`broadcast_like`、`expand_axis`；`sum`、`mean`（全量）、`sum_axis`、`mean_axis`、`max_axis`、`min_axis`、`sum_to`；`matmul`（NumPy 的一维与批维广播规则，k 是归约维）；`nn:max_pool2d`、`nn:avg_pool2d`（第 5.2 节例 4 的窗口：越界的元素 `max` 排除、`avg` 读 +0）；`:mark/unordered_reduce_arr`。每个实例由声明求值器（`tensor::test_index_eval`）在 2520 个随机调用上与 CPU 内核逐位比较过（第 12 节）。

**取得一个实例：**

```cpp
#include "camel/runtime/operator_instance.h"   // 带 camel/core/operator_semantics.h、index_semantics.h
using namespace camel::core;
// (1) 冻结图上的 OPER 节点：输入的类型（带形状）与静态输入（DATA 常量，经过 GATE 也算）
std::optional<sem::IndexedInstance> inst = camel::runtime::instanceOf(*graph, nodeRef);
// (2) Tiles 已经为 def->infer 建好了 InferContext（类型 + ConstArg）：直接问 OpDef
#include "modules/tensor/ops/indexing.h"
auto inst2 = camel::tensor::ops::instantiate(*def, tensor::ops::InferContext(types, constants));
// (3) 只有类型与常量的槽：sem::instantiate(operId, sem::InstanceContext{.norm = types, .statics = statics})
const sem::OperatorSemantics *s = OperatorTable::instance().semantics(operId);
sem::toString(*inst, s);       // 规范的文本形式（也是将来持久化缓存的键的来源）
```

`nullopt`（"无实例"）的情形与读法见 5.4：秩未知、轴或常量实参不是常量、某一维可能是 1 也可能不是（Q9）、内核会拒绝的调用、混合 dtype 的 `matmul`。Tiles 在形状特化之后再问一次即可：特化后的类型给出带值的参数。没有值的参数（`Tensor<f32, const [-1, 3]>` 的第 0 维）仍是参数，实例照样有。

**实例里有什么**（`camel inspect --instances <程序>` 逐节点打印，例：`layers` 里的 `matmul(x: [2,3], w: [3,4])`）：

```
params   s0_0 = extent(0, 0) = 2; s1_1 = extent(1, 1) = 4; s0_1 = extent(0, 1) = 3; s1_0 = extent(1, 0) = 3
domain   d0 < s0_0 parallel; d1 < s1_1 parallel; d2 < s0_1 reduction
where    s0_1 = s1_0
norm 0   (d0, d2)
norm 1   (d2, d1)
write    (d0, d1) combine law fma_f32
law      fma_f32: fma_chain f32 -> f32 -> f32, init 0x0, ascending, empty initial, commutative, identity 0x80000000
```

- `inst.domain.dims`：每维的范围（表达式，`inst.extents()` 在参数都有值时给出整数）与角色；规范顺序是元组的字典序，归约按这个顺序是"递增"。`domain.params`：参数的来源（`extent(操作数, 轴)`、`static(操作数[元素])`、`runtime`、`derived`）与值。`domain.constraints`：参数的前置条件。`domain.guard`：分块之后尾部不存在的点（`sem::splitDims`）。
- `inst.norm[i]`：第 i 个操作数的访问：`Affine` 的 `coords` 每轴一个准仿射表达式（`inst.exprs.evaluate(e, point, inst.paramValues())`、`inst.exprs.affine(e, dims, values)`）；`valid` 与 `onInvalid`（`Unreachable`／`Fault`／`Excluded`／`Padding`）；`None` 表示不读元素（`axis`、`shape` 等常量实参，`broadcast_like` 的 `like`）。
- `inst.output`：写坐标与方式；`Combine` 时 `s->laws[inst.law]` 是归约律（累加类型、初值的位、顺序、空域是初值还是故障、`DivideByCount`、确切的结合与交换、单位元）。Tiles 今天按名字选的累加类型都从律里读：`sum_axis_f32` 是 `f32 -> f64 -> f32`，`sum_f32`（全量）是 `f32 -> f64 -> f64`，`sum_to_f32` 是 `f32 -> f32 -> f32`，从 +0 起；`max_axis_f32` 从 −∞ 起，空轴是故障。
- 逐元素的判定（取代名字列表，5.5）：每个 `Affine` 访问的每个坐标是某个维或常数 0，写是 `(d0, d1, ...)` 的 `Unique`。

**能不能重排**（7.3）：

```cpp
std::optional<sem::ReorderFreedom> f = camel::runtime::reorderFreedomOf(*graph, nodeRef);
// 或 sem::reorderFreedom(s->laws[inst.law], graph->nodePermissions(nodeRef))
f->regroup;          // 分块、树形、split-k
f->permute;          // 原子加、任意次序合并部分和
f->blockIdentity;    // 每块都从单位元起；否则只有第一块带初值，其余从各自的第一个贡献起
f->contract;         // FMA（fma_chain 的律本来就是）
f->changePrecision;  // 换累加或输入精度
```

没有许可时：`max`/`min` 律可以任意分组与置换（IEEE `maximum` 确切可结合可交换，6.9 第 6 条的带下标比较可以去掉）；浮点 `sum` 不行；整数律可以。程序写 `@numeric(reassociate)` 之后浮点 `sum` 可以分组与置换，但 `blockIdentity` 仍为假：初值是 +0 而单位元是 −0，所以只有第一块从 +0 起（7.2 说的"不同的零号"）。`unordered_reduce` 的律是 `Unordered`，自带分组的许可。

**怎样把逻辑映射与 Tiles 的存储映射复合（5.7）。** 核心只给逻辑坐标；Tiles 的 `reindex`（偏移 + 步长）是"访问映射 ∘ 行主序存储"：

```cpp
// x 的形状 shape（行主序）；实例的域就是结果的各维
std::optional<sem::StridedView> v = sem::rowMajorView(inst, inst.norm[0], shape);
// x 在点 t 读的元素的线性下标 = v->offset + Σ v->strides[d] * t[d]
```

例：`transpose` 读 `(d1, d0)`，`shape = [3, 5]` 得 `offset 0, strides (1, 5)`；`slice(x, 1, 1, ., 2)` 读 `(d0, 1 + 2*d1)` 得 `offset 1, strides (5, 2)`；广播的轴读常数 0，步长为 0（Tiles 现在的 `broadcastIndex`）；`reshape` 跨轴时坐标里有 `floordiv`/`mod`，`rowMajorView` 返回 `nullopt`：它不是步长视图，按 Tiles 现在的做法是"线性下标不变"（行主序存储下逻辑映射与存储映射恰好抵消，5.2 例 3）。融合时把生产者的访问代入消费者的坐标（`ExprPool` 的表达式可以按维代入，`sem::splitDims` 是一个现成的例子）。分块：`sem::splitDims(inst, {{dim, 16}, ...})` 把维 `d` 换成 `16*d_o + d_i`，原来的界成为 `guard`：写在尾部不发生，归约在尾部排除（不是补 0），即 Tiles 现在的 `if (base + t < k)`。

**还不在第二阶段里的**：事实库（3b）、`LOOP`（3a）、资源与定义图（4）；`gather`/`scatter` 的 `Indirect` 访问形式已在数据结构里，算子的声明在第四阶段；`conv2d` 的实例（im2col 之后的 FMA 链加偏置）没有声明；`softmax`、`argmax` 等仍是不透明的。

### 14.3 事实库给 Tiles 的（`lane/facts`）

**CPU 的结果、图的转储、程序的输出都没有变**；事实只在被问到时算、只在 `camel inspect --facts` 里显示。

**函数值的图是程序原来的图**（11.5）：`tiles.run` 等声明了 `OperatorTraits::inspectsGraphs`（`modules/tiles/src/module.cpp` 的 `load()` 里，核心为此对 Tiles 做的唯一改动），所以在 `std::fvm`、`std::jit`、`std::prl` 上 `with.get<::Function *>(0)->graph()` 也是带 `LOOP` 的原图（`modules.tiles` 的 `loop_structure_*`）。

**读核心的事实：**

```cpp
#include "camel/runtime/core_facts.h"   // 带 camel/runtime/facts.h
namespace rt = camel::runtime;
namespace fact = camel::runtime::fact;
fact::FactStore &facts = ctx.facts();                 // 内核的 Context&；程序的事实库
const fact::CoreKinds &k = fact::coreKinds();
const fact::Anchor at{graph, nodeRef};                // 一个节点；Anchor{graph} 是整张图
auto shape = facts.obtain<fact::TensorShape>(k.shape, at);     // dtype、dims（-1 未知，nullopt 秩未知）
auto inst  = facts.obtain<fact::NodeInstance>(k.instance, at); // inst->instance：optional<IndexedInstance>
auto order = facts.obtain<fact::Reorder>(k.reorder, at);       // regroup、permute、blockIdentity、contract、changePrecision
auto eff   = facts.obtain<rt::EffectSummary>(k.effects, fact::Anchor{graph});
// LOOP：两个锚点（外层图的 LOOP 节点、循环体图的元素端口或整张图）
std::vector<fact::Anchor> loop{{graph, loopRef}, {body, elementPort}};
auto dom   = facts.obtain<fact::LoopDomain>(k.loopDomain, loop); // kind、iterable、extent（常量数组时）、elementPort
```

`obtain` 先读（印记相符就是缓存的值），否则算出、存入、返回；`nullptr` 表示这个锚点上没有这种事实（不是张量、算子没有下标语义）。`get` 只读不算，`probe` 告诉你是 `absent`、`valid`、`revalidated` 还是 `stale`。**同一张图、同一个节点、同一种事实，问多少次都只算一次，直到图变了。**

**登记 `tiles.*` 的事实**（布局、分块等分析结果；与目标相关，是第 (iii) 类）：

```cpp
static const fact::KindId kLayout = fact::registerKind(fact::KindSpec<MyLayout>{
    .key   = "tiles.layout",          // 前缀 tiles. 只归 owner "tiles"
    .owner = "tiles",
    .cls   = fact::FactClass::Target, // 带目标散列；核心不持久化（不许 encode/decode）
    .scope = fact::Scope::Node,
    .print = [](const MyLayout &l) { return l.text(); },  // inspect --facts 显示
    // .compute 可省：由 Tiles 自己的分析 put；MyLayout 需要 operator==
});
const uint64_t target = hashOf(deviceModel, computeCapability, nvrtcVersion, options); // 非 0
facts.put<MyLayout>(kLayout, fact::Anchor{graph, ref}, layout, target);
auto cached = facts.get<MyLayout>(kLayout, fact::Anchor{graph, ref}, target); // 目标不同即过期
```

登记的规则（违反时 `std::invalid_argument`，消息里有键与原因）：键是 `owner.name`；同一个键再登记必须是同一种（版本、值类型、类、范围），返回同一个编号；必须给 `print`；`Dependency::Cone` 必须给锥散列。与目标无关、可由程序推出的结果（例如 Tiles 自己的下标映射复合）可以登记为 `Derived`，带 `compute` 时 `FactStore::verify` 能重算比较。

**印记怎样保护 Tiles**：每条事实带着它锚点所在各图的 `(lineage, serial)`、算子声明的纪元（若种类读声明）与目标散列；每次读都比较（发布构建里也比较），任何一项不同就读为"没有"并删除。`serial` 在某个遍真的改了这张图、或改了它直接间接调用的图时才变（`std::inline`、`std::opt` 的各部分、`std::macro`、特化、后端私有的降级都经由同一个提交）；所以特化成新形状的图是另一张图，旧图上的布局不会被错用；`declareSemantics` 改了算子的声明，读声明的事实全部过期；换了设备或编译选项，`targetHash` 不同。事实是进程内的；跨进程的编译缓存仍由 Tiles 自己保存，键按 10.6 (iii)。

### 14.4 第四阶段给 Tiles 的（`lane/opsem-p4`）

**图的转储不变；CPU 的结果只有 `log_softmax` 变了**（它的内核改为照定义，第 9 节开头：对数在 f32 里取，最后几位可能不同；Tiles 以定义为准对照）；没有遍或后端默认展开定义图。因为加了定义，`tensor:relu`、`tensor:mean` 的键升为 `@1.2`，`tensor:softmax`、`tensor:log_softmax`、`tensor:sigmoid` 第一次有语义记录（`@1.0`，只有定义）；编译缓存若记了这些键，次版本变化按 4.1 处理。

**Tiles 自己的算子已声明资源**（`modules/tiles/src/module.cpp` 的 `load()`，核心为此对 Tiles 做的第二处改动；若 Tiles 想自己维护，直接改那一段即可）：设备内存是外部对象 `tiles:device`，驱动与设备是环境状态 `tiles:driver`；`upload` 新建 `tiles:device`；`download`、`run`、`benchmark` 读第 0 个操作数里的设备值；`run_device` 同样读，并新建 `tiles:device`，结果可能与输入别名（"returned inputs share native ownership"）；`source`、`inspect` 读 `tiles:driver`；全部有外部效应（与 traits 一致）。

**查询一个节点碰了什么、两个节点要不要保持先后：**

```cpp
#include "camel/runtime/resources.h"
namespace rt = camel::runtime;
rt::NodeResources r = rt::resourcesOf(*graph, node);
for (const rt::NodeAccess &a : r.accesses) {
    a.resource.cls;    // Variable / Object / Foreign / Ambient / Unknown（未声明的保守读法）
    a.resource.name;   // "tiles:device"、"fs"、"rand:generator"
    a.mode;            // Read / Write / ReadModifyWrite
    a.value;           // 同一张图里指名这个资源的节点（anyValue 时为 kInvalidNodeRef）
    a.region;          // Whole / Mapped（某个下标映射取到的元素）/ Unknown
}
r.result;              // Fresh / Same / View / MayAlias / Unknown：结果与操作数的资源的关系
r.allocates;           // 结果是新资源（upload）
r.external;            // 程序外可见的效应：这类节点之间保持程序给的先后

rt::ResourceConflict c = rt::resourceConflict(*graph, a, b);   // None / Resource / External，why 是一行说明
bool data  = rt::dependsOn(*graph, b, a);      // 计算依赖：只看数据边
bool order = rt::orderedBefore(*graph, a, b);  // 任何路径（含前端插入的控制边）
for (const rt::RequiredOrdering &o : rt::requiredOrderings(*graph)) {
    o.first; o.second; o.why; o.ordered; o.byData;   // 冲突的一对，图是否已经排序、是否经由数据
}
rt::GraphFootprint fp = rt::footprintOf(calleeGraph);   // 一张图对收到的资源（端口、捕获）做了什么
bool alias = rt::mayAlias(*graph, x, y);                  // 两个值可能是同一个资源吗
```

事实种类 `core.resources`（`fact::coreKinds().resources`，值是 `NodeResources`）在碰了资源的节点上，`camel inspect --facts` 打印。异步完成不在核心：Tiles 的流与事件仍是它自己的，交出 Camel 值之前完成（8.2）；`sync`、`wait` 不是 GPU 原语。

**定义图：**

```cpp
#include "camel/runtime/definitions.h"
const camel::runtime::DefinitionGraph *d = ctx.definitions().definitionOf(operId, &why);
// nullptr：没有定义，或被拒绝（why 说明）。d->graph 是定义函数的冻结图（上下文持有）；
// d->definition.params：算子的第 i 个操作数绑定哪个参数、缺省值；d->contentHash：内容散列，
// Tiles 若缓存"按定义展开后的内核"，键里放 d->key 与 d->contentHash。
// 在 Tiles 自己的私有副本上展开（不要提交到共享的程序上，9.3）：
camel::runtime::RuntimeGraphDraftSession session(ctx, privateRoot);
auto &draft = session.edit(graph);
auto r = camel::runtime::expandDefinition(session, draft, draft.draftIdOfSourceRef(node));  // r.value / r.problem
GCGraph *expanded = session.commit(/*detached=*/true);
```

保留高层算子（映射到自己的内核）与展开后融合都可以，二者都必须与定义一致：`softmax`、`mean`、`relu`、`sigmoid` 在 CPU 上已逐位一致，Tiles 的实现按同一标准对照（`exp`/`log` 按声明的上界）。Tiles 今天把 softmax 写成组合，正与定义同形；注意定义里"没有元素的张量就是自己的结果"这一分支，以及 `max_axis` 用 IEEE `maximum`（14.1）。

**待问（给负责人）**：
1. 随机数生成器的效应：按 8.1 的注释算外部效应，于是它与打印、文件之间也保持先后；若定为"程序内部的状态"，调度器就可以让抽样与输出交换次序（它们彼此仍保持先后，因为都读改写 `rand:generator`）。
2. `mean` 对空张量：内核的故障是"mean of an empty tensor"，定义是标量除以零（`DivisionByZero`）；都是故障，诊断不同。是否要求故障的种类也一致？
3. 定义库在第一次被问到时编译，而不是模块装载时（第 9 节开头的理由）；若要"装载时建出"，每个导入 `tensor` 的程序都要多编一个库。

---

## 15. 要负责人确认的问题

### Q1 五项许可的名字，以及 (e) 拆不拆

候选：(A) 五个词：`reassociate`、`contract`、`approx_math`、`reduced_precision`、`assume_finite`（或 `ieee_specials`）；(B) 同 A，但 (e) 拆成 `no_nans`、`no_infs`、`no_signed_zeros`、`flush_subnormals` 四个子项，写 `assume_finite` 等于前两项。
**建议 B**：四种假设对实现的作用各不相同（忽略负零让 `x + 0 → x` 合法，冲刷次正规数改变的是另一件事），GPU 上的开关也是分开的；但在源码里提供一个合写的名字。存储是一个字节，放得下。

### Q2 授予的作用范围

候选：(A) 只能写在函数上；(B) 函数与块都可以；词法上属于这段代码的分支臂、循环的条件与循环体、在其中定义的 lambda 都算"这段代码"；(C) 同 B，但 lambda 不算（lambda 可能被传到别处调用）。
**建议 B**。lambda 是写在这里的代码，它被传到哪里调用，执行的仍是这里写下的算术；"被调用的函数不继承"说的是调用另一个函数，不是词法上的嵌套。分支臂与循环体在图上是独立的子图，但在源码里就是这个块的一部分，不算的话 `@numeric` 写在一个含 `if` 的函数上会出人意料地只作用于一半代码。

### Q3 模块级的策略

候选：(A) 不提供：只有函数与块上的标注；(B) 模块顶层一个标注，作用于本模块的所有函数（词法上），不作用于导入的模块；(C) 命令行或流水线选项。
**建议 A，B 留到有需求时再加**；**C 不应提供**：它是"不写在代码上的许可"，与"许可写在哪段代码上就只对那段代码生效"相冲突，并且会让同一份源码在不同的命令行下有不同的语义契约。

### Q4 超越函数的 ulp 上界

候选：(A) 每个函数在其声明里写自己的上界，核心标准库统一给一个目标（例如 f32 的 `exp`、`log` 在正常数范围内 ≤ 2 ulp，`tanh`、`sigmoid`、`gelu`、`erf` ≤ 4 ulp），特殊值（NaN 传播、±∞、±0）一律精确；(B) 只要求"写明"，不给统一目标。
**建议 A**，具体数值由负责人定或在第 2 阶段量出现有实现之后再定。无论哪种，特殊值不属于近似的自由（6.8）；现在的 `vmath::exp` 不忠实地传播 NaN，按 A 要修。

### Q5 `max`、`min` 的 NaN 与 ±0

候选见 6.7：(A) IEEE 754-2019 `maximum`/`minimum`；(B) `maximumNumber`；(C) 保持位置相关的现状。
**建议 A**。它是唯一既不隐藏 NaN、又在任何归约顺序下逐位相同的选择，使最大值归约不需要许可就能并行。代价：改变现有程序在 NaN 上的结果（今天 `max([1, NaN, 3])` 得 3），`softmax` 内核也要随之改；张量算子的主版本加一。

### Q6 `relu` 在 NaN 与 −0 上的值

今天 CPU 得 `relu(NaN) = 0`、`relu(−0) = +0`【实测】，Tiles 得 NaN 与 −0【读码】。候选：(A) `relu(x) = maximum(x, +0)`（随 Q5 的 A）：NaN → NaN，−0 → +0；(B) 保持 CPU 的 `x > 0 ? x : +0`：NaN → +0，−0 → +0；(C) 采用 Tiles 的写法：NaN → NaN，−0 → −0。
**建议 A**：不隐藏 NaN，与 Q5 一致；−0 → +0 与 CPU 一致。CPU 与 Tiles 都要各改一处。

### Q7 `matmul` 的规范算法

候选：(A) 字面读法：每个 k 先乘、舍入，再加、舍入，k 递增，从 +0 开始；FMA 只在许可 (b) 下使用；(B) `matmul` 的规范算法定义为"k 递增的 FMA 链"`acc = fma(a, b, acc)`，从 +0 开始。FMA 是 IEEE 754 规定的一次正确舍入的运算，所以 B 同样逐位可复现；这是算子声明"自己做什么"，不是对用户写的 `a * b + c` 的收缩，许可 (b) 对用户代码仍然有意义。
**建议 B，但请负责人明确裁定**，因为它触及 2.13 措辞的读法。理由：Tiles 现有代码与 CPU 的 AVX2/AVX-512 路径（若其 k 次序是严格递增的，【未查】）已经是 B；B 比 A 更精确；A 会让 GPU 与 CPU 的矩阵乘都放弃硬件的乘加融合，峰值大约减半。无论选哪个，CPU 上"没有 FMA 的 x86-64 舍入两次"都与契约不符（选 B 时要用软件 `fma`，慢；选 A 时 AVX 路径要关掉收缩）。

### Q8 提高累加精度算不算许可 (d)

例如把 `sum_to` 的 f32 累加换成 f64：结果更准，但位变了。候选：(A) 不允许，五项许可里没有它；(B) 把 (d) 定为"改变输入或累加的精度"（提高与降低都在内）。
**建议 B**：两个方向对复现性的影响相同，需要的是同一种授予；单列一项不值得。

### Q9 维度是否为 1 未知时的广播

`Tensor<f32, const [-1, 3]> + Tensor<f32, const [-1, 3]>`：第 0 维可能是相等的广播，也可能一边是 1。候选：(A) 实例化返回"无实例"（保守），等特化；(B) 返回一个带守卫的实例：两个候选映射，各带一个运行期条件；(C) 由类型规则在调用处插入运行期检查，把这种情形规定为"维度必须相等，否则故障"，即不支持隐式地对未知维度广播。
**建议 A 起步**；B 是 Tiles 需要动态形状的多变体内核时再加的扩展。C 改变了张量的语义，不建议。

### Q10 定义图与融合内核谁是规范

`tensor:softmax` 的 CPU 内核与它显然的定义（max、减、exp、sum、除）在位上不同（2.3）。候选：(A) 定义图是规范，内核要与它逐位一致（`Bounded` 部分按上界），不一致的内核是缺陷；(B) 内核是规范，定义图只是近似的说明，自动微分与展开只"大致"等价；(C) 每个算子自己选，在声明里写明。
**建议 A**：B 使展开、导出、自动微分各自得到不同的数，与 2.13 的出发点相反。代价是 `softmax` 内核要改（用除法而不是倒数乘法、按 Q5 处理 NaN），可能慢一点。

### Q11 后端入口能否接受许可

例如 `tiles.run<fn>(x)` 带一个"允许重排"的参数。候选：(A) 不能，许可只能写在 `fn` 的代码上；(B) 能，视为调用处写下的授予，作用于 `fn` 的整张图。
**建议 A**：B 等于让调用者把许可传给被调用的函数，正是"被调用的函数不继承"所排除的；需要放宽就在 `fn` 上写标注。在前端支持标注之前，Tiles 只运行规范算法。

---

## 16. 最不放心的地方

1. **声明是代码，不是数据。** 秩多态迫使下标语义以实例化函数的形式注册（5.1），它是否与内核一致，登记时无法检查，只能靠性质测试。一个写错的实例化函数会让 Tiles 生成"合法却错误"的内核。第 2 阶段的声明求值器与穷举的健全性测试是唯一的防线，必须先于任何声明合入。
2. **逐位相同的契约对核心自己的 CPU 实现代价不小。** GEMM 的收缩、`vmath::exp`、`softmax` 的倒数乘法、`max` 的 NaN 规则（2.3）都与契约不符；按契约修，可能有可测的性能回退。负责人应当在 Q5–Q7、Q10 上知道这一点。
3. **在前端支持 `@numeric` 之前，所有许可都是 0。** Tiles 的 Tensor Core（需要 (d)）、Split-K（需要 (a)）、FMA（需要 (b)，除非 Q7 选 B）在那之前都不能合法地用。这会形成"在后端加个开关"的压力，而那正是 Q3、Q11 建议拒绝的。标注的语法与建图应当尽早排进计划。
4. **保守的失效规则可能太粗。** "印记里任何一张图改了就过期"在 Tiles 反复特化、内联之后的大图上，可能让昂贵的分析频繁重算。10.3 的锥散列是退路，但它本身的正确性（锥是否记全）又是一个容易出错的地方。
5. **`LOOP` 的接口是按设计稿写的**，节点本身还没有实现；实现时若改了子图的端口次序或元组的表示，第 11 节要跟着改。
6. **准仿射语言的边界。** 例 2 的 `max` 要靠"把它登记为参数再加约束"绕过；更复杂的形状函数（`ceil`、条件表达式）会越来越多地落到"参数加前置条件"上，分析能力因此退化为"未知"。要观察实际的声明是否频繁地掉进这个口子。

---

## 附录 A 复现

**A.1 CPU 张量算子在 NaN 与 ±0 上的行为**（2.3、6.7、Q5、Q6）。在 `camel-lanes` 容器的 `gen` 通道里用已有的构建（构建时间 2026-10-15 06:45，未重新构建；源码为 `6bf31d1f`）运行：

```camel
import { Tensor, reshape, relu, maximum, minimum, max } from tensor

func pick(mask: float[], vals: float[]): Tensor {
    let m = reshape(mask, [len(mask)])
    return (m / m) * reshape(vals, [len(vals)])      // mask 为 0 的位置是 0/0 = NaN
}

func main(): int sync {
    let nz = -0.0
    println(relu(pick([0.0, 1.0, 1.0, 1.0], [1.0, nz, 1.0, -2.0])))
    let a = pick([0.0, 1.0, 1.0, 1.0], [1.0, 1.0, 0.0, nz])
    let b = pick([1.0, 0.0, 1.0, 1.0], [1.0, 1.0, nz, 0.0])
    println(maximum(a, b))
    println(maximum(b, a))
    println(minimum(a, b))
    println(max(pick([1.0, 0.0, 1.0], [1.0, 1.0, 3.0]), 0))
    println(max(pick([0.0, 1.0, 1.0], [1.0, 1.0, 3.0]), 0))
    println(max(reshape([0.0, nz], [2]), 0))
    println(max(reshape([nz, 0.0], [2]), 0))
    return 0
}
```

输出：

```
tensor([      0.,       0.,       1.,       0.], dtype=float32)     relu([NaN, -0, 1, -2])
tensor([   -nan,       1.,       0.,      -0.], dtype=float32)     maximum(a, b)，a = [NaN, 1, 0, -0]
tensor([      1.,    -nan,      -0.,       0.], dtype=float32)     maximum(b, a)，b = [1, NaN, -0, 0]
tensor([   -nan,       1.,       0.,      -0.], dtype=float32)     minimum(a, b)
tensor(      3., dtype=float32)                                    max([1, NaN, 3])
tensor(   -nan, dtype=float32)                                     max([NaN, 1, 3])
tensor(      0., dtype=float32)                                    max([0, -0])
tensor(     -0., dtype=float32)                                    max([-0, 0])
```

同一通道里 `let zero = 0.0; let nan = zero / zero` 报 `RuntimeDiag::DivisionByZero`（标量浮点除以零是故障）。Camel 源码里没有 `nan` 这个名字（`UnresolvedReference`）。

**A.2 读码的位置**：本稿引用的文件与行号都以 `6bf31d1f` 为准；Tiles 的源码在 `modules/tiles/src/`，CPU 内核在 `modules/tensor/kernels/`，编译选项在 `CMakeLists.txt:48-54` 与 `modules/tensor/CMakeLists.txt:37-52`。GPU 上的行为本稿没有运行（核心的容器里没有 GPU），凡涉及 CUDA 的都是【读码】或【未查】。
