# Camel：图原生的编程语言

[English](../README.md) | 中文简体

[![许可证：MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
![WIP](https://img.shields.io/badge/status-WIP-yellow.svg)

<p align="center">
  <img src='https://www.github.com/OpenCML/ImagesHosting/raw/main/GetDefaultLogger()o_text.png' width=800>
</p>

Camel 是一门图原生的通用编程语言，AI 是它首要的应用场景。

大多数语言把程序写成给机器一步步执行的指令。可是编译器、并行运行时、自动微分、GPU 后端真正需要的是一张图：要算什么，谁依赖谁。于是图只能从代码里事后恢复，靠追踪、捕获、分析。恢复出来的图会断开，会漏掉副作用的含义，作用在它上面的变换也成了用户碰不到的黑箱。

Camel 让**程序本身就是图**。你写下的是计算的意图与依赖，先后顺序只在需要时才写。由此：

- **编译器拿到的就是你写的东西**：不用追踪，图也不会断开；互不依赖的计算天然可以并行。
- **变换是普通的函数**：优化、求导、上 GPU、导出都是作用在图上的遍，你能看、能换，也能用 Camel 自己写。
- **优化的知识可以写成代码并加以验证**：一条代数律就是一条规则，`check` 检查它对不对。
- **一份程序，多种执行**：解释、JIT、GPU，或导出为 ONNX、C++，意思不变。

这些性质对所有"计算比控制流更重要"的程序都成立。训练与部署模型是其中最迫切的一类，所以 AI 是 Camel 首要的应用场景，但不是唯一的。

## 语言与图

| 源码里 | 在图上 | 由此得到 |
|---|---|---|
| 表达式、算子调用 | 节点与数据边 | 按依赖求值；互不依赖的节点可以乱序或并行 |
| 函数 | 子图；调用是一个节点 | 函数的行为由它自己决定 |
| `sync` 块 | 它直接写着的相邻语句之间的控制边 | 这些语句按书写顺序执行（不向内继承） |
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
- `main` 的函数体是 `sync` 块，它直接写着的语句按书写顺序执行（写在里面的臂、循环体与嵌套块按各自的写法）。别处按依赖求值：在同步块之外，结果没有被使用的效应不执行（编译器会给出警告）。
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
camel prog.cml std::macro std::opt 'onnx::export out="model.onnx"'   # 把 `main` 导出为 ONNX 模型
```

自动微分（`autodiff` 模块的 `grad`）与 ONNX 导出是建立在同一套遍机制之上的库。[入门教程](tutorial/tutorial.cn.md)会带你看图、换流水线、导出一个小模型、求一次梯度。

## 入门

> [!WARNING]
> Camel 仍在开发中。语言与命令行还会变化，文法里有一部分写法保留而尚未实现；[语言参考](language/README.md)逐项写明了哪些是这样。

### 构建

Camel 从源码构建；各平台的工具链见 [setup.cn.md](setup.cn.md)。构建需要 Clang 19 或更高、CMake 3.25 或更高、Ninja、Conan 2.10 或更高，脚本还需要 Node.js 18 或更高。第一次构建会执行 `conan install` 下载 C++ 依赖，因此需要网络。

```bash
npm install          # 安装 npm 包，并执行 conan install
npm run build        # Release 构建；可执行文件落在 out/latest/bin
```

Linux 与 macOS 上可执行文件是 `out/latest/bin/camel`，Windows 上是 `out/latest/bin/camel.exe`。把 `out/latest/bin` 加入 `PATH`，并把 `CAMEL_HOME` 设为 `out/latest`（包含 `bin/`、`libs/`、`include/` 与 `stdlib/` 的目录）。发布 CI 构建并测试 Linux x64 与 Windows x64；macOS arm64 是手动选择的试验任务，因此 macOS 未经 CI 验证。

只有找到带开发头文件的 CPython 时才会构建 `python` 与 `pyplot` 模块；没有就跳过，`CAMEL_SKIP_PYTHON=1` 可显式跳过。同一棵源码树还可以用 `npm run pypi:rollup` 打出名为 `camel-lang` 的 Python wheel，见 [pypi-packaging.md](pypi-packaging.md)。

### VS Code

推荐安装 [OpenCML-Ext 插件](https://marketplace.visualstudio.com/items?itemName=Kogler7.open-cml-extension)，在 VS Code 中使用语法高亮、语言服务、交互式 GIR 浏览和调试。将 `opencmlLanguageServer.camelPath` 设为构建得到的 `camel` 可执行文件路径，插件会在同一目录查找 `camel-ls` 和 `camel-db`。较新的运行时还支持逐遍调试、查看暂停时的 GIR 和内存。配置方法见[插件 README](https://github.com/OpenCML/OpenCML-Ext#readme)，运行时能力见[调试指南](debugging.cn.md)。

### Hello, Camel

```camel
// greeting.cml
module hello

func main(): int sync {
    println('Hello, Camel!')
    return 0
}
```

```bash
camel greeting.cml
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

