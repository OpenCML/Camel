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
  块内表达式按书写顺序执行（等价于编译器在相邻表达式之间插入控制边）。函数类型上没有 `Modifier::Sync`。

二者可以同时使用：`sync func f(...): T sync { ... }`。

### 2.2 求值规则与 `sync` 块

Camel 的求值是**依赖驱动**的：一个表达式被求值，当且仅当它的结果（经数据边或控制边）被函数出口需要。

- **同步块之外，结果未被使用的效应调用不会执行。** 这是依赖驱动求值的自然结果，是合法的语言规则，
  不是缺陷；编译器在能够检测到时给出告警。
- **同步块**（`sync { ... }`，或以 `sync` 块作为函数体）内，表达式按书写顺序执行；编译器在相邻表达式
  之间建立控制边，块尾成为块完成的条件。
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
- 同步块内调用什么都无所谓，写不写 `wait` 也无所谓：块内已经有顺序。副作用只在默认的无序执行下才需要
  考虑，因为它限制了重排。
- 副作用的管理与追踪仍是研究中的部分：按引用捕获、`wait` 跟踪与各个遍相互作用不好的情形，作为研究发现
  连同复现程序记录，不做临时性的修补。

### 2.4 静态约束

- `var` 形参只允许出现在带函数修饰符 `sync` 的函数中（前缀 `sync func`）；否则 `VarParamInAsyncFunction`。
- 调用带副作用签名（典型是 `var` 形参）而既不在同步块内也不加 `wait`：`IgnoredSideEffect` 告警。
- 把 `let` 对象传给 `var` 形参：编译期错误（见 2.3）。

### 2.5 实现现状（与上述规则的差距）

> 本小节记录**当前实现**与 2.1–2.4 规范之间的差距；规范以 2.1–2.4 为准。每条差距在代码跟上之后删除。
> 行为记录由程序运行确认（`test/cases/linear/runtime/effects_outside_sync.cml`，计划
> `linear.runtime_negative` 中的 `effects_outside_sync.*`）。

符合规范、已由测试锁定的行为：

1. 非同步函数体中，结果未被使用的效应调用（例如一条 `println(...)` 语句）不执行，在所有调度器上一致。
2. 前缀 `sync func` 只给函数类型加 `Modifier::Sync`，函数体是普通块；后缀 `sync` 使函数体成为同步块。
   AST 中这是两个独立的标记（`src/parse/ast/builder.cpp` 的 `visitFuncDecl` / `visitStmtBlock`）。
3. 结果作为返回值的效应调用（`return println(...)`）在数据路径上，会执行。

与规范的差距（跟踪项见 `OWNER-QUESTIONS` 的 A1、A3–A6、A9）：

- **`var` 形参尚不是真正的引用。** 目前在调用返回时把被调帧中的值写回实参槽位（见 8.1），因此带 `var`
  形参的函数不做尾调用、不进入 JIT；常量实参不被写回。目标是按引用实现，恢复尾调用与 JIT。
- **捕获变量的写入尚未穿透到外层变量**：零遍执行丢失 `if` 分支或 lambda 内对捕获变量的写入，分支被内联
  后写入又会生效（结果随流水线而变）。
- **`let` 对象传给 `var` 形参尚未被拒绝**：目前可以编译，零遍执行不写入该常量，内联之后会写入共享常量。
- **同一变量传给两个 `var` 形参**：目前按顺序回写，内联后成为别名；探针上结果相同，但没有保证。
- **告警尚不显示。** `IgnoredSideEffect` 只对 `var` 形参调用产生，对 `println` 这类不纯算子不产生；并且
  模块没有 Error 时，Warn 级诊断不输出（`Context::dumpAllModuleDiagnostics`）。因此被丢弃的效应目前没有提示。
- **`wait` 在非同步上下文中**目前只影响 `IgnoredSideEffect` 的判定；对带可变输入的调用，依赖关系经
  `var` 实参的修饰链建立。

尚待语言层面确认（实现没有替语言做决定）：

- 非同步函数体内嵌套的 `sync { ... }` 块，其值未被使用时是否执行。当前实现：块内语句彼此有序，但块尾没有
  接到外层出口，整块不执行。按“依赖驱动”规则这可以是预期行为，按“同步块按书写顺序执行”也可以认为应当
  执行；两种读法都与现有文字相容，未作选择。

## 3. 编译实现层：从 AST/GCT 到 GIR 的落地

## 附：代码示例（sync 与 wait）

```camel
module sync_wait_demo

// 有副作用：修改 var 形参。带 var 形参的函数用函数修饰符 sync 声明。
sync func bump(var x: int): void {
    x += 1
}

// 有副作用：I/O。函数体是同步块，println 按书写顺序执行。
func log_value(x: int): void sync {
    println("x = {}"->format<x>)
}

func main(): int sync {
    var c = 0          // var：可变对象，可以传给 var 形参

    // 同步块内按书写顺序执行，修改和读取顺序稳定
    wait bump(c)
    wait log_value(c)  // x = 1
    wait bump(c)
    wait log_value(c)  // x = 2

    return c
}
```

```camel
module wait_boundary_demo

sync func bump(var x: int): void {
    x += 1
}

func main(): int {
    var c = 0

    // 非同步块：对带可变输入的调用使用 wait，编译器跟踪其副作用并插入控制边，
    // 读取 c 的后续代码在写入完成之后执行
    wait bump(c)
    let after1 = c

    wait bump(c)
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
  - 在 `synced_` 中，通过 `lastSyncedNode_` 串联控制边，构造顺序链。

> 这就是语言层“sync/wait 控制副作用可见性”的具体编译落点。

### 3.4 当前实现边界

从现有代码可见，`wait` 的核心语义落实在 `waitExpr -> WaitLoad` 路径。  
`WAIT stmtBlock` 的 AST 标记目前未在 GCT builder 中形成对称的专门 lower 逻辑（即 block 级 waited 标志未被同等消费）；这不影响 `wait expr` 主路径，但属于后续可增强点。

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

### 8.1 `var` 参数的回写

> **实现现状，不是规范。** 规范是 2.3：`var` 形参是真正的引用。本小节描述的“返回时回写”是当前的过渡实现，
> 与规范不一致之处（不做尾调用、不进入 JIT、别名与捕获的情形）见 2.5；待按引用实现后本小节将被替换。

`var` 参数按引用传递（见 2.3 与语言手册 4.4.2）：被调函数对 `var` 参数的赋值，调用返回后在调用者传入的变量上可见。赋值算子（`:op/assn*`）只写被调帧里该参数的槽位，所以每个执行后端在调用返回、被调帧释放之前，把各 `var` 参数的最终值写回调用点对应实参的槽位（`camel::execute::writeBackVarArgs`，直接调用 `FUNC` 与间接调用 `CALL` 相同）：

- **NodeVM / Taskflow / 宏求值器**：调用返回后回写，再释放被调帧。
- **FastVM**：带 `var` 参数的图，其 `RETN` 的 `fastop[1]` 置位，返回时向调用栈顶的直接调用回写；间接调用走解释器并在返回后回写。
- **JIT**：带 `var` 参数的图、以及调用这类图的图不编译，留在解释器中执行。
- 回写要求被调帧就是绑定实参的那一帧，因此带 `var` 参数的图既不发起尾调用，也不被尾调用。

内联之后参数端口直接绑定到实参节点，赋值算子写的就是实参的槽位，与回写结果一致。重写类 pass 用 `isWrittenInPlace`（`include/camel/runtime/draft_writes.h`）识别被原地写的节点：这样的节点不是一个值，不会被折叠、合并，也不会被特化成常量。

当前**未定**、各管线结果可能不同的情形（实现没有替语言做决定）：

- 实参是静态常量（例如 `let c = 0` 后 `wait bump(c)`，即本文附录示例的写法）：解释执行不回写常量槽位，内联后赋值算子会直接写常量槽位。
- 被分支臂或 lambda 捕获的变量在臂/lambda 内被写：捕获是按值拷贝进子图的，解释执行时写不回外层；臂被内联后写的是外层槽位。
- 同一个变量同时传给两个 `var` 参数。

## 9. 工程建议与回归样例

建议增加以下回归样例，防止出口锚点退化：

- **返回值先就绪，后置副作用**：应生成 `GATE`，出口指向 `GATE`。
- **分支值经 `JOIN` 收敛，分支后仍有副作用**：出口仍应指向 `GATE`。
- **纯值函数（无同步副作用）**：允许出口直接为值节点。

推荐检查方式：

- `camel <file>.cml std::gir`
- 关注函数子图里 `EXIT` 入边是否来自 `GATE`，以及 `GATE` 是否同时拥有 `Norm` 与 `Ctrl` 输入。

## 10. 一句话总结

`JOIN` 负责“把分支值变成一个值”，`GATE` 负责“把这个值和同步控制完成点绑定为函数出口”。
