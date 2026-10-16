# 调试 Camel 程序

中文简体 | [English](debugging.en.md)

`camel-db` 是 Camel 的调试器。本文讲源码级调试：源码行断点与函数断点、单步、调用栈、变量。在编辑器里走
[Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/)（DAP）：任何带 DAP
客户端的编辑器都能用，[`editors/vscode`](../editors/vscode) 里的 VS Code 扩展已经配置好。在终端里，
`camel-db` 的 REPL 有同样的命令（[第 2 节](#2-在命令行里)）。

Camel 的程序是一张图，调试器不把它伪装成别的东西。"下一行""调用栈""变量"在 Camel 里各是什么意思，见
[第 3 节](#3-你看到的是什么为什么)；读一遍，可以少很多意外。设计与内部结构见
[技术文档 35](technical/35_debugger.md)。

已在 Linux x86-64 上验证。Windows 上适配器及其进程处理能构建，但没有运行过；VS Code 的界面没有人工试过
（测试直接对适配器讲协议），见[第 5 节](#5-限制)。

---

## 1. 在 VS Code 里

构建 Camel（`npm run build`），按[编辑器支持](editor-support.cn.md)安装扩展。扩展启动的是
`camel-db --dap`；查找 `camel-db` 的顺序是设置 `camel.debugger.path`、`$CAMEL_HOME/bin`、`PATH`。

- **没有 `launch.json`**：打开一个 `.cml` 文件，按 F5。该文件在字节码虚拟机（`std::fvm`）上调试。
- **有 `launch.json`**：*运行和调试 → 创建 launch.json 文件 → Camel*，或用代码片段
  *Camel: Debug current file*、*Camel: Debug with a pass sequence* 添加配置：

```json
{
    "type": "camel",
    "request": "launch",
    "name": "Debug Camel file",
    "program": "${file}",
    "passes": ["std::fvm"],
    "args": ["n=10"],
    "stopOnEntry": false,
    "stepBy": "line",
    "frames": "functions"
}
```

| 属性 | 含义 |
|------|------|
| `program` | 要调试的 `.cml` 文件。必填。 |
| `passes` | 遍序列，与命令行相同（`camel file.cml std::inline std::fvm`）。为空或不写：编译得到的图，在 `std::nvm` 上运行。 |
| `args` | `main` 的参数值，每项 `name=value`（即 `camel` 的 `--arg` 选项）。 |
| `cwd` | 程序的工作目录。 |
| `env` | 为程序追加的环境变量。 |
| `stopOnEntry` | 在第一个有源码位置的节点之前停下。 |
| `stepBy` | `line`（默认）或 `node`：单步命令以什么为单位，见 [3.2](#32-下一行是下一个被执行的行)。 |
| `frames` | `functions`（默认）或 `subgraphs`：分支的臂并入所在函数的帧，还是各自成帧，见 [3.3](#33-调用栈是函数调用的链)。 |

断点照常在行号旁设置；*函数断点*（*断点* 面板上的 **+**）接受函数的完整名字 `模块::路径::名字`，只有一个
模块有这个名字时也可以省略模块（[3.3](#33-调用栈是函数调用的链)）。*单步跳过*、*单步调试*、*单步跳出*、
*暂停*、*继续*、*停止*，*调用堆栈*、*变量*、*监视* 面板以及把鼠标悬停在名字上，都走标准协议。*断点*
面板里有两项针对故障：**Faults that end the program**（使程序终止的故障，默认开启）与
**Faults isolated by attempt**（被 `attempt` 隔离的故障）。命令 **Camel: Toggle Subgraph Frames**
（调试 Camel 程序时也是 *调用堆栈* 视图标题上的一个按钮）在程序运行或停住时切换 `frames`。

## 2. 在命令行里

### 2.1 REPL

`camel-db <file.cml>` 加载文件并给出提示符。调试会话的命令：

| 命令 | |
|------|---|
| `break <文件>:<行>`、`break <行>`、`break <函数>`（`b`） | 在一行上（只给行号时是已加载的文件）或按完整名字在一个函数上设断点 |
| `delete` | 删除全部断点 |
| `run [遍 ...]`（`r`） | 运行已加载的文件，可带遍序列（`run std::fvm`），等到停下 |
| `continue`（`c`） | 继续，等到下一次停下 |
| `next`（`n`）、`step`、`finish` | 按源码行单步跳过、进入、跳出 |
| `nexti`（`ni`）、`stepi`（`si`） | 按图节点单步跳过、进入 |
| `pause` | 让运行中的程序在下一个节点停下 |
| `bt`（`backtrace`、`where`） | 调用栈 |
| `locals [n]` | `bt` 中第 `n` 帧的变量（默认最上面一帧） |
| `print <名字>`（`p`） | 一个名字的值，可带成员访问（`p.x`、`t.0`、`xs[2]`） |
| `frames [functions\|subgraphs]` | 显示或切换分支的臂怎样显示（[3.3](#33-调用栈是函数调用的链)） |

让程序运行的命令会等到程序停下或结束，途中打印程序的输出。`help` 列出全部命令（其余的是网关任务
用的命令）；Ctrl+C 结束等待，不结束程序。用
[`folded.cml`](../test/cases/tools/debugger/fixtures/folded.cml) 的一次会话（每条命令会以它发出的核心命令
回显一行 `(camel-db) debug ...`，这里略去）：

```text
(camel-db) file folded.cml
Loaded: folded.cml (task 8766).
(camel-db) break 31
Breakpoint 1 at folded.cml:31
(camel-db) break 18
Breakpoint 2 at folded.cml:18 (not verified: Line 18 is not executed: nothing uses the value of `unused`. Evaluation is driven by dependencies; to run it, use its result or put it in a sync block.)
(camel-db) run std::fvm
Running folded.cml
Stopped (breakpoint) in main  folded.cml:31:17
(camel-db) step
Stopped (step) in pick  folded.cml:17:18
(camel-db) next
Stopped (step) in pick  folded.cml:19:18
  line 18 was not executed: nothing uses the value of `unused`
(camel-db) next
Stopped (step) in pick  folded.cml:20:9
(camel-db) next
Stopped (step) in pick  folded.cml:21:13
(camel-db) bt
#0  pick  folded.cml:21:13
#1  main  folded.cml:31:17
#2  __root__
(camel-db) locals
[Locals]
  n: int = 3
  base: int = 4
  unused: int = <not in the graph: nothing uses its value>
  r: int = <not evaluated>
  big: int = 12
  ok: bool = <not evaluated>
(camel-db) frames subgraphs
Frames: subgraphs (every subgraph that runs is a frame).
(camel-db) bt
#0  pick [arm at line 20]  folded.cml:21:13
#1  pick  folded.cml:20:9
#2  main  folded.cml:31:17
#3  __root__
(camel-db) continue
14
Run completed.
Breakpoint at folded.cml:18 was never hit: line 18 was not executed: nothing uses the value of `unused`.
Program finished: completed (exit code 0).
```

### 2.2 脚本化的 DAP 会话

`camel-db --dap` 在标准输入输出上讲 DAP，所以通常由编辑器启动。不用编辑器想试一次会话，仓库里有一个
小的脚本客户端：

```bash
node test/tools/debugger/dap_session.mjs test/cases/tools/debugger/fixtures/recursion.cml \
     --passes std::fvm b:8 go stack vars c stack vars
```

`b:<行号>` 设断点，`f:<名字>` 设函数断点，`go` 启动，`c` 继续，`n` / `s` / `o` 是跳过、进入、跳出，
`ni` / `si` 按图节点单步，`stack`、`vars`、`nodes`、`e:<名字>` 查看，`frames:subgraphs` /
`frames:functions` 切换帧的显示（`--frames` 在启动时设定）；脚本开头列出了全部步骤。上面这次会话，断点设在
一个尾递归函数里：

```text
-> setBreakpoints {"source":{"path":".../recursion.cml"},"breakpoints":[{"line":8}]}
   line 8: verified
-> configurationDone
   (node %18 OPER :op/eq_l)
<- stopped (breakpoint) in spin at recursion.cml:8:17
-> stackTrace {"threadId":1}
   #11 spin  recursion.cml:8
   #2 main  recursion.cml:13
   #1 __root__  recursion.cml:11
-> scopes {"frameId":11}
   [Locals]
     n: int = 5
     acc: int = 0
-> continue {"threadId":1}
   (node %18 OPER :op/eq_l)
<- stopped (breakpoint) in spin (tail call, 1 frame replaced) at recursion.cml:8:17
-> stackTrace {"threadId":1}
   #13 spin (tail call, 1 frame replaced)  recursion.cml:8
   #2 main  recursion.cml:13
   #1 __root__  recursion.cml:11
-> scopes {"frameId":13}
   [Locals]
     n: int = 4
     acc: int = 5
```

其他编辑器：把 DAP 客户端配置为运行 `camel-db --dap`，`launch` 请求带上表中的属性。

OpenCML-Ext 通过原生适配器提供 GIR 和内存界面。配合 pass 序列设置
`stopOnPassEntry: true`、`passBreakpoints: [2]`，可在初始图或返回图的遍后边界暂停。
GIR 调试栏区分真实执行位置与历史快照，选择历史不会继续执行。“查看调试内存”命令
提供暂停区域用量、字节/对象分页与显式刷新；分配断点需 Debug 构建，在 allocator
释放锁后暂停。详见
[`tools/debugger/README.md`](../tools/debugger/README.md)。

## 3. 你看到的是什么，为什么

### 3.1 程序停在一个图节点之前

执行就是图节点的执行：一个算子、一次调用、一个分支。调试器停在*某个节点运行之前*，显示该节点的源码位置，
所以高亮的位置是一行**加一列**：在 `let c = a + b` 里是那个 `+`。

一行可以有多个节点，也可以一个都没有：

- **没有节点的行**放不了断点。断点显示为未验证，消息说明是哪种情况：这一行没有代码；或者代码不会执行，
  因为没有东西用到它的结果（*Line 2 is not executed: nothing uses the value of `unused`.*）；或者被某个遍
  删掉了。第二种情况正是 Camel 的执行模型。在

  ```camel
  func next(x: int): int {
      let unused = x + 100     // 没人用它，函数体不是同步块：永远不执行
      return x + 1
  }
  ```

  里，`+ 100` 不在运行的图中；同一位置如果写的是 `println(x)`，也不会打印（*nothing uses its result*）。
  要让它执行，把代码放进同步块（`func next(x: int): int sync { ... }`），或者使用它的结果。程序结束时，
  调试器会再说一次这样的断点为什么没有命中（在调试控制台里，或在 REPL 里）。
- **没有东西调用的函数**里的行可以放断点，消息是 *Nothing in the program calls `helper`.*，它不会命中，
  结束时也同样说明。因为别的原因没有命中的断点（没被选中的臂、没有发生的调用），调试器什么也不说：它只说
  从图上知道的事。
- **行断点**在一次函数激活*进入*该行时停下：每次到访停一次，而不是该行每个节点停一次。

### 3.2 "下一行"是下一个被执行的行

求值由依赖驱动。同步块里的语句按书写顺序执行；同步块之外，调度器选一个满足依赖的顺序，没有被依赖的
代码根本不执行。

- **单步跳过**（`next`）运行到本次激活即将执行*另一行*上的节点，或已经返回调用者。到达的是
  **执行顺序上**的下一行。在同步块里它就是文本的下一行。在同步块之外，它可能是更后面的行（不执行的行被
  跳过），原则上也可能是更前面的行；这个顺序是后端对这张图采用的顺序，每次运行都相同。
- **单步进入**（`stepIn`）相同，但途中调用的函数内部也会停。
- **单步跳出**（`stepOut`）运行到调用者执行它的下一个节点。
- **按节点单步。** 行单步把一行当作一次停顿。要逐个走过一行里的节点（`println(f(x) + g(x))` 是三次调用
  加一次加法），启动时写 `"stepBy": "node"`：同样三条命令就停在下一个*节点*，列号指明是哪一个。DAP
  客户端也可以逐条请求指定 `granularity: "instruction"`。每次停下时伴随的 `camel/stopped` 事件带有该节点
  （`%20 OPER :op/add_l`）。
- **尾递归。** F10 跳过整次递归调用，直到尾调用链返回真正的调用者；F11 进入下一轮，在第一个可执行位置
  暂停。断点、故障和手动暂停仍可中断 F10。插件的 GIR 暂停区域显示新一轮参数及其前后值，不额外伪造栈帧。
- **没有执行的行。** 行单步越过一行、而那一行因为没有东西用到它的结果而没有执行时，调试器说一次：在调试
  控制台里（在 REPL 里是停顿下面），*line 18 was not executed: nothing uses the value of `unused`*。说的是
  单步起点与停点之间的行、单步离开的函数剩下的行、进入的函数开头的行；不说只是还没运行到的行，也不说没被
  选中的分支臂。

调试器从不改变程序的执行顺序，也从不为了显示而去求值。

### 3.3 调用栈是函数调用的链

函数是子图；一帧是一个函数的一次激活。

- 最底下的 **`__root__`** 是入口图，它唯一的工作是调用 `main`。
- **尾调用替换栈帧。** 以调用结尾的函数不再返回自身，两个后端都复用它的帧。栈如实显示现有的东西：
  一个尾递归循环不论跑了多久都只有*一*帧，并标出它替换过多少帧：`spin (tail call, 2 frames replaced)`。
  不虚构栈帧。
- **分支的臂属于所在函数的帧。** `if c then f(x) else g(x)`（以及 `match`、`&&`、`||`、`??`、`!!`、
  `?->`）编译成一个分支节点和每条臂一张小图，但臂是它所在函数的代码，栈也这样显示：停在臂里就是停在
  `pick` 的帧里，行单步从函数进入臂、再回到函数，与走过函数的其他行一样，臂里声明的名字在臂运行时是函数的
  名字之一，`fact` 在 `else` 臂里调用自己就是 `fact` 上面一个 `fact`。没被选中的臂不执行：里面的断点有效，
  但永远不会命中。
- **需要时显示真实的子图。** 用 `"frames": "subgraphs"`（或 *Camel: Toggle Subgraph Frames*，或 REPL 的
  `frames subgraphs`），每条臂是自己的一帧 `pick [arm at line 9]`，在函数的帧之上；分支是函数最后一件事时
  在函数的帧的位置（`... (tail call, 1 frame replaced)`）；行单步跳过一条臂如同跳过一次调用，尾调用也计入
  臂的帧（按函数显示时的 `spin (tail call, 2 frames replaced)`，这里是 `spin (tail call, 4 frames replaced)`）。
  Camel 有了循环之后，循环也按同样的方式显示。
- **完整的名字。** 函数按完整名字 `模块::路径::名字` 显示，与 Camel 其他地方一样：模块 `names_lib` 的命名
  空间 `Config` 里的函数是 `names_lib::Config::clamp`，程序自身某个命名空间里的是 `util::half`（省略模块）。
  函数断点接受同样的名字；只有一个模块有这个名字时也可以省略模块（`Config::clamp`），否则断点说明名字有
  歧义，并列出各个完整名字。
- **lambda** 显示为 `main [lambda at line 14]`。`map`、`filter`、`reduce`、`foreach` 的每次元素调用各是一帧。
- **泛型函数**对每组实参类型各有一张图（`twice<i64>`、`twice<string>`）；源码里的一个断点绑定到全部实例。

### 3.4 变量是已经运行的节点的值

`let` 给一个节点的值起名字。停下时：

- 节点在本次激活中**已经运行**的名字，显示它的值；
- 节点**还没运行**的名字，显示 `<not evaluated>`（未求值）。这是正常的：写在前面的 `let` 可能后执行，
  也可能永不执行（没人需要它）。调试器不替你求值，因为求值会运行程序尚未运行的代码（以及它的效应）；
- 节点**不在图里**的名字（没有被依赖，或被某个遍删除），会如实说明；
- **`var`** 是共享单元：显示当前内容，类型为 `var T`。被捕获的 `var` 与它来源函数里的是同一个单元；
- **`T?`** 显示值或 `null`；**`T!`** 显示值或 `error <种类>: <消息>`，可展开为种类、消息与位置；
- 数组、元组、结构体可展开为元素与字段（前 100 个）。

作用域：**Locals**（参数与函数里声明的名字，以及正在运行的臂里声明的名字）、**Captured**（lambda 捕获的
名字，以及函数用到的模块顶层的值，按完整名字：`names_lib::Config::limit`）、**Inlined**（内联进这张图的
其他函数的名字）、**Nodes**（按编号、种类、标签列出图的每个值节点：这一帧的图视图）。

**监视与悬停**接受一个名字，后面可以跟成员访问：`p.x`、`t.0`、`xs[2]`；模块的值可以用完整名字，也可以用
源码里写的结尾部分（`limit`、`Config::limit`）。其他表达式会被拒绝：求值意味着在停住的程序里构造并运行
一张图。

### 3.5 故障

故障（除零、下标越界、递归过深）在产生处让程序停下，作为异常呈现：编辑器在诊断的位置显示诊断文本，
以及那一刻的栈与变量。继续运行则让故障照常发展：程序带着诊断终止，退出码为 1，与不带调试器时完全一样。

错误*值*（`throw`、失败了的 `T!`）不是故障：它是普通的值，在变量里显示为 `error <种类>: <消息>`，
不会让程序停下。

`attempt(...)` 之内产生的故障被隔离：它在边界上变成一个错误值，程序继续。这类故障默认不让程序停下，
除非打开过滤器 *Faults isolated by attempt*。

## 4. 后端与遍

| 流水线 | 在调试器下 |
|--------|------------|
| 不写 | 编译得到的图，在 `std::nvm`（节点虚拟机）上：最贴近源码 |
| `std::nvm`、`std::fvm` | 支持 |
| `std::jit` | 按 `std::fvm` 运行：编译出的机器码停不下来。调试控制台会说明 |
| `std::prl` / `std::tf`（并行） | 按 `std::fvm` 运行：多个节点同时执行，没有唯一的"当前节点"。调试控制台会说明。调试器下的顺序是顺序执行的那一种 |
| 后端之前的优化遍（`std::inline`、`std::opt`、`std::macro` 等） | 支持，见下 |

遍会改写图，调试器显示的是实际运行的图。节点在遍之间保留源码位置，所以只要节点还在，停顿与断点就落在
正确的行上。变化的是：

- 所在行已经没有节点的断点，设置时是已验证的（对照编译得到的图），遍运行之后报告为未验证并给出原因；
- 内联之后，被内联的函数没有栈帧。帧是调用者的，在执行内联进来的节点时标为
  `main [inlined code of double]`；从调用者发起的行单步会跨过它们；
- 绑定到已被内联的调用、或已被折叠掉的值的名字，显示为*不在图里*；**Nodes** 作用域仍然列出现有的全部值。

## 5. 限制

- 一个线程，一次会话一个程序；不支持附加（attach），没有 restart 请求（编辑器通过重新启动来重启），
  没有条件断点、命中计数、日志断点、数据断点，不能修改值，不能反向单步。
- 除了带成员访问的名字，其他表达式不能求值。
- 程序的输出与停顿事件走不同的通道：紧挨着停顿之前打印的输出，可能在编辑器显示停顿之后片刻才出现在
  控制台。会话结束时，输出总是在报告程序结束之前就完整了。
- 标准输入没有接到程序上。
- 不能停在 C++ 实现的算子内部；`pause` 与 `terminate` 在下一个节点生效（`terminate` 之后两秒内没有到达
  任何节点的程序，连同它的进程一起被结束）。
- 流水线里有 `std::macro` 时，实参全是常量的 macro 调用由该遍在程序开始之前求值并替换为结果：调试器
  不会停在这次求值里，被替换的调用所在行上的断点变为未验证（*这一行只定义常量*）。
- 未经处理就到达 `main` 出口的错误值（`main(): T!`）不是一次停顿：那时已经没有栈帧了。它与不带调试器时
  一样被报告：标准错误上的 `UnhandledError`，退出码 1；想看它在哪里产生，在 `throw` 处设断点。
- `std::inline` 之后，绑定到被内联调用的名字不按名字显示（见第 4 节）。
- 已验证 Windows Release、Linux Debug 协议和 Windows VS Code 界面；macOS 尚未验证。
- Windows Debug 当前遇到 PE DLL 导出符号数量上限，属于构建限制；不能据此把分配断点显示为可用。
