# 调试 Camel 程序

中文简体 | [English](debugging.en.md)

`camel-db` 是 Camel 的调试器。本文讲通过
[Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/)（DAP）进行的源码级调试：
源码行断点、单步、调用栈、变量。任何带 DAP 客户端的编辑器都能用；[`editors/vscode`](../editors/vscode)
里的 VS Code 扩展已经配置好。

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
    "stepBy": "line"
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

断点照常在行号旁设置。*单步跳过*、*单步调试*、*单步跳出*、*暂停*、*继续*、*停止*，*调用堆栈*、*变量*、
*监视* 面板以及把鼠标悬停在名字上，都走标准协议。*断点* 面板里有两项针对故障：
**Faults that end the program**（使程序终止的故障，默认开启）与 **Faults isolated by attempt**
（被 `attempt` 隔离的故障）。

## 2. 在命令行里

`camel-db --dap` 在标准输入输出上讲 DAP，所以通常由编辑器启动。不用编辑器想试一次会话，仓库里有一个
小的脚本客户端：

```bash
node test/tools/debugger/dap_session.mjs test/cases/tools/debugger/fixtures/recursion.cml \
     --passes std::fvm b:8 go stack vars c stack vars
```

`b:<行号>` 设断点，`go` 启动，`c` 继续，`n` / `s` / `o` 是跳过、进入、跳出，`ni` / `si` 按图节点单步，
`stack`、`vars`、`nodes`、`e:<名字>` 查看；脚本开头列出了全部步骤。上面这次会话，断点设在一个尾递归函数里：

```text
-> setBreakpoints {"source":{"path":".../recursion.cml"},"breakpoints":[{"line":8}]}
   line 8: verified
-> configurationDone
   (node %18 OPER :op/eq_l)
<- stopped (breakpoint) in loop at recursion.cml:8:17
-> stackTrace {"threadId":1}
   #11 loop  recursion.cml:8
   #2 main  recursion.cml:13
   #1 __root__  recursion.cml:11
-> scopes {"frameId":11}
   [Locals]
     n: int = 5
     acc: int = 0
-> continue {"threadId":1}
   (node %18 OPER :op/eq_l)
<- stopped (breakpoint) in loop (tail call, 2 frames replaced) at recursion.cml:8:17
-> stackTrace {"threadId":1}
   #13 loop (tail call, 2 frames replaced)  recursion.cml:8
   #2 main  recursion.cml:13
   #1 __root__  recursion.cml:11
-> scopes {"frameId":13}
   [Locals]
     n: int = 4
     acc: int = 5
```

其他编辑器：把 DAP 客户端配置为运行 `camel-db --dap`，`launch` 请求带上表中的属性。

`camel-db` 另有一个 Web 界面，带图视图与内存查看，见
[`tools/debugger/README.md`](../tools/debugger/README.md)。

## 3. 你看到的是什么，为什么

### 3.1 程序停在一个图节点之前

执行就是图节点的执行：一个算子、一次调用、一个分支。调试器停在*某个节点运行之前*，显示该节点的源码位置，
所以高亮的位置是一行**加一列**：在 `let c = a + b` 里是那个 `+`。

一行可以有多个节点，也可以一个都没有：

- **没有节点的行**放不了断点。断点显示为未验证，消息说明是哪种情况：这一行没有代码；或者代码
  *不会执行，因为没有任何东西依赖它的结果*；或者被某个遍删掉了。第二种情况正是 Camel 的执行模型。在

  ```camel
  func next(x: int): int {
      let unused = x + 100     // 没人用它，函数体不是同步块：永远不执行
      return x + 1
  }
  ```

  里，`+ 100` 不在运行的图中；同一位置如果写的是 `println(x)`，也不会打印。要让它执行，把代码放进
  同步块（`func next(x: int): int sync { ... }`），或者使用它的结果。
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

调试器从不改变程序的执行顺序，也从不为了显示而去求值。

### 3.3 调用栈是子图的调用链

函数是子图；一帧是一张图的一次激活。

- 最底下的 **`__root__`** 是入口图，它唯一的工作是调用 `main`。
- **尾调用替换栈帧。** 以调用结尾的函数不再返回自身，两个后端都复用它的帧。栈如实显示现有的东西：
  一个尾递归循环不论跑了多久都只有*一*帧，并标出它替换过多少帧：`loop (tail call, 4 frames replaced)`。
  不虚构栈帧。
- **分支的臂是独立的子图。** `if c then f(x) else g(x)` 编译成一个分支节点和两张小图。停在臂里时显示为
  `pick [arm at line 9]`；分支是函数最后一件事时，臂替换了函数的帧
  （`... (tail call, 1 frame replaced)`）。臂能看到它所在函数的名字。没被选中的臂不执行：里面的断点有效，
  但永远不会命中。
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

作用域：**Locals**（参数与函数里声明的名字）、**Captured**（lambda 捕获的名字）、**Inlined**
（内联进这张图的其他函数的名字）、**Nodes**（按编号、种类、标签列出图的每个值节点：这一帧的图视图）。

**监视与悬停**接受一个名字，后面可以跟成员访问：`p.x`、`t.0`、`xs[2]`。其他表达式会被拒绝：求值意味着
在停住的程序里构造并运行一张图。

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
- 未验证：Windows（适配器的进程处理已移植但未测试）、macOS，以及 VS Code 界面本身的表现。
