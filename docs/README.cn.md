<div align="center">

<img src="../assets/logo_text.png" alt="Camel" width="560">

### 程序就是图。

**Camel** 是一门图原生的通用编程语言，AI 是它首要的应用场景。

[![CI](https://github.com/OpenCML/Camel/actions/workflows/ci.yml/badge.svg)](https://github.com/OpenCML/Camel/actions/workflows/ci.yml)
[![版本](https://img.shields.io/github/v/tag/OpenCML/Camel?label=version&color=blue)](https://github.com/OpenCML/Camel/tags)
[![许可证：MIT](https://img.shields.io/badge/license-MIT-green.svg)](../LICENSE)
[![平台](https://img.shields.io/badge/platforms-Linux%20%7C%20macOS%20%7C%20Windows-lightgrey.svg)](setup.cn.md)
[![VS Code](https://img.shields.io/visual-studio-marketplace/v/Kogler7.open-cml-extension?label=VS%20Code&logo=visualstudiocode)](https://marketplace.visualstudio.com/items?itemName=Kogler7.open-cml-extension)
[![Stars](https://img.shields.io/github/stars/OpenCML/Camel?style=social)](https://github.com/OpenCML/Camel/stargazers)

[为什么是 Camel](#为什么是-camel) ·
[Camel 之禅](#camel-之禅) ·
[先睹为快](#先睹为快) ·
[快速开始](#快速开始) ·
[教程](tutorial/tutorial.cn.md) ·
[文档](#文档) ·
[路线图](../ROADMAP.md) ·
[English](../README.md)

</div>

---

## 为什么是 Camel

大多数语言把程序写成给机器一步步执行的指令。可是编译器、并行运行时、自动微分、GPU 后端真正需要的是一张图：要算什么，谁依赖谁。于是图只能从代码里事后恢复，靠追踪、捕获、分析。恢复出来的图会断开，会漏掉副作用的含义，作用在它上面的变换也成了用户碰不到的黑箱。

Camel 让**程序本身就是图**。你写下的是计算的意图与依赖，先后顺序只在需要时才写。由此：

- **编译器拿到的就是你写的东西**：不用追踪，图也不会断开；互不依赖的计算天然可以并行。
- **变换是普通的函数**：优化、求导、上 GPU、导出都是作用在图上的遍，你能看、能换，也能用 Camel 自己写。
- **优化的知识可以写成代码并加以验证**：一条代数律就是一条规则，`check` 检查它对不对。
- **一份程序，多种执行**：解释、JIT、GPU，或导出为 ONNX、C++，意思不变。

这些性质对所有"计算比控制流更重要"的程序都成立。训练与部署模型是其中最迫切的一类，所以 AI 是 Camel 首要的应用场景，但不是唯一的。详见[为什么是 Camel](why.cn.md)。

## Camel 之禅

1. **Let the graph speak.** 一切计算都是一张表达意图的图。
2. **Graphs are primitives, not artifacts.** 图是意图的原生形态，不是代码的副产品。
3. **Every construct is a piece of the graph.** 每个语言构造都对应图上的一块；说不清它在图上是什么的构造，不该进语言。
4. **Async by nature, sync when explicit.** 默认异步，同步要明写；先后来自依赖，不来自书写顺序。
5. **Execution is polymorphic and multi-staged.** 遍是函数，函数是图；编译、优化、执行都是对图的分阶段重写。
6. **Keep code pure, let runtimes specialize.** 上下文在运行时注入，不写死在代码里。
7. **One language, all the way down.** 只有一门语言：类型、规则、遍都用 Camel 自己写。
8. **Let compilers bear the burden.** 调度、映射、融合、降级是编译器的事，不是你的事。

`camel --zen` 会打印它们。

## 亮点

| | |
|---|---|
| 🧭 **图原生** | 每个语言构造对应图上固定的一块结构。求值跟着依赖走，互不依赖的计算不用你开口就能并行。 |
| 🔁 **遍就是函数** | 优化、求导、导出、执行都是图到图的遍，在命令行上组合，也能用 Camel 自己写。 |
| 📐 **可验证的规则** | 一条代数律只写一次、对所有满足条件的类型成立；改写引擎用上它，`check` 证明、反驳或抽样检验它。 |
| ∂ **天生可微** | `grad(f)` 在编译期把 `f` 的图改写成导数的图，后面的每个遍都能接着优化它。 |
| 🧮 **带形状的数组** | `f32[n, k]` 一种类型覆盖所有秩；维度名在调用两端核对；没有别人看得见时，内存原地更新。 |
| 🚀 **多种后端** | 节点虚拟机、字节码虚拟机、JIT、并行调度、GPU 内核、ONNX、独立 C++——处处同一个意思。 |
| 🐍 **与 Python 共处** | 从 Python 调用 Camel 模块，数组零拷贝；Camel 的诊断以带位置的 Python 异常到达。 |
| 🛠️ **完整的工具** | 语言服务器、格式化器、能在遍之间单步的调试器，诊断附带可以自动应用的修改。 |

## 先睹为快

用梯度下降拟合一条直线。`grad(loss)` 不是逐步求值的库调用，而是一次图改写：它建出的导数是一张普通的图，优化器和 JIT 都看得见。

```camel
import { grad } from autodiff

with <w: float>
func loss(x: float, y: float): float {
    let e = w * x - y
    return e * e
}

func main(): int sync {
    let fitted = loop w = 0.0 for i in 0..100 do w - 0.01 * grad(loss)<w>(3.0, 6.0)
    println(fitted)                  // 接近 2.0
    return 0
}
```

文件名后面的词是要依次运行的遍：

```bash
camel fit.cml                        # 在节点虚拟机上运行
camel fit.cml std::gir               # 不运行，打印图（Graphviz DOT）
camel fit.cml std::macro std::opt std::jit   # 展开 grad、优化、用 JIT 运行
camel fit.cml std::opt::saturate std::gir    # 保留所有等价写法，并排画出来
```

优化同样用 Camel 写。一条定律只写一次，对所有满足条件的类型成立：

<!-- camel-snippet: fragment: a rule declaration from the standard library's style, shown on its own -->

```camel
rule add_zero::<T>(x: T) when is_integer::<T>() { x + 0 => x }
```

## 语言与图

| 源码里 | 在图上 | 由此得到 |
|---|---|---|
| 表达式、算子调用 | 节点与数据边 | 按依赖求值；互不依赖的节点可以乱序或并行 |
| 函数 | 子图；调用是一个节点 | 函数的行为由它自己决定 |
| `sync` 块 | 块内效应之间的先后 | 效应按书写顺序执行 |
| `if`、`match`、`a ?? b`、`a !! b` | 分支 | 错误与空值都是值，由分支处理 |
| `loop … do …` | 带状态的循环节点 | 迭代是一个表达式，值是最后的状态 |
| 泛型函数 | 子图模板，按具体类型实例化 | 图上只有具体类型 |
| 遍 | 从图到图的函数 | 开放的流水线 |

表格背后的规则见[设计原则](design-principles.cn.md)。

## 快速开始

### 安装

```bash
pip install camel-lang
```

装好后 `camel` 命令就在 `PATH` 上，Python 里也能 `import camel`。发布的 wheel 覆盖 Linux（x64、arm64）、macOS（Apple Silicon）与 Windows（x64）。`camel` 命令在任何 Python 3 上都能装；从 Python 调用 Camel 需要本次发布编了 interop wheel 的那个 CPython，wheel 文件名里写着是哪一个（`cp311`、`cp313`…）。interop wheel 每个平台一只而不是每个 Python 版本一只，所以请用那个解释器安装 camel-lang。

arm64 的 wheel（Linux arm64、Apple Silicon）没有 `std::jit` 的原生代码生成器：该遍照常被接受，程序在字节码解释器上运行，与 `std::fvm` 相同、结果一致，构建在日志里说明一次（`-v`）。见[命令行](cli.cn.md)。

### 从源码构建

需要 Clang 19+、CMake 3.25+、Ninja、Conan 2.10+ 与 Node.js 18+。各平台的工具链见 [setup.cn.md](setup.cn.md)。

```bash
git clone https://github.com/OpenCML/Camel.git && cd Camel
npm install          # npm 包与 C++ 依赖（需要联网）
npm run build        # Release 构建，产物在 out/latest
```

把 `out/latest/bin` 加进 `PATH`，`CAMEL_HOME` 设为 `out/latest`。CI 构建并测试 Linux（x64、arm64）、macOS（Apple Silicon）与 Windows（x64）。

### Hello, Camel

```camel
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

### 编辑器

在 VS Code 安装 [OpenCML-Ext](https://marketplace.visualstudio.com/items?itemName=Kogler7.open-cml-extension)：高亮、语言服务、交互式的图浏览，以及能在遍之间暂停的调试器。把 `opencmlLanguageServer.camelPath` 指向你的 `camel`。见[插件说明](https://github.com/OpenCML/OpenCML-Ext#readme)与[调试指南](debugging.cn.md)。

### 在 Python 里用

```python
import camel

stats = camel.load("stats.cml")      # 一个 Camel 模块
y = stats.normalize(x)               # NumPy 数组零拷贝进出
```

出错时抛 `camel.CamelError`，带着诊断与位置。

## 文档

| | |
|---|---|
| 📘 [教程](tutorial/tutorial.cn.md) | 从第一个程序到规则、遍、求梯度与导出 |
| 💡 [为什么是 Camel](why.cn.md) | Camel 解决什么问题，程序为什么应该就是图 |
| 📖 [语言参考](language/README.md) | 每个构造的精确规则 |
| 📚 [标准库](stdlib-reference.md) | 每个模块、每个函数 |
| 🧪 [示例](../examples/README.cn.md) | 按任务组织的短程序：文件、JSON、命令行工具、并行 `map`、训练一步 |
| ⌨️ [命令行](cli.cn.md) · [诊断](diagnostics.md) | 选项、遍，以及 `camel --explain <名字>` |
| 🤖 [给 AI 的 Camel](reference/camel-for-ai.md) · [`llms.txt`](../llms.txt) | 模型写 Camel 需要知道的 |
| 🧱 [设计原则](design-principles.cn.md) | 语言建立在哪些规则上 |
| ⚙️ [技术文档](technical/README.md) | 编译流水线、图 IR、运行时 |

## 状态

Camel 在积极开发中，尚未到 1.0。语言变化时不留兼容层：一种写法被取代，旧写法就直接没有了。发布说明见 [changes.log](../changes.log)，计划见[路线图](../ROADMAP.md)。

## 参与贡献

欢迎提 issue 与 pull request。先读[贡献指南](CONTRIBUTING.cn.md)，并遵守[行为准则](CODE_OF_CONDUCT.cn.md)。安全问题请按[安全策略](SECURITY.cn.md)报告。

## 许可证

Camel 以 [MIT 许可证](../LICENSE)发布。
