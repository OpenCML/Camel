# Camel 同步控制语义解读

> **先读 [`../design-principles.cn.md`](../design-principles.cn.md)。** 它是语言作者对 Camel 根本定位与执行模型的规范性说明：与实现或其他文档冲突时以它为准；它没有覆盖或标为“未定”的问题，向语言作者提问，不要自行选择。

本文档解释 Camel 的同步控制语义，覆盖两层视角：

- **语法语义层**：`sync` / `wait` 在语言中的约束与直觉语义。
- **编译实现层**：从 AST/GCT 到 GIR 如何落地控制依赖，并最终保证运行时顺序。

## 1. 设计目标

Camel 的同步执行语义要求：

- 返回值必须正确（值语义）。
- 同步副作用（例如 `println`、`seed`、I/O）必须在函数对外完成前完成（时序语义）。

因此，编译器需要同时编码两类依赖：

- **Norm/Data 依赖**：谁产出值、谁消费值。
- **Ctrl 依赖**：谁必须先执行完成，后者才能开始。

## 2. 语法语义层：`sync` 与 `wait`

> **注意（H461）**：函数前缀 `sync func` 与关键字 `wait` 已退役，解析器不再认识它们。2.2、2.3 与第 3 节
> 描述的是退役前的模型，作为实现记录保留；现行写法见 `docs/language/04_execution_model.md`。

本节描述“语言层”定义；后续章节给出对应实现路径。

### 2.1 语法入口（关键产生式）

- `stmtBlock : SYNC? '{' stmtList? '}'`
- `blockStmt : WAIT? stmtBlock`
- `waitExpr : WAIT? assignExpr`
- `funcDecl : ... modifiers? FUNC identRef parentParams (':' typeExpr)? stmtBlock`

`sync` 在函数定义里可以出现在两个位置，它们是**两个不同的东西**（语言设计如此，不是同一修饰符的两种写法）：

- **前缀 `sync func f(...) { ... }` 修饰的是函数**：`sync` 位于 `modifiers` 中，成为函数类型上的
  `Modifier::Sync`。它声明“这个函数有副作用、需要被同步”——调用方可能需要 `wait` 它。它**不会**把函数体
  变成同步块。
- **后缀 `func f(...): T sync { ... }`（以及单独的 `sync { ... }`）修饰的是其后的块**：该块是同步块，
  它自己直接写着的语句按书写顺序执行（等价于编译器在相邻语句之间插入控制边）；写在里面的分支臂、
  循环体与嵌套块不继承这个顺序（2.2 的“不向内继承”）。函数类型上没有 `Modifier::Sync`。

二者可以同时使用：`sync func f(...): T sync { ... }`。

### 2.2 求值规则与 `sync` 块

Camel 的求值是**依赖驱动**的：一个表达式被求值，当且仅当它的结果（经数据边或控制边）被函数出口需要。

- **同步块之外，结果未被使用的效应调用不会执行。** 这是依赖驱动求值的自然结果，是合法的语言规则，
  不是缺陷；编译器在能够检测到时给出告警。
- **同步块**（`sync { ... }`，或以 `sync` 块作为函数体）内，**它自己直接写着的语句**按书写顺序执行；
  编译器在相邻语句之间建立控制边，块尾成为块完成的条件。
- **同步块不向内继承**（设计原则 3.4，形式化 F17）：同步块只管它自己直接写着的语句；写在里面的臂、
  循环体与嵌套块按它们自己的写法执行，要同步就在那里写 `sync`。例：
  `func f(c: bool): int sync { if c then { println(1); println(2) }; return 0 }` 里 `if` 在同步链上，
  它的值是 `println(2)`，所以 `println(2)` 执行、`println(1)` 不执行；要两句都打印，写
  `if c then sync { ... }`。
- `sync` 不改变值语义，只增加顺序约束。

```camel
// 函数修饰符：函数体不是同步块，println 的结果未被使用，因此不执行（设计如此）
sync func f(): int {
    println("hello")
    return 1
}

// 函数体是同步块：println 执行，然后返回 1
sync func g(): int sync {
    println("hello")
    return 1
}
```

### 2.3 引用、`var`、`let` 与 `wait`

- Camel 中一切值**默认按引用传递**。`var` 标记可变对象，`let` 标记不可变对象。
- `var` 形参是真正的引用：被调函数对它的写入就是对调用方变量的写入；在任何调度器、任何遍之后
  （内联、特化、折叠、CSE、DCE）观察到的写入都相同。
- 被 `if` 分支或 lambda 捕获的变量也按引用捕获：在分支或 lambda 内对它的写入作用于外层变量。
- 把 `let` 绑定的（不可变）对象传给 `var`（可变）形参是**编译期错误**。
- 同一个变量传给两个 `var` 形参是别名：两个形参指同一个对象。
- `let` 的不可变性是**编译期校验**，不是对内存加锁：修改变量的操作本身是 `sync` 的，且必须声明哪个形参
  是 `var`；`let` 变量不能传给 `var` 形参，因而无从修改。
- **`wait`** 作用于它后面的那**一个表达式**：`wait` 前后的操作如果用到了该表达式的值，或用到了被它修改的
  值（即用到了它的副作用），就与它局部有序。例：`A(i); wait modify(i); B(i)`，编译器自动插入
  `A → modify → B` 的控制依赖。这正是“默认按引用”这一设计的原因。
- **`wait` 不是机械的排序，它依靠 `var` 的使用情况来排序。** 被排在修改之前的，是此前**直接使用**了该
  变量的值的那些节点。`A(i + 1); wait modify(i)` 中直接使用 `i` 的是 `+`，所以 `modify` 排在 `+` 之后，
  而不保证排在 `A` 之后——这是正确的行为，不是缺陷（测试 `wait_direct_user.graph` 从图上锁定这两条：
  有 `+ → modify`，没有 `A → modify`）。
- 被 `wait` 的表达式如果不涉及任何 `var`（例如只有输入输出类副作用的 `wait println(...)`），`wait` 没有
  可依据的东西，也就没有意义：它不产生任何边，也不会让这个调用执行。这类调用应当写在同步块里。
  因此不写 `wait` 不是错误，写了没有可排序对象的 `wait` 也不是错误。
- 同步块内**它自己直接写着的**表达式按书写顺序执行（写在里面的臂、循环体与嵌套块不继承这个顺序，
  见 2.2），这也包括：以一个值结尾的同步块（值在块内最后一个表达式之后交出）、
  写在同步块里的 `sync { }` 块（它自己是外层链上的一个语句，块内按它自己的写法）、
  `let (p, q) = f()` 这样一次绑定多个名字的声明。
- 同步块内的调用按书写顺序执行：块的相邻语句之间就是控制边。副作用只在默认的无序执行下才需要
  考虑，因为它限制了重排。
- 副作用的管理与追踪仍是研究中的部分：按引用捕获、写入的追踪与各个遍相互作用不好的情形，作为研究发现
  连同复现程序记录，不做临时性的修补。

### 2.4 静态约束

- 函数修饰符 `sync`（前缀 `sync func`）与关键字 `wait` 已退役（H461）：`var` 形参是资源参数，
  不再有任何声明要求，也不再因此告警（原 `VarParamInAsyncFunction`/`CapturedWriteInAsyncFunction`
  随前缀一起删除）。
- 写在函数体位置的 `sync`（函数体是同步块）与 lambda 的 `sync` 前缀保留。
- 调用带 `var` 形参的函数或算子、直接调用写入被捕获变量的函数、或进入有分支臂写入被捕获变量的分支，
  而不在同步块内：`IgnoredSideEffect` 告警。
- 把 `let` 对象传给 `var` 形参：编译期错误（见 2.3）。

### 2.5 实现现状（与上述规则的差距）

> 本小节记录**当前实现**与 2.1–2.4 规范之间的差距；规范以 2.1–2.4 为准。每条差距在代码跟上之后删除。
> 行为记录由程序运行确认（`test/cases/linear/runtime/effects_outside_sync.cml`，计划
> `linear.runtime_negative` 中的 `effects_outside_sync.*`）。

符合规范、已由测试锁定的行为：

1. 非同步函数体中，结果未被使用的效应调用（例如一条 `println(...)` 语句）不执行，在所有调度器上一致；
   在它前面写 `wait` 也一样（不涉及 `var`，`wait` 无可依据，见 2.3）。
2. 前缀 `sync func` 只给函数类型加 `Modifier::Sync`，函数体是普通块；后缀 `sync` 使函数体成为同步块。
   AST 中这是两个独立的标记（`src/parse/ast/builder.cpp` 的 `visitFuncDecl` / `visitStmtBlock`）。
3. 结果作为返回值的效应调用（`return println(...)`）在数据路径上，会执行。

与规范的差距（跟踪项见 `OWNER-QUESTIONS` 的 A1、A3–A6、A9）：

- **规范未明确、实现暂定的几点**（`var` 形参、捕获、`let` 检查、别名均已按 2.3 实现，见 8.1）：没有 `var`
  的形参视同 `let`；`var` 变量传给没有 `var` 的形参或绑定到 `let` 时取当时所持的对象（对象共享，不复制）；
  对 `s.x`、`arr[0]` 赋值是编译期错误；带 `var` 形参的函数不能交给会调用它的算子（macro 算子除外）。
- **JIT**：带 `var` 形参的图不再被排除，只把 `var` 形参继续传下去的函数在 `std::opt::inline std::jit` 下会被
  编译；但 JIT 对任何含通用算子（`OPER`）的图回退到解释器，赋值与 `:var/get` 都是这样的算子，所以读写变量
  的图仍由解释器执行（测试 `var_param_jit` 记录了这一现状）。
- **告警的范围。** 模块没有 Error 时，Warn 级诊断在编译之后、运行之前输出到标准错误
  （`Context::dumpModuleWarnings`，见 `docs/cli.md` “Warnings”）。`IgnoredSideEffect` 只对变量的写入产生；
  同步块之外、没有任何东西依赖它而被丢弃的效应（不纯算子、有副作用的函数调用、赋值、含这类分支臂的分支）
  由 `EffectNotExecuted` 报告：每张图建完后，从出口沿数据边和控制边反向可达之外的效应节点就是被丢弃的
  （`Builder::reportDroppedEffects`）。通过函数值的调用只能按类型识别。
- **`wait` 的跟踪范围**：`A(i); wait modify(i); B(i)` 的两段依赖都已建立（3.3）。不带 `wait` 且不在同步块
  内的写入与此前的读取之间没有顺序。经由捕获（含模块顶层变量）的写入，对直接调用和分支已经跟踪（3.3）；
  通过函数值（作为实参、返回值或存放起来的 lambda / 函数）进行的调用不在其内：函数类型不携带“写入了哪些
  被捕获变量”这一信息。

尚待语言层面确认（实现没有替语言做决定）：

- 非同步函数体内嵌套的 `sync { ... }` 块，其值未被使用时是否执行。当前实现：块内语句彼此有序，但块尾没有
  接到外层出口，整块不执行。按“依赖驱动”规则这可以是预期行为，按“同步块按书写顺序执行”也可以认为应当
  执行；两种读法都与现有文字相容，未作选择。

## 3. 编译实现层：从 AST/GCT 到 GIR 的落地

## 附：代码示例（sync 与函数体）

```camel
module sync_body_demo

// 修改 var 形参。函数体是同步块：修改与读取按书写顺序执行。
func bump(var x: int): void sync {
    x += 1
}

// 有副作用：I/O。函数体是同步块，println 按书写顺序执行。
func log_value(x: int): void sync {
    println("x = {}"->format<x>)
}

func main(): int sync {
    var c = 0          // var：可变对象，可以传给 var 形参

    bump(c)
    log_value(c)       // x = 1
    bump(c)
    log_value(c)       // x = 2

    return c
}
```

```camel
module order_demo

func bump(var x: int): void sync {
    x += 1
}

func main(): int sync {
    var c = 0

    // 不在同步块里的写入只与读取它的操作按依赖排序：这里的 let 各自读到前一次写入之后的 c
    bump(c)
    let after1 = c

    bump(c)
    let after2 = c

    // 注意：这里若再写一条结果未被使用的 println(...)，它不在同步块内，不会执行（见 2.2）
    return after2
}
```

### 3.1 AST 层标记

- 解析器在 `stmtBlock` 上记录 `synced` 标志。
- 解析器在 `waitExpr`（以及语法允许时的 `WAIT stmtBlock`）上记录 `waited` 标志。

### 3.2 GCT 层节点

- `StmtBlock(sync=...)` lower 为 `ExecLoad(synced)`。
- `wait` 表达式 lower 为 `WaitLoad(innerExpr)` 包装（二元表达式、保留表达式等路径均有实现）。

### 3.3 GIR Builder 状态机

`src/compile/gir/builder.cpp` 中有两类关键状态：

- `synced_`：当前是否在同步执行上下文。
- `waited_`：当前表达式是否被 `wait` 包装。

行为要点：

- `visitExecNode` 进入/退出时切换 `synced_`。
- `visitWaitNode` 进入/退出时切换 `waited_`。
- `visitLinkNode` 在处理调用/链接时，若参数为 `var` 语义：
  - `!waited_ && !synced_` => 触发 `IgnoredSideEffect`。
  - 在 `synced_` 中，通过 `lastSyncedNode_` 串联控制边，构造顺序链；对变量的读取（`:var/get`）同样进入
    这条链。
  - 调用成为该变量的写入者（`recordWrite`）：此后对该变量的读取与写入都排在它之后；若它带 `wait` 且不在
    同步块内，它还排在此前用过该变量取值的每个操作之后（`A → modify → B`）。
- **被捕获的变量**（包括模块顶层变量）。函数、lambda、分支臂引用外层图的变量时，变量的单元经捕获端口进入
  子图，函数类型里没有它。Builder 为每张图记一份摘要（`CaptureSummary`）：捕获了哪些变量、函数体自己写了
  哪些、直接调用了哪些图（含分支臂）。图的写入集合是这份调用关系上的最小不动点（`capturedWritesOf`）：
  自己写的，加上被调图写的、且自己也捕获了的那些；递归调用不给函数自己的集合增加任何东西。
  - 直接调用一张图（`visitLinkNode`，被调图已知）或进入分支（`visitBrchNode`，从 `BRCH` 到 `JOIN`）时，
    对被调图捕获的每个变量：调用/分支排在该变量此前的写入者之后；被调图写它，就按上一条记为写入者
    （分支以 `BRCH` 为起点、`JOIN` 为完成点）；只读，就记为一次使用，排在此后带 `wait` 的写入者之前。
  - 函数体还没建完时（递归调用自身，直接或经由分支臂）它将来写什么还不知道。此时在函数体内部按
    “它捕获的变量都可能被写”来排序（保守，只多加控制边）；函数建完后对外的摘要是精确的，告警也在全部
    函数体建完后按精确的集合发出。
  - 捕获端口绑定在被捕获的值本身（同一个节点，模块顶层的值是公共 root 上的节点），调用处传入的是同一个值
    在调用者看来的样子，不按名字在调用处重找（`src/compile/gir/captures.cpp`）。不可变值的捕获可以在
    调用之后才出现（递归函数先调用自己、后用到值），由链接步补上；变量的捕获不行：调用处已经按当时知道的
    变量排好了序，所以函数被直接调用之后再捕获变量是 `CaptureAfterRecursiveCall`。函数体在第一次被用到时
    就建出来（三步加载的第二步，`src/compile/gir/program.cpp`），这只会出现在递归里：函数在调用自己之后才
    第一次用到某个变量。写入摘要（`capturedWritesOf`）与"出口是否依赖效应"（`settleEffectfulGraphs`）都在
    全程序的函数体建完之后、在链接步取不动点。
  - 通过函数值的调用（`CALL` 节点、交给算子的 lambda）不查摘要。

> 这就是语言层“sync/wait 控制副作用可见性”的具体编译落点。

### 3.4 当前实现边界

从现有代码可见，`wait` 的核心语义落实在 `waitExpr -> WaitLoad` 路径。  
`WAIT stmtBlock` 的 AST 标记目前未在 GCT builder 中形成对称的专门 lower 逻辑（即 block 级 waited 标志未被同等消费）；这不影响 `wait expr` 主路径，但属于后续可增强点。

### 3.5 控制边约简现在在哪（H95 结论）

控制边的传递约简只有一个入口：`std::opt::order`（`OrderReductionPass::apply`，预算 `250000`）。建图器不做传递约简：`shouldAddSyncCtrlLink` 只在“同一前驱已经是直接值输入”时跳过一次控制边（`hasDirectDataDependency`），这是精确、局部的去重，不查路径。编码前的 `reduceControlDependencies(*prepared.draft, 8192)` 已删除。`include/camel/runtime/control.h` 的规则不变：删除 Port/Data 控制输入和值已蕴含的边，保留 BRCH/JOIN 边界与 GATE 的第一条 CTRL。

H95 复测 H85 对比过的 6 个程序：带不带 `std::opt::order` 都与改动前逐字节相同，唯一例外是 `test/cases/linear/byref/element_assigned.cml` 在不带 `std::opt::order` 时多一条 `:var/take -> :op/assn_owned` 的 CTRL 边。那是 `:var/take` 与随后 `:op/assn_owned` 之间真实的顺序要求（`assn_owned` 要排在取出之后），`std::opt::order` 的传递约简会把它删掉；它不是使用关系，见下。

H85 把 `path_assigned_allocs.cml` 的 `2 0 0` 读成“多出的控制边让 `:var/take` 的结果多了一个使用者，原地改写因此又复制了一次”。H95 用分配计数和运行轨迹复核，结论不同：

1. 两个图（有/无约简）里 `:var/take` 都只复制一次，`path_assigned_allocs.cml` 的整段复制序列完全相同，计数差不在复制上。
2. 第一个测量窗口里多出的 320 字节块是**数组字面量自己的构造**：GIR 是 `字面量(Data) -> COPY -> :var/new`，`COPY` 在无约简的图里被排在第一个 `std::mem::blocks()` 之后，在基线的图里排在它之前。
3. 原因不在共享判定，而在顺序没有被写进图：`:op/mem::blocks()` 这个语句节点没有任何输入（jgir 里是根），`var v = [...]` 的声明也不进入同步链（`lastSyncedNode_` 只由读、调用、分支、循环更新），所以“先构造还是先测量”不受边约束；`computeExecutionOrder` 是从出口出发的 DFS 后序，边集一变，两个互不约束的节点就换了位置。字面量的构造除了这一次分配没有可观察效应，因此图没有违反 3.3/3.4，是测量窗口写得过宽。

H95 把该用例改成用调用构造数组（`make_array()`）：调用是同步链上的语句，构造稳定地落在第一个窗口之前。四个后端在有/无 `std::opt::order` 时都是 `1 0 0`。

原地改写的共享判定不数使用者：`:var/take` 只在变量单元格没有“独占数组”标记时复制，`:op/assn_owned` 置位，`:var/get`、`:op/assn` 清除，`:var/peek` 保留。控制边只表达顺序、不读值（3.3、3.4），所以加一条控制边不会让这条语句复制。

## 4. 核心 GIR 节点职责

### 4.1 `BRCH`

- 表示分支选择点。
- 从条件输入计算“命中分支索引”。
- 通过控制输出把执行引导到分支候选。

### 4.2 `JOIN`

- 表示分支结果汇聚点。
- 语义是“从多个候选值中选出当前路径对应的值”。
- 对外表现为一个值节点，便于后续链路统一消费。

### 4.3 `GATE`

- 表示“值 + 控制”联合锚点（output anchor）。
- `normInputs[0]` 给出返回值来源；`ctrlInputs` 给出必须等待的控制尾节点。
- 在 GIR 中，`GATE` 的 `index()` 语义上转发到其 `normInputs[0]`，因此不会改变返回槽位，只增加控制约束。

## 5. `EXIT` 的正确建模规则

在函数出口，编译器必须回答两个问题：

1. 返回哪个值？（通常是 `resNode`）
2. 还要等待哪些同步控制尾节点？（例如同步块最后一个副作用节点、节点修饰器链尾）

若存在任何待等待控制尾，`EXIT` 应锚定到 `GATE`，并满足：

- `Norm: resNode -> GATE`
- `Ctrl: tail_i -> GATE`（对每个控制尾）
- `Graph.output = GATE`

若不存在控制尾，`Graph.output` 可直接为 `resNode`。

## 6. 为什么不是总用 `JOIN`

`JOIN` 只解决“分支值收敛”，不表达“副作用完成点”。

典型场景：

- 先得到返回值（如 `JOIN`）
- 后续仍有同步副作用（如 `println`）

若 `ExitNode` 仍指向 `JOIN`，调度器可能在副作用完成前就把函数视为完成，破坏同步语义。

`GATE` 的存在就是把“值已就绪”与“控制已完成”绑定为单一出口契约。

## 7. 本次修正的要点（Builder 层）

问题根因是：在 `visitExitNode` 中，代码曾过早以 `resNode` 作为控制连边校验目标，导致某些 `tail -> resNode`（例如 `println -> JOIN`）被判定为潜在环，从而误判“无需 GATE”。

修正策略：

1. 先收集潜在控制尾（不先用 `resNode` 过滤）。
2. 若存在控制尾且返回节点不是 `GATE`，先创建 `GATE` 并连接 `Norm`。
3. 再对最终 `outputAnchor`（通常是 `GATE`）做 Ctrl 连边合法性检查并连边。

该策略与语言目标一致：出口锚点优先保证语义正确，再做图合法性校验。

## 8. 与各执行后端的契约

- **NodeVM**：`GATE` 作为无操作节点处理，返回值仍按 `exitNode()->index()` 读取；当出口为 `GATE` 时索引转发到其 norm 输入。
- **FastVM**：`GATE` 不产生核心计算字节码，主要作为图结构语义节点参与控制/跳转相关约定。
- **Taskflow**：`GATE`/`SYNC` 需至少有占位任务以承接依赖边，保证并行调度下的完成顺序与线性语义一致。

### 8.1 变量与按引用

`var` 变量是一个**单元**：只有一个槽位的堆对象（运行时是 `Tuple`，类型为 `TupleType::cell(T)`，打印为
`var T`）。名字绑定到的节点保存的是指向单元的指针，读写变量都是作用在单元上的算子：

| 源码 | 图 |
|---|---|
| `var x = e` | `x := OPER :var/new(e)` |
| 读取 `x` | `OPER :var/get(x)`，取该节点执行时单元里的值 |
| `x = e`、`x += e` 等 | `OPER :op/assn*(x, e)`，写入单元 |
| `var p: T` 形参 | 一个 PORT，槽位里是调用方的单元 |
| 实参传给函数的 `var` 形参 | 直接传单元节点；不是名字的表达式先由 `:var/new` 得到一个新单元 |
| 实参传给没有 `var` 的形参、`let y = x` | 传 `:var/get(x)` |
| 分支臂 / lambda 中出现 `x` | 捕获端口（with 端口或闭包槽位）里是同一个单元 |

由此，调用方、被调方、分支臂与闭包持有的是同一个指针；实参绑定、闭包 FILL、尾调用复用帧、内联把端口替换成
实参节点，复制的都只是指针，所以各调度器、各遍之后观察到的是同一个对象，不需要回写，也不要求被调帧存活。
函数类型仍记录源码签名（参数类型 `T` 与 `var` 标记）；重载决议按值类型进行（`valueTypeOf`）。

- 赋值之外不再有带 `var` 形参的内建或标准库算子：`append`、`extend`、`apply`、`array::sort` 等返回新值，
  不修改实参（设计原则 11.3）。写成语句的 `acc = acc->append<v>` 由图构建器按算子声明的原地形式
  （`OperatorTable::setInPlaceForm`）写成 `:op/assn_owned(acc, :struct/append_arr_in_place(v, :var/take(acc)))`：
  `acc` 的单元作为 `var` 实参，记为对它的一次写入；`:var/take` 在单元只持有该数组时把原数组交给原地形式，
  否则给一份复制，原地形式改完返回它（`src/builtin/operators/ops.cpp`、`struct.cpp`）。模块的算子仍可以
  声明 `var` 形参：它们收到的是对象本身（`:var/get` 的结果），该调用记为对这个变量的一次写入。
- `:var/new`、`:var/get`、`:op/assn*` 都不是纯算子：折叠、CSE、特化不会越过它们；CSE 也不合并以
  `:var/get` 的结果为输入的相同表达式（两次求值之间对象可能被原地修改）。`isWrittenInPlace`
  （`include/camel/runtime/draft_writes.h`）仍用于识别传给 `var` 形参的节点。
- `let` 的检查在图构建时完成（`SemanticDiag::ImmutablePassedToVarParam`），运行期没有任何检查。

## 9. 工程建议与回归样例

建议增加以下回归样例，防止出口锚点退化：

- **返回值先就绪，后置副作用**：应生成 `GATE`，出口指向 `GATE`。
- **分支值经 `JOIN` 收敛，分支后仍有副作用**：出口仍应指向 `GATE`。
- **纯值函数（无同步副作用）**：允许出口直接为值节点。

推荐检查方式：

- `camel <file>.cml std::gir`
- 关注函数子图里 `EXIT` 入边是否来自 `GATE`，以及 `GATE` 是否同时拥有 `Norm` 与 `Ctrl` 输入。

## 10. 一句话总结

`JOIN` 负责“把分支值变成一个值”，`GATE` 负责“把这个值和同步控制完成点绑定为函数出口”。控制边的传递约简只在 `std::opt::order`；建图器只跳过“前驱已经是直接值输入”的边，控制边只表达顺序，不参与共享判定（3.5）。
