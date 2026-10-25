# 第 1 章 · 五分钟见到图

[English](01_five_minutes.en.md) | 中文 · [返回目录](tutorial.cn.md)

读完这一章你会跑起第一个 Camel 程序，并且看到两件别的语言不会给你看的事：**你的函数是一张图**，
**编译器会改写这张图，而结果不变**。全书要一直带着的问题从现在开始：**这段代码在图上是什么？**

## 为什么是 Camel

大多数语言把程序写成给机器一步步执行的指令；可是编译器、并行运行时、自动微分、GPU 后端真正需要的是一张图，
它要说清要算什么、谁依赖谁。别的语言里，这张图只能事后从代码里恢复出来，恢复出来的图会断开，也会漏掉副作用的
含义。Camel 让**程序本身就是图**：你写下的是计算的意图与依赖，先后顺序只在需要时才写——所以图不用猜，改写图
就是改写你的程序。为什么这门语言把 AI 作为首要的应用场景，写在[为什么是 Camel](../why.cn.md) 里。

## 先拿到 camel

```bash
pip install camel-lang
camel --version
```

装完 `camel` 就在 `PATH` 上，可以开始下面的命令了。想参与这门语言的开发、或者要跟最新的主线，另一条路是从源码
构建：[setup.cn.md](../setup.cn.md)（构建到 `out/latest/bin`）。下面的例子都在仓库里，命令都从仓库根目录执行。

## 第一个程序

[`examples/01_hello.cml`](examples/01_hello.cml)：

```cml
// The smallest program: `main` is the entry, and its body is a sync block.
// Run: camel docs/tutorial/examples/01_hello.cml

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
- 返回类型后面的 **`sync`** 表示这个函数体是**同步块**：它直接写着的语句按书写顺序执行。为什么需要它？
  Camel 只算**被需要**的结果，而"打印出来"这个结果没人要；`sync` 让这个函数体照写下的顺序执行。
  细节留给第 10 章；现在只要记住"要打印的函数，函数体前写 `sync`"。
- 语句之间换行即可，不需要分号。

## 你的函数是一张图

把 `std::rgir` 写在命令行最后：程序不会运行，编译器把它编译出的图**画出来**（DOT 是 Graphviz 的格式，交给 `dot`
就是一张图）。文件名后面写的东西叫**遍**，`std::rgir` 是只负责把图画出来的遍。

```cml
// The smallest program: `main` is the entry, and its body is a sync block.
// Run: camel docs/tutorial/examples/01_hello.cml

func main(): int sync {
    println('Hello, Camel!')
    return 0
}
```

```bash
camel docs/tutorial/examples/01_hello.cml std::rgir > hello.dot
dot -Tsvg hello.dot -o hello.svg
```

![main 的子图，以及入口对它的调用](images/01_hello.svg)

对着源码读这张图（圆是值，菱形是算子或门）：

- 上面那个大框是 **`main` 的子图**。图里没有"第 5 行做了什么"，只有节点和边。
- 写着 `Hello, Camel!` 的圆是字符串常量，写着 `:io/println` 的菱形是 `println` 这个算子，
  两者之间的**数据边**说的是"这条字符串是它的实参"。
- 写着 `0` 的圆是返回值，写着 `GATE` 的菱形是出口的门：`0` 走到门，门的输出就是 `main` 的值。
- `println` 到 `GATE` 的那条边是**同步边**（`std::rgir` 画成虚线、空心箭头）：**同步块在图上是效应之间的同步边**。
  这条边把 `println` 排在出口之前，也就是排在 `return 0` 之前——`sync` 做的正是这件事。把 `sync` 去掉，这条边和
  `println` 就都不在了（第 9、10 章）。
- 最下面那一对节点是入口自己：写着 `CALL` 的菱形是入口对 `main` 的调用，它的输入是 `main` 这张子图。

本书里的图都由脚本从例子生成：[`tools/render-graphs.mjs`](tools/render-graphs.mjs)。生成物在
[`images/`](images/) 下，每张图旁边有一个同名的 `.dot`——图就是从它画出来的；`node
docs/tutorial/tools/render-graphs.mjs --check` 会重画一遍再和入库的图比对，图和例子不会悄悄走散。

## 编译器会改写图

第二个程序多写了一点：同一个乘法写了两遍。[`examples/01_rewrite.cml`](examples/01_rewrite.cml)：

```cml
// The same shape as 01_hello.cml, with one expression that has more than one writing.
// Run: camel docs/tutorial/examples/01_rewrite.cml

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

结果是对的，但这份程序写得比它需要的多。看一下 `twice` 这个函数在图上的样子：左边是它刚编译出来的样子，
右边是加上 `std::opt`（优化遍）之后。

```cml
// The same shape as 01_hello.cml, with one expression that has more than one writing.
// Run: camel docs/tutorial/examples/01_rewrite.cml

func twice(x: int): int {
    return x * 2 + x * 2
}

func main(): int sync {
    println(twice(3))
    return 0
}
```

```bash
camel docs/tutorial/examples/01_rewrite.cml std::rgir
camel docs/tutorial/examples/01_rewrite.cml std::opt std::rgir
```

| `std::opt` 之前 | `std::opt` 之后 |
| --- | --- |
| ![两个乘法加一个加法](images/01_twice_before.svg) | ![一个乘法乘常量 4](images/01_twice_after.svg) |

左边是两个乘法节点加一个加法节点，两个常量都是 `2`；右边只剩一个乘法节点，常量变成 `4`。`x * 2 + x * 2` 不见了：
`2 + 2` 先算成 `4`，两个乘法合成一个。编译器在图这一层把两个乘法合成了一份，**结果不变**——
`camel docs/tutorial/examples/01_rewrite.cml` 仍然打印 `12`。

程序不是一张写着"先做这个、再做那个"的清单，而是一张**谁要谁的结果**的图；既然是图，就可以被改写，
而改写可以发生在你运行它之前。这就是 Camel 与别的语言最不一样的地方，也是这本教程后面每一章都要回到的
那件事。

## 在图上

这一章要带走的一句话：**函数是一张子图，调用是图上的一个节点**；同步块在图上是效应之间的同步边；
优化是把图换成一个更小、结果相同的图。

## 练习

写一个有两个参数的函数 `area(w: int, h: int): int`，返回它们的乘积，并在 `main` 里打印 `area(3, 4)`。
然后把它画出来（`camel docs/tutorial/examples/01_exercise.cml std::rgir > area.dot`，再
`dot -Tsvg area.dot -o area.svg`），在你的图里找到：`w`、`h` 这两个 `PORT` 节点，做乘法的那一个 `OPER` 节点，
以及把这些连起来的两条数据边。

参考答案 [`examples/01_exercise.cml`](examples/01_exercise.cml)：

```cml
// The answer to the exercise of chapter 1: a function of two parameters, and its value.
// Run: camel docs/tutorial/examples/01_exercise.cml

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