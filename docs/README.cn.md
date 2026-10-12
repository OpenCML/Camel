# Camel：图原生的编程语言

[English](../README.md) | 中文简体

[![许可证：MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
![WIP](https://img.shields.io/badge/status-WIP-yellow.svg)

<p align="center">
  <img src='https://www.github.com/OpenCML/ImagesHosting/raw/main/GetDefaultLogger()o_text.png' width=800>
</p>

Camel 是**图原生的通用编程语言**，AI 是它首要的应用领域。

Camel 的程序只表达计算意图：要算什么，谁依赖谁。图不是从别的语言的代码里捕获或恢复出来的，源码写下的就是图。语言里的每个元素直接对应图上的一块结构；编译、优化、求导、导出、执行都是对这张图的变换，称为遍，由你在命令行上组合。

## 语言与图

| 源码里 | 在图上 | 由此得到 |
|---|---|---|
| 表达式、算子调用 | 节点与数据边 | 按依赖求值；互不依赖的节点可以乱序或并行 |
| 函数 | 子图；调用是一个节点 | 函数的行为由它自己决定 |
| `sync` 块 | 相邻表达式之间的控制边 | 语句按书写顺序执行 |
| `wait e` | 一个表达式前后的控制边 | 修改变量之处局部有序 |
| `if`、`match`、`a ?? b`、`a !! b` | 分支 | 错误与空值都是值，用分支处理 |
| 泛型函数 | 子图模板，按每组具体类型实例化 | 图上只有具体类型 |
| 遍 | 把一张图变成另一张图（或消费一张图）的程序 | 开放的流水线 |

这张表背后的规则见 [设计原则](design-principles.cn.md)。它是规范性的：与实现或其他文档冲突时以它为准。

## 先看一眼

```camel
module stats

func mean(xs: float[]): float {
    return xs->reduce<(acc: float, x: float): float => acc + x, 0.0> / float(len(xs))
}

func parse_all(texts: string[]): int[] {
    return texts->map<(t: string): int => parse_int(t) !! 0>   // 不是数字的文本记为 0
}

func main(): int sync {
    let xs = parse_all(['3', '4', 'five', '6'])
    println(xs)
    println('mean = {:.2f}'->format<mean(xs->map<(x: int): float => float(x)>)>)
    return 0
}
```

```text
[3, 4, 0, 6]
mean = 3.25
```

- `->` 把一个值送进函数；传给 `map`、`reduce` 的函数写在尖括号里，因为它是对算子的配置，数组才是流过算子的数据。
- `parse_int` 返回 `int!`：要么是一个 `int`，要么是一个错误；`!!` 给出替代值。没有异常。
- `main` 的函数体是 `sync` 块，其中的语句按书写顺序执行。别处按依赖求值：在同步块之外，结果没有被使用的效应不执行（编译器会给出警告）。
- 目前没有循环语法；重复的计算写成递归（尾调用不占栈）和高阶算子 `map`、`filter`、`reduce`、`foreach`。

## 一份源码，不同的遍

命令行上文件名之后写的，就是要依次运行的遍：

```bash
camel prog.cml                          # 在节点虚拟机上运行（默认）
camel prog.cml std::fvm                 # 在字节码虚拟机上运行
camel prog.cml std::jit                 # 带即时编译运行
camel prog.cml std::prl                 # 并行调度
camel prog.cml std::opt std::fvm        # 先优化，再运行
camel prog.cml std::rgir                # 不运行，打印图（Graphviz DOT）
camel prog.cml std::macro std::opt::simplify onnx::export    # 把 main 转译为 ONNX 模型
```

自动微分（`autodiff` 模块的 `grad`）与 ONNX 导出是建立在同一套遍机制之上的库。[入门教程](tutorial/tutorial.cn.md)会带你看图、换流水线、导出一个小模型、求一次梯度。

## 入门

> [!WARNING]
> Camel 仍在开发中。语言与命令行还会变化，文法里有一部分写法保留而尚未实现；[语言参考](language/README.md)逐项写明了哪些是这样。

### 构建

Camel 从源码构建，工具链见 [setup.cn.md](setup.cn.md)。

```bash
npm install
npm run build        # 可执行文件在 out/latest/bin/camel
```

把 `out/latest/bin` 加入 `PATH`（并把 `CAMEL_HOME` 设为 `out/latest`）。同一棵源码树还可以打出名为 `camel-lang` 的 Python wheel，见 [pypi-packaging.md](pypi-packaging.md)。

### Hello, Camel

```camel
// hello.cml
module hello

func main(): int sync {
    println('Hello, Camel!')
    return 0
}
```

```bash
camel hello.cml
```

```text
Hello, Camel!
```

## 了解更多

- [入门教程](tutorial/tutorial.cn.md)（[English](tutorial/tutorial.en.md)）：从第一个程序到看图、换遍、导出与求梯度，例子都可以运行
- [示例集](../examples/README.cn.md)：按任务组织的短程序（JSON、文件、映射、排序、命令行工具、并行的 `map`、仿真、训练步）
- [语言参考](language/README.md)（[English](language/en/README.md)）
- [命令行](cli.cn.md)与[诊断目录](diagnostics.md)（`camel --explain <名字>`）
- [设计原则](design-principles.cn.md)（[English](design-principles.en.md)）：Camel 是什么，为什么是这样
- [技术文档](technical/README.md)：编译流水线、图的中间表示、运行时
- [从源码构建](setup.cn.md)与 [CMake 构建选项](build-options.cn.md)
- [Python 集成](../modules/python/README.md)：通过 `python` 模块嵌入 Python
- [路线图](../ROADMAP.md)

## 贡献

- [贡献指南](CONTRIBUTING.cn.md)
- [行为准则](CODE_OF_CONDUCT.cn.md)
- [安全政策](SECURITY.cn.md)
- [问题追踪](https://github.com/OpenCML/Camel/issues)

## 许可证

本项目贡献者编写的代码以 [MIT 许可证](../LICENSE)授权。

来自 antlr4 的第三方代码以 BSD-3-Clause 许可证授权。
