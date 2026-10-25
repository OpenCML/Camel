# 第 1 章 · 五分钟见到图

[English](01_five_minutes.en.md) | 中文 · [返回目录](tutorial.cn.md)

读完这一章你会跑起第一个 Camel 程序，并且看到两件别的语言不会给你看的事：**你的函数是一张图**，
**编译器会改写这张图，而结果不变**。全书要一直带着的问题从现在开始：**这段代码在图上是什么？**

先拿到 `camel`：按 [setup.cn.md](../setup.cn.md) 从源码构建，把 `out/latest/bin` 加进 `PATH`，
`camel --version` 能打印版本就可以开始。下面的命令都从仓库根目录执行。

## 第一个程序

[`examples/01_hello.cml`](examples/01_hello.cml)：

```cml
// The smallest program: `main` is the entry, and its body is a sync block.
// Run: camel docs/tutorial/examples/01_hello.cml
module hello

func main(): int sync {
    println('Hello, Camel!')
    return 0
}
```

```bash
camel docs/tutorial/examples/01_hello.cml
```

```text
Hello, Camel!
```

- 一个 `.cml` 文件是一个模块，`module hello` 给它起名（名字可以省略）。
- 程序从 `main` 开始。它返回的 `int` 是进程的退出码，所以这里返回 `0`。
- 返回类型后面的 **`sync`** 表示这个函数体是**同步块**：它直接写着的语句按书写顺序执行。第 10 章讲为什么
  需要它；现在只要记住"要打印的函数，函数体前写 `sync`"。
- 语句之间换行即可，不需要分号。

## 你的函数是一张图

把 `std::gir` 写在命令行最后：程序不会运行，编译器把它编译出的图打印出来。文件名后面写的东西叫**遍**，
`std::gir` 是一个只负责把图打印出来的遍。

<!-- camel-snippet: graph-dump: the text block is std::gir's DOT, not the program's stdout -->

```cml
// The smallest program: `main` is the entry, and its body is a sync block.
// Run: camel docs/tutorial/examples/01_hello.cml
module hello

func main(): int sync {
    println('Hello, Camel!')
    return 0
}
```

```bash
camel docs/tutorial/examples/01_hello.cml std::gir
```

```text
digraph GraphIR {
    subgraph cluster_F01 {
        label="main";
        O01 [label="EXIT", type="EXIT"];
        N00 [label="Hello, Camel!", type="DATA", origin=35, span="5:12-5:27", srcfile="docs/tutorial/examples/01_hello.cml", value_type="string"];
        N01 [label=":io/println", type="OPER", origin=36, span="5:4-5:11", srcfile="docs/tutorial/examples/01_hello.cml", value_type="string"];
        N02 [label="0", type="DATA", origin=37, span="6:11-6:12", srcfile="docs/tutorial/examples/01_hello.cml", value_type="int"];
        N03 [label="GATE", type="GATE", value_type="int"];
        N03 -> O01 [label="0", type="EXIT"];
        N00 -> N01 [label="0|0", type="NORM"];
        N02 -> N03 [label="0|0", type="NORM"];
        N01 -> N03 [label="0|0", type="CTRL"];
    }
    O00 [label="EXIT", type="EXIT"];
    N04 [label="Function(graph=main, tupleSlots=0)", type="DATA", origin=33, span="4:0-7:1", srcfile="docs/tutorial/examples/01_hello.cml", value_type="() => int"];
    N05 [label="CALL", type="CALL", value_type="int"];
    N05 -> O00 [label="0", type="EXIT"];
    N04 -> N05 [label="0|0", type="WITH"];
}
```

对着源码读这张图：

- `cluster_F01` 是 **`main` 的子图**。图里没有"第 5 行做了什么"，只有节点和边。
- `N00` 是字符串常量，`N01` 是 `:io/println` 这个算子，`N00 -> N01` 这条**数据边**说的是"这条字符串是它的
  实参"。
- `N02` 是 `0`，`N03` 是出口的门 `GATE`：`N02 -> N03` 说明 `main` 的值是 `0`。
- `N01 -> N03` 这条边的类型是 **`CTRL`**：**同步块在图上是效应之间的同步边**。`println` 是效应，
  这条边把它排在出口之前，也就是排在 `return 0` 之前——`sync` 做的正是这件事。把 `sync` 去掉，
  这条边和 `println` 就都不在了（第 9、10 章）。
- 最外面那三行是入口自己：`Function(graph=main)` 是 `main` 这张子图，`CALL` 是入口对它的调用，
  `WITH` 边把两者连起来。

## 编译器会改写图

第二个程序多写了一点：同一个乘法写了两遍。[`examples/01_rewrite.cml`](examples/01_rewrite.cml)：

```cml
// The same shape as 01_hello.cml, with one expression that has more than one writing.
// Run: camel docs/tutorial/examples/01_rewrite.cml
module rewrite_hook

func twice(x: int): int {
    return x * 2 + x * 2
}

func main(): int sync {
    println(twice(3))
    return 0
}
```

```bash
camel docs/tutorial/examples/01_rewrite.cml
```

```text
12
```

结果是对的，但这份程序写得比它需要的多。加上 `std::opt`（优化遍）再看一次图：

<!-- camel-snippet: graph-dump: the text block is one excerpt of std::opt std::gir's DOT, not the program's stdout -->

```cml
// The same shape as 01_hello.cml, with one expression that has more than one writing.
// Run: camel docs/tutorial/examples/01_rewrite.cml
module rewrite_hook

func twice(x: int): int {
    return x * 2 + x * 2
}

func main(): int sync {
    println(twice(3))
    return 0
}
```

```bash
camel docs/tutorial/examples/01_rewrite.cml std::opt std::gir
```

```text
        subgraph cluster_F02 {
            label="twice";
            F02 [label="ARGS", type="ARGS"];
            O02 [label="EXIT", type="EXIT"];
            N00 [label="x", type="PORT", param_name="x", param_kind="normal", param_index="0", value_type="int"];
            N01 [label="4", type="DATA", value_type="int"];
            N02 [label=":op/mul_l", type="OPER", origin=92, span="5:17-5:18", srcfile="docs/tutorial/examples/01_rewrite.cml", value_type="int"];
            F02 -> N00 [label="0", type="NORM_PORT"];
            N02 -> O02 [label="0", type="EXIT"];
            N00 -> N02 [label="0|0", type="NORM"];
            N01 -> N02 [label="0|1", type="NORM"];
        }
```

上面是那份输出里 `main` 中 `twice` 这一段（节选）。`x * 2 + x * 2` 不见了：现在只有一个乘法节点和一个
常量 `4`。编译器在图这一层把两个乘法合成了一份，**结果不变**——`camel docs/tutorial/examples/01_rewrite.cml`
仍然打印 `12`。

程序不是一张写着"先做这个、再做那个"的清单，而是一张**谁要谁的结果**的图；既然是图，就可以被改写，
而改写可以发生在你运行它之前。这就是 Camel 与别的语言最不一样的地方，也是这本教程后面每一章都要回到的
那件事。

## 在图上

这一章要带走的一句话：**函数是一张子图，调用是图上的一个节点**；同步块在图上是效应之间的同步边；
优化是把图换成一个更小、结果相同的图。

## 练习

写一个有两个参数的函数 `area(w: int, h: int): int`，返回它们的乘积，并在 `main` 里打印 `area(3, 4)`。
然后加上 `std::gir`，在你的图里找到：`w`、`h` 这两个 `PORT` 节点，做乘法的那一个 `OPER` 节点，
以及把这些连起来的两条数据边。

参考答案 [`examples/01_exercise.cml`](examples/01_exercise.cml)：

```cml
// The answer to the exercise of chapter 1: a function of two parameters, and its value.
// Run: camel docs/tutorial/examples/01_exercise.cml
module exercise

func area(w: int, h: int): int {
    return w * h
}

func main(): int sync {
    println(area(3, 4))
    return 0
}
```

```bash
camel docs/tutorial/examples/01_exercise.cml
```

```text
12
```

## 参考

- [语言参考 1 绪论](../language/01_introduction.md)
- [语言参考 4.1 程序与入口](../language/04_execution_model.md#41-程序与入口)
- [命令行：遍与流水线](../cli.cn.md)