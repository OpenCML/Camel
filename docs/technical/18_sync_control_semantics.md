# Camel 同步控制语义解读

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

当前实现可见以下语法入口：

- `stmtBlock : SYNC? '{' stmtList? '}'`
- `blockStmt : WAIT? stmtBlock`
- `waitExpr : WAIT? assignExpr`

函数层面，`sync` 通过函数类型修饰符体现（`Modifier::Sync`），例如 `func main(): int sync { ... }` 或 `sync func ...`。

### 2.2 `sync` 的语义

`sync` 的语义是：**在该作用域内显式建立副作用顺序约束**。  
它不改变值语义本身，而是要求编译器为有副作用/可变参数相关的操作串联控制边，使“后语句依赖前语句完成”在 GIR 和调度器中可见。

直观理解：

- 无 `sync`：以值依赖为主，调度器可更自由并行。
- 有 `sync`：增加控制依赖链，保证顺序完成语义。

可将其简单理解为：`sync` 块把该块内操作从“默认可异步/可并行调度”收紧为“按代码书写顺序同步执行”。

### 2.3 `wait` 的语义

`wait expr` 的语义是：**将该表达式标记为“需要等待其副作用完成”**。  
在当前实现中，`wait` 主要用于抑制“副作用被忽略”的语义告警，并参与 GIR builder 的副作用判定逻辑（见第 3 节）。

在非 `sync` 上下文中，调用有副作用的逻辑（例如会修改 `var` 参数或发生 I/O）应显式使用 `wait`。  
可以把 `wait` 看成一个顺序边界：依赖被修改值的后续代码必须在该副作用完成后执行，从而保证“先写先执行、后写后执行”的时序符合预期。

### 2.4 静态约束（实现已落地）

- 变量参数（`var`）只允许出现在 `sync` 函数中；否则触发 `VarParamInAsyncFunction`。
- 调用带副作用签名（典型是 `var` 参数）而既不在 `sync` 上下文也不加 `wait`，会触发 `IgnoredSideEffect` 告警。

## 3. 编译实现层：从 AST/GCT 到 GIR 的落地

## 附：代码示例（sync 与 wait）

```camel
module sync_wait_demo

// 有副作用：修改 var 参数
func bump(var x: int): void {
    x += 1
}

// 有副作用：I/O
func log_value(x: int): void {
    println("x = {}"->format<x>)
}

func main(): int sync {
    let c = 0

    // sync 块内按书写顺序执行，修改和读取顺序稳定
    wait bump(c)
    wait log_value(c)
    wait bump(c)
    wait log_value(c)

    return c
}
```

```camel
module wait_boundary_demo

func bump(var x: int): void {
    x += 1
}

func main(): int {
    let c = 0

    // 非 sync 上下文，副作用调用需显式 wait
    wait bump(c)
    let after1 = c

    wait bump(c)
    let after2 = c

    println("after1={}, after2={}"->format<after1, after2>)
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
