# 第 7 节第 9 步：`std::cml`（图与源码往返）——实施计划

依据：设计原则 2.5（图与源码的表达能力相同）、3.10（循环是同一张图的两个控制区域）、6.2/6.6（标签的作用域与声明）、10.5（数组与数据块）；图存储设计稿 §3.3（区域）、§4.7（声明标签与 `let` 名字）、§4.8（往返所需的信息）、§7 第 9 步。只读调研，没有改代码；所有测量用基线 `e52ee05a6` 的预编译 `camel.exe` 完成，没有构建。

## 0. 结论摘要

1. **`std::cml` 是一个翻译遍**：读一个 `Program`，把当前图打印成一个可重新编译的模块，返回 `nullptr`（与 `std::jgir` 同类）。它打印的是**当前的图**，不是原始源码；名字、源码位置、注释、原始写法都不还原（2.5 明确不计名字）。
2. **“等价”不能只靠文本**。本计划定义 `canon(P)`：取从根可达的图、丢弃死节点/死区域、**抹掉标注区域**（Inline、Block）、把被内联的入口提升成虚拟 `main`、去掉根的入口调用，然后把名字/位置/编号全部去掉，比较“节点、输入边（种类与槽位）、区域（种类、父、所有者、臂序号、形参、结果）、节点标签、类型（命名类型作原子，按双射匹配）、静态值（按值不按拼写）”。检查器是一个独立的结构比较器，`std::cml` 的文本相等只作为确定性信号。
3. **图里确实没有的**（按 §4.8 逐项测量）：`let` 名字、声明标签（函数/类型/字段/参数各一套）、`Block` 区域（模型有、前端不建）、泛型声明（只有实例）、循环收集槽的源码形式、大常量的数据文件形式、字面量的拼写。其中**声明标签、`Block` 区域、收集槽与大常量形式是需要补的缺口**（2.5 要求补），不是“拒绝即可”的永久边界；`let` 名字不影响等价，泛型声明是 2.5 允许的损失，字面量拼写由打印机负责。
4. **难例的决定**：被优化掉的区域按现图打印；无名值用结构键生成稳定名字；标签用 `Block` 区域 + `region.attrs` 打印（需要前端建块）；泛型实例打印成具体函数/具体类型；函数值打印成名字或 lambda，多个 FILL 共享同一张 lambda 图则拒绝；模块级捕获摊平成顶层 `let`；效应顺序由一个 `sync` 块 + 构造器的省略规则复原；枚举 `match` 的决策树重建；收集槽与大常量在源码形式出现前拒绝并记账。
5. **步骤**：S0 先做等价判定器（`std::jgir canonical=true` + 独立检查器），S1 直行函数、S2 分支/同步/闭包、S3 循环、S4 声明（类型/标签/规则/泛型）、S5 整程序与随机程序。每步独立落地，遍从一开始就对不支持的构造精确拒绝。
6. **它给谁用**：唯一的正式消费者是往返测试本身（2.5 要求“全部测试用例与随机生成的程序”）；附带地，人可以把某个流水线之后的图打印出来，用现有调试器/`std::nvm` 当普通源码看。不为部署（`std::cpp`）、不为编辑器、不做源码级优化器。
7. **最大的风险**是类型表与图同构判定器的工作量（S0 可能与 S1 相当），以及 208 个内置算子 URI 的源码形式表；两者都在计划里单列并有覆盖测试。

## 1. 测量：图里有什么，dump 里缺什么

### 1.1 方法与探针

用预编译的 `D:\Projects\Camel\out\latest\bin\camel.exe`（`CAMEL_HOME` 指向该目录）对下列探针跑 `std::jgir`、`std::gir`、`std::gir_regions`、`std::opt`、`std::cpp`，并读 `include/camel/runtime/graph.h`、`draft.h`、`src/compile/gir/builder*.cpp`、`src/passes/trans/json/gir.cpp` 与 `src/passes/trans/cpp/*`。探针在 `D:\Temp\cmlprobe`，不随仓库提交；关键源码抄在附录 A。任务里提到的 `#resource`/`#gpu`：本树没有 `#resource`；放置只写 `#gpu` 标签（`@gpu` 装饰器与显式运行算子已删除），标签是声明或节点上的编译期事实（6.2），本计划按标签的同一机制处理，GPU 分支本身不在本次范围。

| 探针 | 覆盖 | dump 里看到的事实 |
|---|---|---|
| `p1_regions.cml` | `if`、`match`、两个 `loop` | `pick` 有 2 个 arm 区域，`classify` 有 4 个（`case 1 \| 2` 拆成两臂）；循环是**区域形式**：`loop.condition/body` 为 `null`，`conditionRegion/bodyRegion` 指向 `g1r1/g1r2`；区域记录只有 `id/kind/parent/owner/result`，**没有 `index`、`ports`、`params`、`attrs`**；`pick` 的第 1 臂没有节点，`result` 是区域外的端口 `g2n0`。 |
| `p2_tags.cml` | `#kernel { let a = x + 1 }`、`tag finite = [kernel, heavy]` | 标签只在**节点**上：`+` 节点带 `main::kernel`，`finite` 把两个标签都加到节点；**没有建 Block 区域**；`let` 本身不是节点。 |
| `decl.cml`（`test/cases/compile/tags`） | `#kernel` 在函数上、`#shape`/`#width(32)` 在类型/字段上 | `std::jgir` 里**完全没有**这些标签：函数标签没有落到节点（`marked` 只有 `DATA 1`），类型/字段标签没有存储位置。`#strict` 是例外，它作为节点标签 `strict` 落在每个节点上（`p21`）。 |
| `check_rules.cml` / `check_when.cml` | `rule` 声明 | 规则是普通图：`add_zero__lhs`、`add_zero__rhs`，加一个 `let add_zero = ...` 的构造图（7 个节点：名字字符串、单向/双向布尔、权限掩码、两个函数值、`rewrite:rule`）；`when` 形式多一个 `__when` 图和 `rewrite:rule_when`。`#requires(fast)` 变成掩码 `3`，组名丢失但位还在。 |
| `bound_closures.cml` | lambda、闭包、间接调用 | lambda 是图 `__g_3__`…；闭包是 `DATA Function` + `FILL`（`tupleSlots=1`）；调用是带函数值输入的 `CALL`；捕获在 `capturePorts`。 |
| `p6_generic.cml` | 泛型函数/类型 | 实例图 `id<int>`、`id<string>`、`pair<float>`；**没有模板图**，类型参数只在名字里。 |
| `p8_literals.cml` | 字面量 | `1.0e10` 的标签变成 `10000000000.0`（值对、拼写丢）；字符串原样打印、没有引号/转义（标签里直接出现换行、制表符）；数组是 `[1, 2, 3]`、嵌套数组；`f32` 的 `3.0` 值对。 |
| `p9/p10` + `std::opt` | 内联、折叠 | `main` 被内联进 `__root__`，产生 `inline` 区域（`inlined: "main"`）；`if true` 的臂被折掉；`choose` 留下一个 1 节点图；根复制了 main 的体，而 `main` 图还在（依赖表里）。 |
| `p11/p12` | `sync` 块 | 控制边是 `println1 -> pure3(FUNC)`、`println(a) -> println(b)`、`println(b) -> add`、`add -> GATE`；到 `println(a)` 的边被**省略**，因为它的数据输入已经是 `pure3`（`shouldAddSyncCtrlLink`）；嵌套 `sync { }` 摊平成一条链，块边界丢失，但边集可由一个块复现。 |
| `p13`（跨模块捕获） | 模块级 `let` | `main` 捕获得到端口名 `writer::wrote`；根有 `DATA 41`、`Function(main)`、`FILL`、`CALL`；入口闭包链与模块值是分开的。 |
| `p14_types` | 命名类型 | 签名打印 `(p: Point)`、`Named` 解析成 `Point`；**类型声明本身不在 dump 里**（没有 `type Point = ...`）。 |
| `p17_forms` | `if`/`match`/`??`/`&&` | `if c then 1 else 2` 的 `BRCH` **没有** with 输入；`match c { case true … }` 的 `BRCH` 有 `true`/`false` 两个 with 输入，节点集不同；`x ?? 0` 与 `match x { case null … }` 完全相同；`a && b` 与 `if a then b else false` 同形。 |
| `p18/p19` | 默认臂的位置 | 可达性过滤后默认臂**总在最后**（中间的 `case _` 之后的分支被丢掉）；臂序号必须靠 `GCRegion.index`，不能靠模式输入的顺序。 |
| `p20` | 枚举 `match` | 不是“模式列表”，是决策树：`:enum/tag(o) == 0`、`:enum/field(o, i)`、内层再 `:enum/tag == 1`；变体名要从类型声明取。 |
| `p21` | `#strict` | 节点属性 `{"strict": true}`；声明在函数上时函数体所有节点都有，声明在块上时块内节点有。 |
| `p22` | 类型/字段标签 | `shape`/`width` 在 dump 里完全不出现。 |
| `p23` | 没有 `main` 的顶层语句 | 顶层只允许声明，`println(...)` 直接是语法错误；所以“把入口体摊到顶层”不是可行方案。 |
| `p24` | 字面量的上下文类型 | `func f(): i32 { return 3 }` 直接得到 `i32` 的 `DATA 3`（没有 CAST）；`let x: f32 = 3.0` 同理。类型可由 `let`/参数/返回值/算子签名给定。 |

### 1.2 §4.8 的七类信息，逐项对照

| §4.8 要求 | 图里有吗 | 现状 | 结论 |
|---|---|---|---|
| 命名类型的声明与模块归属 | **部分**：`StructDecl`/`EnumDecl` 有名字、模块、字段/变体；没有“声明列表” | `jgir` 只打印签名字符串；`toString()` 会用 `Point`、`Option<int>` | 打印机可以从类型对象重建**具体**声明；泛型参数与类型级写法丢失 |
| 函数签名与参数名 | **有**：`FunctionType`，参数名参与驻留键 | `jgir` 打印签名 | 可直接打印 |
| `let` 名字 | **没有**：`GCNodeChunk` 只有 origins/regions/permissions/attrs（`graph.h#GCNodeChunk`），草稿也没有名字列 | `jgir` 无字段 | §4.7 计划中的列；**不影响等价**（名字不计），只影响可读性与调试 |
| 控制依赖 | **有**：控制边 + `shouldAddSyncCtrlLink` 的省略规则 | `jgir` 打印 `control` 边 | 可复原顺序，但需要算法（见 5.7） |
| 声明标签与节点标签 | **节点标签有**（`nodeAttrs`）；**声明标签没有** | `jgir` 打印节点 `attrs`；函数/类型/字段/参数标签无处可存 | 节点标签可打印；声明标签是缺口（5.3/问题 2） |
| 常量 | **有**：静态区；`printSlot` 只给显示串 | `jgir` 的 `label` 是显示串，字符串不转义 | 打印机要自己的字面量书写器；大常量缺口（5.10） |
| 大常量引用旁边的数据文件 | **没有**源码形式 | 2.5 要求；语言还没有 `data(...)` 一类的写法 | 缺口，先按阈值拒绝（5.10） |

### 1.3 图里有、dump 里没打印的（修 dump，不动模型）

这些不是设计约束，是 `std::jgir` 的字段缺口；S0 补上：

1. 区域 `index`（臂序号；`GCRegion.index` 已有，`gir.cpp#index` 没打）。
2. 区域形参：`regionPorts` + `GCRegion.params`（循环的 `x`、`acc` 名字与类型；`portInfo` 对区域端口给不出名字，见 `p1` 的 `PORT` 全为空名）。
3. 区域 `attrs`（`GCRegion.attrs` 已有；`Block` 标签要用）。
4. `Inline` 区域的 `callSite`（`gir.cpp` 只打 `inlined` 名字）。
5. 图 `flags`（`kGraphFlagMacro` 等；`graph.h#flags`）。
6. 循环的 `collectionCount` 与 `loopKind`（`jgir` 有 kind，没有 collectionCount）。
7. 静态值的**结构化**表示（类型 + 值），而不是 `label` 字符串。
8. 类型表（`types`），让检查器能比较结构而不是拼写。
9. `captureNames` 的限定名（当前通过 `portInfo` 打到端口 `name`，够了；显式列表更稳）。

### 1.4 图本身没有的（2.5 的“语言缺口”，不是拒绝的借口）

1. **`let` 名字**：§4.7 计划，交接 H2/H3b 合流。打印机可以生成名字，因此不阻塞；但调试器/诊断要用。
2. **声明标签**：函数头、类型声明、字段、参数各一套（§4.7、6.2）。今天 `#kernel` 在函数上、`#shape`/`#width` 在类型/字段上，冻结后**完全丢失**。这是往返的硬缺口，必须补存储。
3. **`Block` 区域**：`GCRegionKind::Block` 与 `region.attrs` 都已存在（`graph.h#GCRegionKind`），但前端只在测试遍 `src/passes/test/regions.cpp#Block` 建块；真实的 `#tag { }` 不建块，作用域只能从节点集合猜。
4. **泛型声明**：模板不是图（1.6），只有实例；`<int>` 只留在图名里，而图名不计。2.5 说“泛型实例写成具体类型的函数”，所以这是**允许的损失**，不是缺口。
5. **循环收集槽的源码形式**：`loop.h` 说收集槽属于 H4d 设计，源码还没有 `loop ... collect ...` 一类的写法；图里有 `collectionCount`。这是语言缺口。
6. **大常量的数据文件形式**：2.5 要求“放在旁边的数据文件中由源码引用”，语言还没有这个引用写法。
7. **字面量拼写**：值在、拼写不在；2.5 只要求值，打印机自己负责最短可回读的写法。

### 1.5 表面形式的选择（图不保留，但可规范化）

- `if` 与 `match` 在 bool 上**不是同一张图**（`p17`）：`match` 有模式 with 输入，`if` 没有。打印机按“有模式输入 → `match`，无 → `if`”区分，测量足以复原两者。
- `??` 与 `match null` 同图（`p17`）；`&&` 与 `if … else false` 同图。打印机选 `??`/`if` 作为规范形式；重编译得到同一张图。
- `case 1 | 2` 与两个 `case` 同图（`p1`，4 臂）；打印机逐臂打印。
- `match` 默认臂总在最后（可达性过滤后，`p18/p19`），所以“前 k 个模式对应前 k 臂、最后一臂是默认”是安全的。

## 2. “等价”的定义与检查

### 2.1 单位

比较的单位是**一个 `Program`**（根 + 从根可达的图 + 引用表 + 静态区），不是一张图的字符串。`std::cml` 打印整个程序为一个模块；库模块（没有 `main`）也打印。

### 2.2 `canon(P)`

1. **可达**：从根图出发，沿 `Program::traceGraphs`、`FUNC` 的被调图、`DATA` 的函数常量、规则引用、静态引用走到闭包；丢掉不可达的图。每张图只保留后端会执行的活节点（`execPlan().topo` 的口径），丢掉墓碑与 `Dead` 区域。
2. **抹掉标注区域**：`Inline`、`Block` 是记录，不是控制。把节点归到最近的非标注祖先区域，丢掉这些区域记录。这与 2.5“被内联的部分不必还原”一致：打印出来的代码是摊平的，重编译不再有内联记录。
3. **入口归一**：
   - 根有 `main` 调用：去掉这个调用（以及只为它服务的 `FILL`/函数常量/GATE），`main` 图就是入口图。
   - 根有 `inline` 区域且 `inlineOf` 是 `main`（`p9`）：优先把区域的节点、边、子区域、形参、结果提升成一张虚拟 `main` 图，再从根去掉它。若区域不完整（有边穿到区域外、形参对不上），**不提升**：此时结构比较覆盖除入口外的所有图，入口只做执行比较（可选地，对打印出的源码重跑 `std::opt::inline`，再比较根里的内联区域），报告写 `entry=execution` 而不是静默通过。这是 2.5“被内联的部分不必还原”的边界。
   - 根里的其余活节点就是模块级值/效应；它们与入口分开比较。
4. **名字与位置全部去掉**：图名、节点/端口名、类型声明名、标签键名、`let` 名、origin/span、图/节点编号。函数参数名也不计（它不参与类型兼容），但打印时保留。
5. **类型**：每个命名声明（`StructDecl`/`EnumDecl`）是一个**原子**；比较时在原子之间找双射，保持“不同声明必须不同像”。字段顺序、字段名、变体顺序、变体名、形状（unit/tuple/struct）都算；递归类型用声明原子的编号表示回边。两个字段完全相同的命名类型**不能**合并（名义类型）。泛型实例 `Option<int>` 与 `Option<string>` 是两个原子，字段类型不同，双射自然分开。
6. **节点**：`kind`；输入边按（种类 normal/context/control、目标槽位）排序后的序列（`sourceIndex` 只表示同一输出被用了几次，不计）；`GCOperBody` 的算子身份（不是 URI 字符串，按 `OperatorSemantics` 身份）、`FILL`/`ACCS`/`BRCH`/`JOIN`/`CALL`/`LOOP`/`CAST`/`COPY` 的负载；端口角色（norm/with/capture/region）与序号；`FUNC` 的被调图颜色；静态值按值（数值按位模式、字符串按字节、数组逐元素、函数常量按图颜色、`Rule` 按字段）。
7. **节点属性**：排序后的（标签声明原子，值）集合；标签声明也走双射（名字不计，但同一标签的所有用法必须指向同一像）。
8. **区域**：种类、父区域（相对路径）、所有者节点、`index`、形参（类型与顺序）、结果、`attrs`。
9. **图**：把每个图表示成上述局部签名 + 被调图颜色，做颜色细化；颜色相同的图再逐节点匹配。入口图单独标记；根只比较模块级部分。

两张图等价，当且仅当存在一个保结构、保类型/标签原子的双射，使上述各项逐项相等。**文本相等不是定义**；它只是当打印机是确定性的且两个图同构时会成立的一个必要（非充分）信号。

### 2.3 检查器的实现

1. `std::jgir canonical=true`（S0 新增参数）输出 `canonical` 文档：无 `origin`/`source`/`name`/`label` 拼写；节点用确定性的局部编号；区域带 `index/ports/params/attrs`；图带 `flags`；文档带 `types` 表与结构化 `value`。这是**规范化输入**，不是完整的规范形（同构但不同编号的文档仍可能不同）。
2. `test/tools/cml_roundtrip.py` 读两份文档，做颜色细化 + 对未定颜色类做个体化回溯（节点预算，例如每图 2000 个节点/20 秒）；输出第一处差异的路径。超过预算的图只做执行比较，并在报告里写 `structural=skipped`，不静默通过。
3. 第二遍往返的**文本**相等（`comments=false`）作为确定性检查：`print(compile(print(P))) == print(P)`。它抓的是命名/排序的不稳定，不是等价证明。
4. 测试入口是一个新的 `test/plans/feat/trans/cml.plan.toml`（`kind="behavior"`, `tool="python"`, 参照 `jgir.plan.toml` 与 `jgir_contract.py`），驱动 `camel` 三次：`P`、`P std::cml`、`S`。
5. **名字**不参与图等价（2.2 明确抹掉名字），所以 `let` 名字的保留要单独证明：`std::jgir` v1 的每个图节点名（源图）必须出现在重编译图和打印文本中；重编译多出来的名字只能是打印器生成的 `cmlN`/`pN`。这项检查不能替代往返检查，往返检查也不能替代它。
6. **S1 的验收固定为往返性质**：S1 语料里的每个程序都必须让 `canon(P)` 与 `canon(compile(P std::cml))` 通过 2.3 的检查器；打印文本再打印一次必须逐字节相同（2.3 第 3 条）。只检查“打印结果看起来可读”不算通过。

### 2.4 诊断

打印出的源码**必须零错误**；警告按代码多重集与原文比较：打印出的源码引入新警告是失败，原程序有而打印后消失的警告允许（图里不保留“源码本来会警告”的事实，例如原始源码里不可达的 `case` 在打印出的图里已经不存在）。`std::cml` 自己不声明 `readsTags=true`（那会在无关的运行里改变 `TagHasNoReader` 的判定）；它拒绝时用 `SemanticDiag::CmlUnsupportedConstruct` 在节点/图的 origin 上报，大常量用 `SemanticDiag::CmlBigConstant` 并给出字节数与 `max_literal_bytes`。

### 2.5 执行

对有 `main` 的程序，`camel P std::fvm` 与 `camel S std::fvm` 的 stdout 字节、退出码、故障文本（去掉路径）相等；再跑一遍 `std::nvm` 作为调度差异的交叉检查。跳过依赖时间/随机/文件/环境或并行不确定性的用例，跳过库模块。故障用例比较退出码与故障类别，不比较栈/地址。

### 2.6 为什么需要 S0

没有 `canonical` 的 `jgir`，检查器只能比较文本或 `outputType` 拼写；后者会把 `Option<int>` 与打印出的 `Option_int` 当成不同，也会漏掉区域形参、臂序号与类型结构。文本比较无法证明“图等价”，正是任务里点名的“text-only round-trip test is not a proof”。

## 3. 遍的位置

### 3.1 名字、基类、参数、注册

- 名字：`std::cml`。头文件 `include/camel/passes/trans/cml/cml.h`，源文件 `src/passes/trans/cml/`（`cml.cpp`、`module_writer.cpp`、`expr_writer.cpp`、`type_writer.cpp`、`literal.cpp`、`operator_syntax.cpp`），与 `src/passes/trans/json/` 同构。
- 类：`CmlDumpPass : public RuntimeGraphTranslatePass`（运行时图，翻译遍）。`apply(const Program *, std::ostream &)` 写源码，返回 `nullptr`（消费程序），与 `JsonGirDumpPass` 一致。
- `acceptsWritings()` 保持 `false`：驱动在消费型遍之前会 `extractWritings`（`src/execute/pass.cpp#extractWritings`），所以遍看到的是可执行状态；带 `kGraphFlagHasWritings` 的图不会到达这里。若手工绕过驱动，遍拒绝。
- 参数（`PassDescriptor`，`include/camel/execute/pass/params.h#PassDescriptor`）：
  - `out`（String，默认 `""`）：写文件；空 = 写 `os`（stdout）。写文件时 `data_dir` 默认取 `out` 的父目录。
  - `data_dir`（String，默认 `""`）：大常量的 sidecar 目录；为空且需要 sidecar 时拒绝。
  - `comments`（Bool，默认 `true`）：是否写“由哪个流水线打印”的注释；注释不影响图，测试用 `false` 去掉不确定性。
  - `max_literal_bytes`（Int，默认 `65536`）：超过就写 sidecar/拒绝（见 5.10）。
- 注册：`src/execute/pass.cpp` 的 `trans` 作用域，`{"cml", def(cmlDescriptor())}`，紧挨 `jgir`（当前注册表在 739-755 行）。不加别名；`std::cml` 就是正式名。
- 拒绝：`RuntimeDiag::TranslationRefused`，与 `std::cpp` 的 `refuse()` 同型（节点/图/入口的 origin），进程非零退出；测试记录拒绝类别。

### 3.2 与 `std::jgir` / `std::cpp` 的分工

- 与 `std::jgir` 共享：`src/passes/trans/gir_metadata.cpp` 的 `recordType`、`portInfo`、`nodeStaticSlotType`、`recordLabel`（只用于注释/调试，不用于生成源码）；`Program` 的遍历与 `calleeOf`；`ControlRegions`/`LoopView`；`OperatorTable` 与 `tags.h`。**不共享**格式化：DOT/JSON 是数据转储，CML 是有作用域、优先级、命名与顺序的程序。
- 与 `std::cpp` 共享**算法形状**：`ProgramEmitter::findEntry`（入口：返回值/退出值/输出值里找 `CALL`，再穿过 `FILL` 找函数常量，`program_emitter.cpp#std::cpp`）、`collectFunctions`（函数常量 + `FUNC` 的被调图，232-259 行）、`FunctionEmitter` 的序列/分支/循环/变量/高阶算子的分解（`function_emitter.cpp#FunctionEmitter`、`980-1105`、`1106-1190`、`1571-1660`、`1723+`）。第一版**照抄分解逻辑**，不改 `std::cpp`；等两边都稳定后，再把“读图得到结构”的部分抽成共享的 `ProgramIndex`/`StructuredBody`，避免为了复用而先动导出器。
- 与 `std::cpp` 不共享输出：C++ 有语句序列和显式类型，Camel 需要 `sync`、`loop`、`match`、标签块、`var`、函数值语法。

### 3.3 为什么不是“又一种 GIR”

GIR 是**无损数据转储**：它能打印语言写不出的图（两张写法、收集槽循环、没有源码形式的内部算子），因为它的读者是工具，不是编译器。源码是**程序**：必须有名字、作用域、顺序、可解析的表达式，并且对写不出的结构**拒绝**。如果给 GIR 加一个 `source` 字段，那是让图存它没有的文本（原始源码），既违反“图即程序”，也在改写后失效。`std::cml` 是从图**构造**源码，不是把源码存回去。

### 3.4 输出与 sidecar

`out` 为空时写 stdout，不能写 sidecar（大常量拒绝）。`out` 非空时，模块写 `out`，sidecar 写 `data_dir`（默认 `out` 的父目录），源码里用语言将来提供的数据文件引用形式；在该形式落地前，超过 `max_literal_bytes` 的常量拒绝并报缺口（5.10）。

## 4. 打印算法

### 4.1 模块与入口

1. 找入口：`findEntry(root)`。找到 `main` 就把根里只为入口调用服务的 `CALL`/`FILL`/函数常量/GATE 丢掉，打印 `func main`；根的其余活节点是模块级 `let`（或顶层效应，若语言允许——当前不允许顶层语句，所以只有 `let`）。
2. `main` 被内联进根（`p9`）：把 `inline` 区域当 `main` 的体打印（`func main`），根里其余节点是模块级值；不打印那个 `inline` 区域本身。
3. 没有 `main`（库模块）：打印所有声明与顶层 `let`，不打印入口。
4. 顶层 `let` 按依赖顺序打印；捕获名（`module::name`）清洗成合法标识符（`module__name`），函数体里用这个名字，重编译时自然形成捕获。名字全部加唯一后缀避免碰撞。
5. 模块名：`out` 的文件名主干，或 `main`；捕获名已清洗，不依赖模块名。

### 4.2 声明：类型、标签、规则

- **类型**：从所有图/签名的 `Type*` 收集可达的命名声明（`StructDecl`/`EnumDecl`）。具体实例声明成具体类型：`type Option_int = enum { Some(int), None }`、`type Point = { x: int, y: int }`；打印机的类型书写器把 `Option<int>` 映射到 `Option_int`。字段名/变体名/顺序保留；泛型参数名丢掉。匿名 struct/tuple/array/function/optional 直接写结构。类型声明按依赖拓扑排序（递归通过数组/枚举是允许的，因为语言只允许这些环）。
- **标签**：收集所有节点 `attrs` 的键，生成声明（`tag kernel on nodes`、`tag width(w: int) on nodes`；标准标签 `strict` 不声明）。打印事实：
  - 有 `Block` 区域时：按区域的 `attrs` 打印 `#[...] { ... }`，块内节点保留自己的标签（见 5.3）。
  - 没有 `Block` 区域、但一个语句的整棵表达式树的节点集合恰好等于某个标签集：在语句外打印块。
  - 否则拒绝（`CmlTagScope`），并把它记为缺口（5.3）。
- **规则**：识别 `rewrite:rule` / `rewrite:rule_when` 的构造图（7/8 节点、`let name = ...` 的初值、名字字符串、单向/双向、掩码、lhs/rhs/when 函数值），打印 `rule name(...) { lhs => rhs }` / `<=>` / `when ...`；`#requires(...)` 从掩码的位反推标准标签名（`3 -> reassociate, contract`；组名丢失）。构造图不是这个形状（例如被折叠成 `Rule` 常量）则拒绝。
- **`#macro`**：图 `flags` 的 `kGraphFlagMacro` 或函数类型的 `macro` 修饰；打印在函数声明前。
- **`#vjp(rule)`**：图的规则引用（`GCGraphRefRole::Rule`）打印成标准标签 `#vjp(rule)`；规则名从被引图取。
- **`#requires`** 只在规则上；其他声明标签（`#shape`、`#width`、函数上的 `#kernel`）等 §4.7 的声明标签存储落地后再打印（5.3、问题 2）。

### 4.3 函数体：表达式、`let`、`sync`、`var`

1. 用 `execPlan().topo` 作为顺序，跳过 GATE/SYNC 一类只表达顺序的节点；GATE 只用于找结果（`resultOf`）。
2. 每个节点按种类降级：
   - `DATA` → 字面量/函数常量；`PORT` → 参数/区域形参/捕获名；`OPER` → 算子语法表；`CAST` → `expr as T`；`COPY` → 直接写输入表达式（不引入节点）；`FILL` → struct/tuple/数组/闭包字面量；`ACCS` → `expr[i]`/`expr.field`/`expr.0`/枚举字段；`FUNC` → `callee(args)`；`CALL` → `f(args)`；`BRCH/JOIN` → 分支；`LOOP` → 循环。
   - 算子语法表（`operator_syntax.cpp`）按 `OperatorSemantics` 身份分类：中缀/前缀/赋值/索引/字段/调用/类型转换/内建语句；带优先级与结合性。第一版**全括号**，不做最小括号；括号不改变图。
   - 没有源码形式的内部算子（`:op/tag_is_null`、`:enum/tag`、`:enum/field`、`:var/*`、`:struct/append_arr_in_place`、`:op/assn_owned` 等）不由表直接打印，而是由产生它们的构造打印（`match`/`var`/`+=`/`append`）；单独出现则拒绝。
3. **`let`**：一个值有多个使用者、或它是效应语句的结果、或它是 `var` 的初值时，打印 `let name: T = expr`；纯单使用值直接内联。`let` 不产生节点，所以打印/重编译不改变图；名字由结构键（算子身份 + 输入颜色 + 类型）生成，第二次往返稳定（5.2）。类型标注只在需要给字面量定类型时写（`p24`），或为了可读性统一写；不制造 CAST。
4. **`sync`**：函数类型带 `sync` 修饰 → 函数头写 `sync`，体内不再套块；否则按控制边的极大连通分量套 `sync { ... }`。效应顺序见 5.7。
5. **`var`**：`:var/new` 打印 `var x: T = init`；`:var/get`/`:var/peek` 打印 `x`；赋值/复合赋值打印 `x = v` / `x += v`；`cellEscapes` 的 cell 仍用 `var`（Camel 的 `var` 本身就是可逃逸的引用），不需要额外包装。名字生成；作用域取所有使用的共同支配点（由控制边计算）；计算不出唯一支配点则拒绝。

### 4.4 控制：`if`/`match`、循环

- `BRCH` + `JOIN`，臂是 `Arm` 区域，`region.index` 是臂号。
  - `BRCH` 没有 with（模式）输入、恰好 2 臂 → `if cond then <arm0> else <arm1>`；臂结果可能是区域外的节点（`p1` 的空臂），直接打印那个表达式。
  - `BRCH` 有 with 输入 → `match subject { case <pattern0> => <arm0> … }`；模式是静态值（bool/int/string）时直接写；默认臂（`with` 数量 = 臂数 − 1，且最后一臂没有模式）写 `case _`；模式与臂的对应按 `index`，不按输入顺序（5.8）。
  - `:op/tag_is_null` + `:op/tag_null_value` 的形状 → `match x { case null => …; case v => … }`；`??` 用规范形式。
- 循环：按 3.10 的**区域模型**打印：条件与体是同一张图的两个控制区域，共帧，没有单独的条件/体图，也没有捕获表。`p1` 里 `loop.condition/body` 为 `null`、`conditionRegion/bodyRegion` 指向本图的 `g1r1/g1r2`，就是证据。`LoopView` 读 `loopKindOf`、状态数、可迭代输入、条件/体区域、`collectionCount`。
  - `For`：`loop s0 = init, … for x in xs do <body>`；`While`：`loop s0 = init, … while <cond> do <body>`；`ForWhile`：两者都有。
  - 状态名用区域 `params` 的名字，没有就 `s0…`；元素名用体区域第一个形参的名字，没有就 `x`。
  - 体结果是单值 → 直接写；k 个状态 → 写 `(s0, …, sk-1)`；嵌套的分支/循环递归。
  - `collectionCount > 0`：拒绝（`CmlLoopCollects`），源码形式未定（5.9）。
  - 循环外层的捕获/共享帧不需要 `with` 参数：区域端口就是形参，体里直接引用外层值（3.10 的共帧）。

### 4.5 闭包与函数值

- `DATA Function(graph=g)` 无闭包：如果 `g` 是命名图，写 `g`；否则写 lambda（见下）。
- `FILL`（`FunctionClosure`）包着一个函数常量：在该使用点写 lambda `(params) => <body>`，捕获作为自由变量（从 `capturePorts` 与 FILL 的输入映射）。
- 同一个 lambda 图被多个 FILL 使用：源码写不出“一个体、多个闭包实例”而不复制图；拒绝（`CmlSharedClosure`），记为缺口。
- 函数值进数组/结构/作为参数：同样按名字或 lambda 打印；类型是函数类型。
- 高阶算子（`->map<f>`、`->filter<f>` 等）：按 `OperatorTable::elementCalls` 的形状打印方法调用；函数值按上面的规则。

### 4.6 命名与顺序

- 每个图一个作用域：参数名来自 `FunctionType`，区域形参来自 `GCRegion.params`，其余值按需生成；生成名用结构键（`op:add_l@<color>,<color>` 的短哈希/序号），不用 `NodeId`（节点编号在改写后不稳定）。冲突加 `_2`。
- 声明顺序：类型按依赖；标签按名字排序；函数按名字排序；顶层 `let` 按依赖。函数体按 `execPlan().topo`，分支/循环内部同样。
- 确定性：同样的图两次打印字节相同（`comments=false`）；这是第二遍往返测试的内容。

## 5. 难例与决定

### 5.1 被优化掉的区域

**决定**：打印现图。折掉的 `if true` 就是直行代码；死臂消失；空臂打印 `then <区域结果表达式>`（结果可以在区域外，`p1` 的 `g2n0`）。如果区域还在但 `result` 无效、或区域内的节点已被移到区域外导致边穿出，拒绝（`CmlBrokenRegion`），不猜。
**影响**：不恢复源码顺序/源码分支；`canon` 按现图比较，所以这是正确的。优化器的“折叠”不需要逆向。

### 5.2 没有名字的值

**决定**：只为多使用/效应/`var` 初值生成 `let`，名字由结构键派生；单使用的纯值内联。名字不参与等价，所以第二次往返只要结构键稳定就稳定（不用 `NodeId`）。不生成未使用的 `let`（避免警告）。
**影响**：输出名字与源码不同；调试器看到的行号不是原始行号（本来也不保证）。若一个值必须跨控制区域使用而找不到唯一支配点，拒绝而不是错误提升（提升会改变求值顺序）。

### 5.3 标签的作用域是区域

**现状**：标签在节点上；`#tag { }` 不建 `Block` 区域（`p2`、`p22`）；函数/类型/字段/参数的声明标签根本没有存储（`decl.cml` 的 dump 里没有）。
**决定**：
1. 前端为每个 `#tag { }`（以及 `@` 装饰块，如果它也是词法块）建一个 `Block` 区域，`region.attrs` 放块自己的标签集，节点仍保留累积后的标签。这是 `GCRegion.attrs` 的现成用途，模型不需要新字段。
2. 打印机按 `Block` 区域打印 `#[...] { ... }`；区域 `attrs` 是块自己的标签，节点 `attrs` 是累积后的集合，打印块标签即可复现两者。
3. 没有 `Block` 区域的图（今天的冻结图、或测试遍造的块）：如果一个语句的表达式树节点集恰好是某个标签集的闭包，就在语句外打印块；否则拒绝（`CmlTagScope`），并把它记为“等前端建块后消失”的缺口。
4. 声明标签：函数头用图级标签集（§4.7 的第一项；今天还没有这个字段）；类型/字段/参数需要声明标签存储（问题 2）。在它落地前，`#shape`/`#width`/函数上的 `#kernel` 无法打印；往返测试的图比较**看不到**它们（图里没有），所以必须单独用“声明事实表”检查，不能因为图相同就宣布通过。
**影响**：`Block` 区域本身是标注区域，`canon` 比较时抹掉；它承载的作用域只通过节点标签体现，所以往返严格比较的是“节点标签”，不是“块边界”。标签事实保留在节点上，比较仍然严格。不引入表达式级标签语法。

### 5.4 泛型实例

**决定**：打印具体函数/具体类型（`id_int`、`Option_int`），不打印泛型声明，也不打印对泛型的调用（模板不是图）。名字清洗，冲突加后缀；类型参数丢失。
**影响**：2.5 明确允许（“泛型实例写成具体类型的函数”）。若两个实例清洗后同名，后缀区分；若一个函数值引用 `id<int>`，打印 `id_int` 这个函数名。调用图（`FUNC` 的 callee）按图颜色对应，不按名字。

### 5.5 函数值与共享闭包

**决定**：命名图 → 名字；匿名图 + 单个 FILL → 该点的 lambda；同一个 lambda 图被多个 FILL 使用 → 拒绝（`CmlSharedClosure`）。捕获从 `capturePorts`/FILL 的输入映射成自由变量。
**影响**：共享闭包在源码里只能复制体（多一张图）或提成带显式参数的函数（类型/图都变），两者都不等价；拒绝是诚实的。若以后语言有“共享体”的写法，再补。

### 5.6 模块级捕获与扁平化

**决定**：一个文件、一个模块；所有可达的图/类型都摊平成局部声明，不打印 `import`（外部库函数/类型也局部重声明）。模块级值打印成顶层 `let`，捕获名清洗成 `module__name`；函数体里的捕获变成自由名字，重编译时自动形成捕获。跨模块的常量在根里有节点（`p13` 的 `DATA 41`），直接打印。
**影响**：模块身份、`import` 结构、限定名丢失（名字不计）；输出可能变大（把库函数体也打印出来）。若某个外部图不可达/不可打印（运行期注册、无模块），拒绝。若作者要保留模块边界，再做“每模块一个文件 + import”的第二版（问题 5）。

### 5.7 效应顺序与 `sync`

**决定**：按控制边把每个区域内的效应节点串成链；构造 `sync` 块时按链顺序打印语句。构造器的规则是：若前驱已经是目标的直接数据输入，就不再建控制边。打印机按同一规则预测重编译的边集；若预测的边集与原图不一致，先尝试相邻交换/把被省略的节点插回链中；仍不一致则拒绝（`CmlEffectOrder`）。
**测量支撑**：`p11` 的 `println(a)` 边被省略；`p12` 的嵌套块摊平成一条链。一个块可以复现同样的边集，所以块边界不是必须还原的。
**影响**：顺序是语义（3.1/3.3），不能靠“反正文本一样”糊过去；这一块需要独立测试（边集逐条比较，不只是执行）。

### 5.8 枚举 `match` 的决策树

**现状**：`p20` 的 `unwrapOr` 是 `:enum/tag(o) == 0` → 臂 0 里 `:enum/field(o, 0)` + 内层 `:enum/tag == 1` 的树，不是模式列表；变体名只在类型声明里。
**决定**：识别“同一 subject 上的 `:enum/tag == k` 链 + 臂内 `:enum/field(subject, i)`”并映射到 `match subject { case Type::Variant(fields) => … }`；变体顺序/字段名从 `EnumType` 取；穷尽性由树的默认臂判断。不是这个形状（被改写合并、subject 被换掉）→ 拒绝（`CmlEnumMatch`）。
**影响**：源形状的枚举匹配能往返；优化后的不规则决策树可能打印不出，记为缺口/测试白名单，不假装成功。

### 5.9 循环的收集槽

**决定**：`LoopView::collectionCount > 0` 拒绝（`CmlLoopCollects`）。源码没有对应写法（H4d 设计未落地）；打印成“丢弃收集槽”的状态循环会改变图与语义，不允许。
**影响**：使用收集槽的用例（若已有）无法往返，直到语言补形式；这是 2.5 意义上的语言缺口，不是打印机的选择。

### 5.10 大常量

**决定**：常量先按大小分类；超过 `max_literal_bytes`（默认 64 KiB）时，若 `out`/`data_dir` 给定且语言有数据文件引用形式，就写 sidecar 并引用；当前语言没有该形式，所以**拒绝**（`CmlBigConstant`），并在报告里列出大小与类型。阈值以下按字面量打印（数组嵌套、字符串转义、浮点用能回读的最短表示）。
**影响**：大数组/大字符串的用例暂时不能往返；内联一个 100 MB 字面量既不可读也不是 2.5 的本意。等语言补 `data(...)` 一类形式后，这条限制消失。

### 5.11 写法表（两张写法）

**决定**：`std::cml` 是翻译遍，驱动在它之前抽取一种写法（`src/execute/pass.cpp#std::cml`），所以它打印的是抽取后的可执行图；写法表本身不可打印（源码没有“一个值两种写法”的语法）。若手工把带 `kGraphFlagHasWritings` 的图送进来，拒绝（`CmlHasWritings`）。
**影响**：2.5 的“任何合法的图”按“可执行状态的图”理解；表是遍之间的内部表示，不是程序。需要作者确认（问题 7）。

### 5.12 没有 `main` / 库模块

**决定**：没有入口就打印声明与顶层 `let`，不打印 `main`；执行检查跳过。`std::cml` 是翻译遍，不要求入口（与 `firstConsumingPass` 的注释一致）。
**影响**：库模块可以往返；若顶层 `let` 有副作用且没有被使用，它本来就不执行（3.1），打印成 `let` 也不会执行。

## 6. 步骤与独立落地

每一步都是一个可独立提交、可独立运行测试的增量；遍从 S1 起就注册，对不支持的构造精确拒绝，后续步骤只是减少拒绝类别。

### S0：判定器（不写 `std::cml`）

- 产物：`std::jgir canonical=true`（区域 `index/ports/params/attrs`、图 `flags`、类型表、结构化 `value`、去名字/位置）；`test/tools/cml_roundtrip.py`（颜色细化 + 回溯 + 差异路径）；`test/plans/feat/trans/cml.plan.toml` 的第一个测试：对现有用例跑 `canon(P) == canon(P)`，以及一对已知不同的图必须不等。
- 测试：`jgir_contract.py` 的字段检查扩展；自身相等；不同图必须报差异；超预算路径。
- 独立理由：没有它，后面只能用文本比较；它也为 `std::jgir` 的读者提供区域/类型信息。

### S1：直行函数

- 产物：`std::cml` 骨架、`module`/入口/顶层 `let`、函数签名、字面量、数组/struct/tuple、ACCS、CAST、COPY、算子语法表（算术/比较/逻辑/转换）、直接调用、返回、函数值（无捕获）、`var`、`sync` 直行。`let`/`var` 的声明名打印源码里的名字（图不保存名字列，名字在 `GCGraphDebugRecord` 的冷数据里）；只有图里本来没有名字的值才生成 `cmlN`。
- 拒绝：区域（分支/循环/内联）、命名类型、泛型、规则、标签（`#strict` 等节点标签留到 S4 与 `Block` 一起处理，先拒绝带标签的图）、`var` 参数、S1 算子表之外的算子、超过 `max_literal_bytes` 的常量。拒绝先于输出：整段打印在内存里完成，失败时流上不留下半个程序。
- 测试：`test/cases/linear` 的直行子集 + 新增 `test/cases/trans/cml/linear.cml`（覆盖每种表达式/字面量/变量/调用）；验收是往返性质而不是“看起来对”：`source → std::cml → source` 重编译后与原文的 canonical v2 文档必须被 `test/tools/cml_roundtrip.py` 判为等价（2.3）。`let` 名字不属于图等价，另用 v1 节点名与打印文本逐图检查：源图的名字必须全部出现在重编译图和打印文本里，新增名字只允许打印器生成的 `cmlN`/`pN`。
- 独立理由：即使只有直行，也能打印一大类程序；“先直行后控制”是诚实的进度。

### S2：分支、同步、闭包

- 产物：`BRCH/JOIN` + `Arm` 区域 → `if`/`match`/`??`；臂序号；空臂/默认臂；控制边 → `sync` 块（`CmlEffectOrder` 的预测/回退）；GATE；lambda/闭包/捕获；函数值作为参数/返回值；高阶算子方法调用。
- 测试：`p17/p18/p19` 的形态、`p11/p12` 的边集逐条比较、`bound_closures.cml`、闭包捕获、嵌套 `if`/`sync`。
- 独立理由：分支/同步/闭包是大多数用例的核心；循环可以之后。

### S3：循环

- 产物：`LoopView` 区域形式 → `for`/`while`/`for-while`；状态/元素名；体结果 tuple；嵌套；循环体里的分支/效应；捕获；`collectionCount > 0` 拒绝。
- 测试：`test/cases/loops` 的形态（`captures.cml`、嵌套、`while`、`for-while`）、故障循环、执行。
- 独立理由：循环依赖 S2 的分支/效应打印，但可以独立落地。

### S4：声明（类型/标签/规则/泛型）

- 产物：命名 struct/enum 声明（具体实例）；枚举 `match` 决策树重建；标签声明 + `Block` 区域（依赖前端建块）+ 节点标签块；`#strict`；`#macro`/`#vjp`；`rule`/`#requires` 构造图识别；泛型实例具体化；声明标签（依赖 §4.7 存储；没有就先拒绝并记账）。
- 测试：`generic_tree.cml`、`decl.cml`、`check_rules.cml`、`check_when.cml`、`requires_group.cml`、`strict_algebra.cml`、`p6_generic`、`use_let_writer`。
- 独立理由：类型/标签/规则是“整程序”的最后一块；S1-S3 可以在没有它们时打印原始类型/无标签的程序。

### S5：整程序与随机程序

- 产物：顶层 `let`/捕获/模块摊平（跨模块）、库模块、大常量 sidecar/拒绝、第二遍文本稳定性、全用例 sweep + 随机程序（progen，宿主机慢层）、执行 `std::fvm`/`std::nvm` 对比；`test/plan-selection.json` 规则（`src/passes/trans/cml/` → `feat/trans/cml`）。
- 测试：`test/cases/**` 全扫，按拒绝类别记账（每类一个 allowlist，新增类别失败）；`test/cases/trans/library_module.cml`；`p13` 跨模块；第二轮 `print(compile(print(P))) == print(P)`；随机程序（宿主）。
- 独立理由：这是“2.5 全部测试用例与随机程序”的落地；前面的步骤各自已有测试，不靠它才发现问题。

### S6：缺口收尾（可选，单独排期）

- 表达式级标签（若不建 `Block` 区域）、循环收集槽源码形式、类型/字段/参数声明标签、每模块一文件 + `import`、大常量数据文件形式。每项都需要语言/存储的改动；`std::cml` 的拒绝类别就是它们的清单。

## 7. 作者问题（编号，附我的答案）

1. **`let` 名字列**（§4.7）：现在只在计划里，没有列。`std::cml` 不需要它也能等价（生成名），但调试器/诊断要用。**我的答案**：v1 打印源码名；图不保存名字列，但 `GCGraphDebugRecord` 的冷数据里已有 `nodeNames`，打印器查它，只有本来没有名字的值才生成 `cmlN`。名字不进图等价，所以“名字保留”按 2.3 第 5 条单独检查；等调试器需要源文件/行号时再加列。
2. **声明标签存哪里**：函数头可以放图头的一个 `AttrSetId`；类型/字段/参数是驻留的声明对象，需要一个按声明身份索引的**程序级声明事实表**（或者给 `StructDecl`/`EnumDecl` 加不进驻留键的标签集）。没有它，`#shape`/`#width`/函数上的 `#kernel` 冻结后丢失，往返测试也看不见。**我的答案**：加程序级声明事实表，键为声明身份（类型/字段/参数），函数用图头；这是 §4.7 已经要求的信息，不是新语义。
3. **`Block` 区域**：前端为每个 `#tag { }` 建 `Block` 区域，`region.attrs` 存块标签；打印机按块打印。或者加表达式级标签语法 `#tag expr`？**我的答案**：建 `Block` 区域。`GCRegionKind::Block` 和 `attrs` 都已存在，前端只需建块并填 attrs；表达式级标签会扩大语法和解析器，收益不如前者。
4. **泛型实例**：打印成具体函数/类型（`id_int`、`Option_int`），不打印泛型声明；类型参数与 `<...>` 名字丢失。**我的答案**：可以，2.5 明确说“泛型实例写成具体类型的函数”；名字本来就不计。
5. **模块边界**：v1 一个文件、全部摊平、不打印 `import`；还是每模块一个文件 + `import`？**我的答案**：v1 摊平（图等价、实现简单、不依赖模块解析）；如果作者要可读的模块结构，第二版再做“每模块一文件”，但那是输出组织，不是等价条件。
6. **大常量**：语言还没有数据文件引用形式。先按 `max_literal_bytes` 拒绝，还是全部内联？**我的答案**：拒绝并列出缺口；内联 100 MB 数组不可用，也不是 2.5 的本意。等作者定下 `data(...)`/sidecar 形式再实现。
7. **写法表**：`std::cml` 在驱动抽取之后运行，打印的是抽取后的可执行图；写法表不可打印。2.5 的“任何合法的图”是否按“可执行状态的图”理解？**我的答案**：是；抽取是驱动对消费型遍的既定行为（`pass.cpp#std::cml`），表是内部表示，不是程序。若作者要求打印表，需要新的源码形式，那是语言设计。

## 8. 这个遍给谁用

- **正式消费者只有一个：往返测试**（2.5 要求全部用例与随机程序）。它迫使图模型与源码表面保持一致；每发现一个写不出的结构，就得到一个明确的缺口（§1.4），而不是一个模糊的“以后再支持”。
- **附带的人用**：`camel prog.cml std::opt std::cml > prog.opt.cml` 得到一份可编译、可执行的当前图源码；用现有的 `std::nvm`/`std::fvm`/调试器当普通源码看。不为它新增调试器/编辑器功能，也不保证行号对应原文件。
- **明确不做**：部署格式（用 `std::cpp`/ONNX）、编辑器/LSP 的源文件（编辑器编辑源码，不编辑图）、源码级优化器（不承诺打印出的代码能再跑哪些遍）、注释/格式/原始写法的保留。
- **范围限制**（每条都是已知缺口，不是永久拒绝）：一个模块/一个文件、可执行状态（驱动抽取写法）、入口 `main` 或库模块、无 `import`（摊平）、大常量阈值、无收集槽循环、标签作用域需要 `Block` 区域、声明标签需要 §4.7 存储。

## 9. 风险与不做

1. **S0 的工作量**：类型表 + 结构化值 + 图同构判定器，可能和 S1 一样大。它是“证明”的前提；不做它，往返测试就退化成文本比较（任务明确不接受）。
2. **算子表覆盖**：内置 `:op/*` 约 204 个、`:var/*` 4 个（`src/builtin` 的注册），加上 `:enum/*`、`:struct/*`、`:io/*`。表要按 `OperatorSemantics` 身份而不是 URI 字符串分类；未覆盖的逐一拒绝并计数。第一版可以只覆盖常用子集，但 sweep 报告必须给出“未覆盖 URI 的数量与用例”。
3. **优化后的图**：入口内联的提升、部分内联、枚举决策树、共享闭包是四个可能提升不了的点；提升不了时只做执行比较并在报告里写明 `structural=skipped`，不能静默当通过。
4. **判定器预算**：图同构是指数最坏；用颜色细化 + 回溯 + 预算，超预算的图只做执行比较并报告。
5. **跨任务依赖**：声明标签（§4.7/问题 2）与 `Block` 区域（问题 3）不在 `std::cml` 内，需要图存储第 5 步/前端配合；没有它们，`std::cml` 只能打印节点标签，声明标签是**测试看不见的损失**。这一点必须让作者知道。
6. **不做**：不把源码/文本存进图；不为打印在图上加“源形状”字段；不因为打印不出就改语言语义；不把 `std::cml` 做成部署格式。

## 附录 A：测量命令与探针

```powershell
$env:CAMEL_HOME='D:\Projects\Camel\out\latest'
$env:PATH="$env:CAMEL_HOME\bin;$env:PATH"
camel p1_regions.cml std::jgir
camel p1_regions.cml std::gir_regions
camel p2_tags.cml std::jgir
camel test\cases\compile\tags\decl.cml std::jgir
camel test\cases\modules\rules\check_rules.cml std::jgir
camel test\cases\compile\closure\bound_closures.cml std::jgir
camel p6_generic.cml std::jgir
camel p8_literals.cml std::jgir
camel p11_sync.cml std::jgir
camel p12_syncblock.cml std::jgir
camel p17_forms.cml std::jgir
camel test\cases\compile\enum\generic_tree.cml std::jgir
camel test\cases\compile\tags\strict_algebra.cml std::jgir
camel p9_opt.cml std::opt std::jgir
```

探针源码（节选，完整版在测量目录，未提交）：

```camel
// p1_regions.cml
func pick(c: bool, x: int): int { return if c then x else 0 }
func classify(n: int): int {
    return match n {
        case 0 => 10
        case 1 | 2 => 20
        case _ => 30
    }
}
func main(): int sync {
    let a = loop acc = 0 for x in [1, 2, 3] do acc + x
    let b = loop i = 0, s = 0 while i < 100 do (i + 1, s + i)
    println(pick(true, 7), classify(2), a, b)
    return 0
}
```

```camel
// p17_forms.cml（节选）
func f1(c: bool): int { return if c then 1 else 2 }
func f2(c: bool): int {
    return match c {
        case true => 1
        case false => 2
    }
}
func f3(x: int?): int { return x ?? 0 }
```

```camel
// p20_enum.cml（等价于 test/cases/compile/enum/generic_tree.cml 的 unwrapOr；类型声明从略）
type Option<T> = enum {
    Some(T),
    None,
}

func unwrapOr(o: Option<int>, fallback: int): int {
    return match o {
        case Option::Some(v) => v
        case Option::None => fallback
    }
}
```

## 附录 B：`canonical` JSON 的字段（S0 的目标）

在现有 `camel.gir` v1 上加一个参数，不改变默认输出：

- 文档：`format/version/root`；`graphs`；`types`；`entry`（图颜色或 null）。
- 图：`id`（确定性局部编号）、`flags`、`signature`（结构类型引用，不是字符串）、`nodes/edges/normalPorts/contextPorts/capturePorts/exit`、`regions`。
- 节点：`id/kind/inputs`（边在 `edges` 里）、`outputType`（类型表索引）、`value`（结构化）、`attrs`（排序的 `[tag, value]`）、`parameter`（名字可留，比较不计）、`callee`（图颜色）、`loop`（kind/collectionCount/conditionRegion/bodyRegion/stateCount）。
- 区域：`id/kind/parent/owner/index/result/ports/params`（类型列表，名字可留）/`attrs`/`inlined`（图颜色）/`callSite`。
- 类型表：每个条目 `kind`（primitive/array/tuple/struct/enum/function/optional/named）、`name`（可留，比较不计）、字段/变体/参数/结果/修饰；递归用表索引。
- 静态值：`kind` + 数值位模式/字符串字节/数组元素/函数常量图颜色/`Rule` 字段。
- 不输出：`origin`、`source`、`label` 的拼写（需要显示时另给 `display`，比较不用）、图名、节点名、`mangledName`。

## 附录 C：与 `std::cpp` 的对应表（实现时照抄的分解）

| 图结构 | `std::cpp` 的位置 | `std::cml` 的目标 |
|---|---|---|
| 入口（`CALL`/`FILL`/函数常量） | `program_emitter.cpp#CALL` | `findEntry` + 去掉入口链 |
| 收集函数/规则/常量 | `program_emitter.cpp#collectFunctions` | `collectFunctions` |
| 序列/尾调用/GATE | `function_emitter.cpp#GATE` | 语句序列 + `return` |
| 分支/臂/默认臂 | `function_emitter.cpp#function_emitter` | `if`/`match` + 臂区域 |
| 循环（区域形式） | `function_emitter.cpp#function_emitter` | `loop ... do ...` |
| `var`/复合赋值 | `function_emitter.cpp#function_emitter` | `var` + `=`/`+=` |
| 高阶算子/函数值 | `function_emitter.cpp#function_emitter+` | `->map<f>` 等 |
| 算子语法 | `operator_rules.cpp`（C++ 输出） | 新表 `operator_syntax.cpp`（Camel 输出） |

`std::cpp` 已证明这些分解在现有测试上可行；`std::cml` 复用的是**分解的判断**，不是 C++ 文本。第一版复制到 `src/passes/trans/cml/`，不重构导出器；S5 之后再考虑把“读图得到结构”抽成共享层。