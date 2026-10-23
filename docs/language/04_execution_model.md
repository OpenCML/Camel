# 4. 执行模型

[English](en/04_execution_model.md) | 中文

> 本章的规则来自 [设计原则](../design-principles.cn.md) 第 2–4 节，冲突时以它为准。

Camel 的程序是一张图。本章说明这张图怎样被求值：按依赖求值、`sync`、`var` 与 `wait`、`macro`、故障与退出、遍。

| 语言元素 | 在图上是什么 |
|---|---|
| 表达式、算子调用 | 节点与数据边；只有从函数出口沿边可达的节点才会执行 |
| 函数 | 属于它自己的一张子图；调用处是外层图里指向该子图的一个节点 |
| `sync { ... }` 块 | 它直接写着的相邻语句之间的控制边 |
| `sync func`（函数修饰符） | 函数类型上的标记；不产生函数体内的任何边 |
| `var` 形参 | 被调子图与调用方指向同一个对象 |
| `wait e` | `e` 与前后使用其值或被它修改的变量的操作之间的控制边 |
| `if`、`match`、`??`、`!!`、`?->` | 分支节点，每个臂是一张子图 |
| `#macro` 函数 | 普通子图；`std::macro` 遍可以提前求值，把结果物化为图中的常量 |

---

## 4.1 程序与入口

- 每个源文件是一个模块，由声明组成（[8.1](08_top_level_and_grammar.md#81-程序)）。
- 入口是名为 `main` 的函数。它与别的函数一样是一张子图，可以带参数、有返回值：

  - 参数是程序的输入，在命令行上用 `--arg 名字=值` 给出；缺少时是带位置的错误 `RuntimeDiag::EntryArgumentMissing`，不会用零或默认值代替。值按参数声明的类型读取，整数与浮点按声明的**宽度**读取：`i32` 是 int32、`f32` 是 float，装不下的值被拒绝并说明是哪种类型及其范围（`i32`、`f32`），能装下的值在那个宽度上只转换一次。复数写成语言里的复数文字（`1+2i`、`0.5-0.25i`，或只有虚部的 `-2i`），实数就是实部、虚部为 0 的复数（`1.5` 是 `1.5+0i`，与 `let z: c128 = 1.5` 相同：字面量按上下文取类型，设计原则 5.1）。
  - **只有写明了返回类型的 `main`，它的返回值才算程序的结果**（退出码，以及 `-o` 写出的内容）。
  - 返回 `int`（或 `bool`）时，返回值是进程的退出码。
  - 返回其他类型的值（`float`、`string`、张量、元组……）时，这个值是程序的结果：打印到标准输出，或由 `-o <文件>` 写入文件。
  - 返回 `void` 时退出码是 0。不写返回类型的 `main` 没有结果：函数体仍然有值（[7.4.1](07_statements.md#741-声明)，例如以 `println('a')` 结尾时是 `'a'`，因为 `println(x)` 返回 `x`），但这个值既不打印、不写出，也不当作退出码，退出码是 0。
  - 返回类型是 `T!` 时：返回的是成功值，就按 `T` 处理；返回的是**没有被处理的错误值**，它作为诊断 `RuntimeDiag::UnhandledError` 打印到**标准错误**，位置是错误值里记录的产生处，退出码为 1（见 [4.6](#46-故障退出与资源上限)）。

```cml
// camel greet.cml --arg name=Camel --arg times=2
func main(name: string, times: int): int sync {
    println(`Hello, ${name}! x${times}`)
    return 0
}
```

```text
Hello, Camel! x2
```

详细规则见 [cli.cn.md](../cli.cn.md)“`main` 带参数或有返回值的程序”。

## 4.2 按依赖求值

求值由依赖驱动，没有固定的执行顺序：一个表达式被求值，当且仅当它的结果被需要，即经数据边或控制边到达函数的返回。互不依赖的表达式之间没有先后，可以乱序或并行。

**同步块之外、结果没有被使用的带副作用的调用不会执行。** 这是按依赖求值的自然结果，是合法的，不是错误。编译器给出警告 `SemanticDiag::EffectNotExecuted`，以免漏写 `sync`：

```cml
func greet(): int {
    println('hello from greet')     // 结果没人用，也不在同步块里：不执行
    return 1
}

func main(): int sync {
    println(greet())
    return 0
}
```

```text
warning[SemanticDiag::EffectNotExecuted]: The call of 'println' has side effects but is never executed: nothing depends on it and it is not in a sync block.
  --> greet.cml:2:5
   |
 2 |     println('hello from greet')     // 结果没人用，也不在同步块里：不执行
   |     ^^^^^^^
help: Evaluation is driven by dependencies: outside a sync block an expression whose result (or the
      variable it writes) nothing needs does not run. This is legal. To run it, put it in a sync
      block ('sync { ... }', or a function body 'func f(): T sync { ... }'), or use its result.

1
```

警告输出到标准错误，程序照常运行。用 `-Wno-effect-not-executed` 关闭，用 `-Werror` 把警告当作错误（见 [cli.cn.md](../cli.cn.md)“诊断”）。

## 4.3 `sync`

`sync` 出现在两个位置，含义不同。

### 4.3.1 同步块（块修饰符）

写在块前的 `sync` 表示同步块：**它自己直接写着的语句**按书写顺序执行，相当于编译器在相邻的语句之间插入控制边。在同步块里调用什么、有没有副作用都无所谓，因为已经有顺序了。

- 作函数体：`func f(...): T sync { ... }`（`sync` 在返回类型之后）。
- 作分支臂或 lambda 体：`if c then sync { ... }`、`(x: int) => sync { ... }`。
- 单独的块语句 `sync { ... }`。注意它本身也是一个表达式：写在非同步的函数体里而值没有被使用时，整个块不执行。
- **同步块不向内继承**（[设计原则 3.4](../design-principles.cn.md#34-sync-块)）：同步块只管它自己直接写着的语句，写在里面的分支臂、循环体与嵌套块按它们自己的写法执行，要同步就在那里写 `sync`——一个块的含义不随它写在哪里而变。下面这个 `if` 本身在同步链上，它的臂不是：臂按依赖求值，值没有被用到的 `println(1)` 不执行，`println(2)` 是臂的值、也就是 `if` 的值，所以只有它执行。要两句都打印，写 `if c then sync { ... }`：

```cml
func f(c: bool): int sync {
    if c then { println(1); println(2) }   // 臂不是同步块：只有 println(2) 执行
    return 0
}
```

```cml
func greet(): int sync {
    println('hello from greet')     // 同步块：执行，并且先于 return
    return 1
}
```

### 4.3.2 函数的行为由它自己决定

函数编译成它自己的子图。调用只是把这个子图节点放进更大的图里，不改变它的结构。所以从同步块里调用一个函数体不是同步块的函数，函数体内仍按依赖求值：

```cml
func log(x: int): int {
    println(x)          // 结果没人用，函数体不是同步块：不执行
    return x + 1
}

func main(): int sync {
    let a = log(1)
    let b = log(a)
    println(b)
    return 0
}
```

```text
3
```

（另有一条 `SemanticDiag::EffectNotExecuted` 警告指向 `println(x)`。）`main` 的函数体是同步块，两次 `log` 调用按顺序执行；但 `println(x)` 属于 `log` 自己的函数体。要打印，把 `log` 写成 `func log(x: int): int sync { ... }`。

### 4.3.3 `sync func`（函数修饰符）

写在 `func` 之前的 `sync` 修饰的是函数：声明它有副作用、需要被同步，调用方可能需要 `wait` 它。

- 带 `var` 形参的函数、写入被捕获变量（包括模块顶层变量）的函数应当这样声明；没有声明时编译器给出警告 `SemanticDiag::VarParamInAsyncFunction`、`SemanticDiag::CapturedWriteInAsyncFunction`。lambda 的写法是 `sync (x: int) => ...`。
- **它不会使函数体成为同步块。** 要让函数体按顺序执行，两者并用：

```cml
sync func f(): int {
    println('f')        // 仍然不执行
    return 1
}

sync func g(): int sync {
    println('g')        // 执行：函数体是同步块
    return 2
}

func main(): int sync {
    println(f() + g())
    return 0
}
```

```text
g
3
```

目前这个修饰符只是声明：编译器检查“有副作用的函数是否声明了它”，不依据它在调用处产生顺序。调用处应当如何对待它尚待确定。

## 4.4 `var`、引用与 `wait`

- 一切值按引用传递。`var` 形参是引用，函数对它的写入就是对调用方变量的写入；同一个变量传给两个 `var` 形参时，两个形参是同一个对象的别名。
- 赋值是带副作用的操作。**副作用的声明是一个闭环**：要修改一个参数，它必须声明为 `var`；可变性从最底层的算子声明一路向上，每一层都显式声明。没有把参数声明为 `var` 的函数不可能修改它。
- 把 `let` 传给 `var` 形参、在函数体内对非 `var` 的形参赋值，都是编译期错误（`SemanticDiag::ImmutablePassedToVarParam`）。

**`wait` 作用于它后面的那一个表达式。** `wait` 前后的操作，如果用到了该表达式的值，或用到了被它修改的变量，就与它局部有序：

```cml
sync func bump(var counter: int) {
    wait counter += 1
}

// 函数体不是同步块：顺序来自 wait 与数据依赖
func twice(): int {
    var c = 0
    wait bump(c)
    wait bump(c)
    return c
}

func main(): int sync {
    println(twice())
    return 0
}
```

```text
2
```

- `wait` 不是机械的排序，它依靠 `var` 的使用情况来排序。被排在修改之前的，是此前**直接使用**了该变量的值的那些操作：`A(i + 1); wait modify(i)` 中直接使用 `i` 的是 `+`，所以 `modify` 排在 `+` 之后，而不保证排在 `A` 之后。一次被等待的写入排在此后对该变量的读取之前。
- 被 `wait` 的表达式如果不涉及任何 `var`（例如只有输入输出），`wait` 没有可依据的东西，也就没有意义；这类调用写在同步块里。
- 在同步块里写不写 `wait` 都一样。
- `wait modify(i)` 之后如果没有任何地方再读 `i`，这次修改不执行（按依赖求值）。
- 不带 `wait`、也不在同步块里的写入，与此前的读取之间没有顺序；编译器给出警告 `SemanticDiag::IgnoredSideEffect`。
- 修改的追踪覆盖捕获：函数通过捕获写入一个 `var`，调用处记为对该变量的一次写入。通过函数值（作为实参、返回值或存起来的 lambda）进行的调用，调用处只能按它的类型（`sync` 修饰符、`var` 形参）识别副作用。
- `wait` 可以写在调用、赋值与运算符表达式之前。写在块、名字、字面量、lambda 之前保留，尚未实现（`SemanticDiag::ConstructNotSupported`）；要让一个块按顺序执行，用 `sync { ... }`。

副作用的管理与追踪仍在研究中（设计原则 3.5）。同一个可变对象在同步块之外被多处写入、且都没有 `wait` 时的行为尚未决定。

## 4.5 `#macro` 函数

`#macro` 是标准标签（[6.2](../design-principles.cn.md)、[8](../design-principles.cn.md)），写在函数声明前面，不用导入：

```cml
#macro func cube(x: int): int { return x * x * x }

func main(): int sync {
    println(cube(3))
    return 0
}
```

`camel m.cml` 与 `camel m.cml std::macro std::nvm` 都打印 `27`；后者的图里 `cube(3)` 已经是常量 `27`（用 `camel m.cml std::macro std::rgir` 可以看到）。

`#macro` 只是声明上的标记，不是函数类型的一部分：函数类型仍然和普通函数一样。它只能写在函数声明上；写在类型、字段、参数或块上是标签位置错误，写成 `#macro<...>` 是标签参数个数错误。lambda 没有声明可附，不能写 `#macro`。

- 标签本身不定义阶段，也不是必需的：流水线里没有 `std::macro` 时，macro 函数就是普通函数。
- 只有实参全是常量（或另一个 macro 调用的结果）的调用才被提前求值；否则调用留到运行时。
- macro 函数体里的副作用与普通函数一样处理：在求值的时候发生。
- 求值按轮进行：一轮的结果成为下一轮的常量，直到没有调用可以再提前求值；因此一轮中得到的新的 macro 函数值可以继续被展开。
- 提前求值失败（算子报错、超过求值深度上限，默认 64 层，`--max-macro-depth` 可调）是运行时故障，与任何遍运行中的故障相同：阶段是 `pass:std::macro`，位置在调用处，程序不运行，求值已经产生的副作用照常留下。求值中的循环至多 1000000 轮（`--max-macro-rounds` 可调），超过同样是这个故障。
- 自动微分的 `grad` 等是 macro 算子，这是算子声明的事实，不是 `#macro` 标签；使用它们需要流水线里有 `std::macro`（教程第 16 节）。这是该库的前提，不是语言的约束。

## 4.6 故障、退出与资源上限

**故障**是程序无法继续的情形：除数为零、下标越界、`match` 没有分支匹配、递归过深、数字文本无法转换等。故障使程序带源码位置终止，退出码为 1；它之前已经产生的输出照常写出。故障不是错误值，`!!` 接不住它。

```cml
func main(): int sync {
    let xs = [1, 2, 3]
    println('before')
    println(xs[5])
    println('after')
    return 0
}
```

```text
before
error[RuntimeDiag::IndexOutOfRange]: Index 5 is out of range for an array of length 3.
  --> fault.cml:4:15
   |
 4 |     println(xs[5])
   |               ^
help: Ensure the index is within the valid range of the collection.
```

把故障隔离在子图边界上的算子是 `attempt`（见 [6.10.1](06_expressions.md)）。

**`exit(n)`** 立即结束程序，退出码为 `n`；此前的输出照常写出。

**递归深度。** 尾位置的调用不占用栈帧，尾递归可以任意深。非尾递归同时打开的调用数有上限，默认 1000，超过是故障 `RuntimeDiag::MaxRecursionDepthExceeded`；用 `--max-recursion-depth <n>` 或环境变量 `CAMEL_MAX_RECURSION_DEPTH` 调整。计数包括通过函数值的调用和高阶算子对每个元素的调用。默认值在各执行后端上相同，并行调度器同样计数。

```cml
func depth(n: int): int {
    return if n == 0 then 0 else 1 + depth(n - 1)     // 非尾递归
}

func count(n: int, acc: int): int {
    return if n == 0 then acc else count(n - 1, acc + 1)   // 尾递归
}

func main(): int sync {
    println(count(1000000, 0))
    println(depth(200))
    println(depth(1200))
    return 0
}
```

```text
1000000
200
error[RuntimeDiag::MaxRecursionDepthExceeded]: Maximum recursion depth (1000) exceeded when evaluating graph '__g_7__'.
```

（其后是位置与建议。）

**其他上限**：单张图的节点、块与边表项各不超过 65,535；图空间、永久空间、元空间三块内存各 256 MB，分别由 `CAMEL_GRAPH_SPACE_LIMIT_MB`、`CAMEL_PERM_SPACE_LIMIT_MB`、`CAMEL_META_SPACE_LIMIT_MB` 调整，耗尽时是 `RuntimeDiag::MemoryExhausted`。完整的表见 [cli.cn.md](../cli.cn.md)“资源上限”。

## 4.7 遍与流水线

程序由命令行上的遍序列处理：`camel file.cml pass1 pass2 ...`。每个遍变换图、执行图（调度遍）或把图翻译成别的东西（转译遍）。

| 遍 | 作用 |
|---|---|
| （不写） | `std::default`，即 `std::nvm` |
| `std::nvm` | 节点虚拟机：直接在图上解释执行 |
| `std::fvm` | 字节码虚拟机 |
| `std::jit` | 带即时编译的字节码虚拟机；不能编译的图由解释器执行 |
| `std::prl` | 并行调度：互不依赖的节点可以并发执行 |
| `std::macro` | 提前求值 macro 调用 |
| `std::opt`、`std::opt::inline`、`std::opt::fold`、`std::opt::cse`、`std::opt::dce`、`std::opt` | 优化 |
| `std::rgir`、`std::gir` | 打印图（DOT 格式），不运行 |
| `std::stats` | 打印图的统计数字，图原样传下去 |
| `onnx::export`、`std::cpp` | 转译为 ONNX 模型、C++ 源码 |

- **语言不限制遍的顺序，顺序可以影响结果。** 遍会修改图，可以有副作用；“任意遍序列下行为相同”不是语言的性质。
- 调度遍与转译遍消费掉图，写在它们之后的遍不会运行（有警告 `RuntimeDiag::PassesNeverRun`）。指定的遍都没有消费掉图时，接着运行默认遍。
- 源码层的规则（`var`/`let`、`wait`、`sync`）约束的是编译器怎样构造图；图一旦得到，这些规则不再约束后续的遍。标准库的优化遍以不改变程序结果为目标，这是对这些遍的质量要求，由测试保证。
- 命令行上写某个模块的遍（如 `onnx::export`）时，编译器自动加载该模块。

## 4.8 高阶算子的顺序

- `map`、`filter` 的各个元素之间没有执行顺序的约定：并行调度（`std::prl`）可以并发执行它们，结果按元素位置排列。同步块不改变这一点。元素函数里的副作用在并行的 `map` 下顺序不作保证。
- `foreach`、`reduce` 按元素顺序执行。它们的无序并行版本是 `para` 模块的 `unordered_foreach`、`unordered_reduce`。
