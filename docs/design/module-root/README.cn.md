# 模块导出值与公共 root（设计说明）

状态：**设计说明，待语言作者裁定。本稿不含任何编译器源码改动。**
依据：`docs/design-principles.cn.md` 10.1“模块与图”、2.1 对应表“模块顶层的 `let`”一行、10.1 的“合并的具体做法先出设计说明”。
基线提交：`0c842f2c`（Camel v0.1.23）。文中的图与数字都可以用附录 A 的命令复现。

文中每条事实标注来源：

- 【实测】在本仓库构建出的 `camel` 上实际运行看到的；
- 【读码】读代码得到、没有运行验证的（给出文件与行号）；
- 【未查】没有核实的。

图用 `std::rgir` 的记法，省略了 `origin`/`span`/`srcfile` 属性；标“示意”的图是手画的，其余是实际输出。

示例程序在本文档旁边的 [`examples/`](examples/) 目录里；文中写作 `examples/x` 的路径都指 `docs/design/module-root/examples/x`，仓库根目录下的同名目录会写明“仓库的 `examples`”。

---

## 0. 摘要

**现状与设计原则 9.6 的对照。**

| 10.1 的说法 | 现状 | 来源 |
|---|---|---|
| 模块顶层的代码不默认执行，按依赖求值 | **成立。** 没有被 `main` 依赖的顶层 `let` 不求值（带效应时给 `EffectNotExecuted` 警告）；被依赖的求值一次 | 【实测】1.3 |
| 模块顶层的 `let` 是 root 上的一个节点 | 成立（入口模块） | 【实测】1.2 |
| 被导入之后各模块的 root 合并 | **不成立。** 每个模块各有一张 `__root__`；程序运行的只是入口模块的那张，被导入模块的 root 上的节点没有任何途径被求值 | 【读码】【实测】1.5 |
| 模块可以导出值 | **不成立。** `export let` 能写，导入方一用就段错误 | 【实测】1.5 |

还有两点需要知道：

1. “被依赖”是**图上的静态依赖**：`main` 沿调用链静态可达的函数用到的顶层值，全部在 `main` 的第一条语句之前求值，即使运行时走不到用它的那个分支（1.3）。仓库里已有测试锁定“顶层值出故障时 `main` 一行都没执行”。
2. **今天被导出的函数不能使用自己模块顶层的任何值**（不只是泛型函数）：导入方要么得到 `UnresolvedReference`，要么——导入方恰好有同名的顶层值时——**悄悄用了导入方的那个值**（期望 11，得到 1001，没有任何诊断）。这是本设计必须解决的问题的另一半，而且是一个现存的静默错误（附录 B，D1）。

**结论。** 比较了四种合并方式（第 2 节）：

| 方案 | 图上是什么 | 无跨图节点引用 | 按值惰性 | 只求值一次 | 结论 |
|---|---|---|---|---|---|
| (a) 物理合并 | 全程序一张 `__root__`，各模块顶层节点都在上面；函数经捕获端口取值 | 是 | 是 | 是 | **推荐** |
| (b) 层次化 | 公共 root 下每个模块一张 root 子图，导出值是它的输出 | 是 | 否：以模块为单位 | 是 | 不推荐 |
| (c) 导入方调用被导入模块的 root | 没有公共 root，图引用图 | 是 | 否 | **否**：菱形导入时求值两次 | 不成立 |
| (d) 惰性单元 | 函数里放一个指名 root 值的节点，首次使用时求值并缓存 | 否 | 是（动态） | 靠运行时加锁 | 不推荐 |

**推荐 (a)，而且它几乎不需要新机制。** 入口模块里“函数用到顶层的值”今天就是这样工作的：值在 root 上是一个节点，函数里是一个端口，值沿调用边（`WITH`）一路传下去（1.2）。(a) 只是把“root 上的节点”从“入口模块的顶层节点”扩大为“所有模块的顶层节点”。把两个模块拼成一个文件，今天编译出来的图就是 (a) 合并后的图，所以本稿里 (a) 的图是实际输出，不是示意。需要改的是名字怎么找：捕获今天按**名字**在调用处绑定，跨模块时名字会找错（D1 的根因），要改成按“模块 + 名字”绑定。

用户能写 `export let pi = 3.14159` 是实施计划的第 2 步（第 8 节），前面只有一步“所有模块建到同一张 root 上”。

**最需要负责人决定的三件事**（完整清单见第 9 节）：

1. **Q1 合并方式**：建议 (a)。
2. **Q2 求值时机**：保持现状——被 `main` 静态依赖的顶层值在 `main` 开始之前求值——还是改为首次用到时才求值。建议保持：它是“值是 root 上的一个节点”的直接结果，也是并行调度下不需要加锁的原因。
3. **Q5 模块互相导入**（a↔b）：现在是段错误。函数互相调用要不要支持，决定加载过程是否要改成“先声明、后建图”的两阶段。建议：第一步先改成带环路说明的 `ImportCycle`，两阶段加载排在导出值之后单独做。

**调研中发现的现有缺陷**（附录 B）：导入的函数用错顶层值（静默错误）；`export let` 被使用、模块互相导入、`std::cpp` 遇到计算出来的顶层值，这三处是段错误；同名同签名的两个导入静默取其一；`std::rgir` 里带捕获的函数端口名错位。

---

## 1. 现状

### 1.1 导出函数、导入并调用

`examples/now_func/`：

```camel
// lib.cml
module lib
export func double(x: int): int { return x * 2 }

// main.cml
import { double } from lib
func main(): int sync { println(double(21)); return 0 }
```

`camel examples/now_func/main.cml std::rgir`【实测】：

```
digraph GraphIR {
    subgraph "main" {
        subgraph "double" {                       // lib 的函数：一张子图
            N00 [PORT x]  N01 [DATA 2]  N02 [OPER :op/mul_l]
            N00 -> N02; N01 -> N02; N02 -> EXIT
        }
        N03 [DATA 21]
        N04 [FUNC double]                         // 调用：图引用图
        N05 [OPER :io/println]  N06 [DATA 0]  N07 [GATE]
        N03 -> N04 [NORM]; N04 -> N05 [NORM]; N06 -> N07 [NORM]; N05 -> N07 [CTRL]; N07 -> EXIT
    }
    // 以下是 __root__：入口模块 main.cml 的 root
    N08 [DATA Function(graph=main)]
    N09 [CALL]
    N08 -> N09 [WITH]; N09 -> EXIT
}
```

要点：

- 导入函数就是“图引用图”：`main` 里一个 `FUNC double` 节点，`double` 的图进入依赖表。这部分没有问题。
- 图里**没有 `lib` 的 root**。每个模块编译时各建一张 `__root__`【读码 `src/compile/gir/builder.cpp:737`】；程序的运行时图从入口模块的 root 出发按可达性收集【读码 `src/core/context/context.cpp:619-632`、`src/compile/gir/reachable.cpp:36-79`】，被导入模块的 root 不被任何东西引用，所以不在其中。非入口模块的 root 上是它自己的顶层节点，外加一个占位的 `DATA 0` 作为出口【读码 `builder.cpp:830-838`】；这张图不会被执行。
- 一个模块能从别的模块得到的实体：函数图集、C++ 算子组、带装饰器的函数、泛型模板、类型【读码 `src/core/module/entity_internal.h`、`builder.cpp:2240-2309`】。

### 1.2 函数用到本模块顶层的 `let`

`examples/now_let/main.cml`：

```camel
let k = 10
func add_k(x: int): int { return x + k }
func twice(x: int): int { return add_k(add_k(x)) }
func main(): int sync { println(twice(1)); return 0 }
```

`camel examples/now_let/main.cml std::rgir`【实测】：

```
digraph GraphIR {
    subgraph "main" {
        subgraph "twice" {
            subgraph "add_k" {
                N00 [PORT]  (WITH_PORT 0)         // 捕获的 k
                N01 [PORT]  (NORM_PORT 0)         // x
                N02 [OPER :op/add_l]
                N01 -> N02; N00 -> N02; N02 -> EXIT
            }
            N03 [PORT]  (WITH_PORT 0)             // 捕获的 k：twice 自己不用，替 add_k 传
            N04 [PORT]  (NORM_PORT 0)             // x
            N05 [FUNC add_k]  N06 [FUNC add_k]
            N03 -> N05 [WITH]; N04 -> N05 [NORM]
            N03 -> N06 [WITH]; N05 -> N06 [NORM]; N06 -> EXIT
        }
        N07 [CLOSURE_PORT]                        // 捕获的 k
        N08 [DATA 1]  N09 [FUNC twice]  N0A [OPER :io/println] ...
        N07 -> N09 [WITH]; N08 -> N09 [NORM]; ...
    }
    // __root__
    N0D [DATA 10]                                 // let k = 10：root 上的一个节点
    N0E [DATA Function(graph=main, tupleSlots=1)]
    N0F [FILL]                                    // 把 k 装进 main 的函数值
    N10 [CALL]
    N0D -> N0F [WITH]; N0E -> N0F [NORM]; N0F -> N10 [WITH]; N10 -> EXIT
}
```

这就是现有的捕获机制，四条规则【读码】：

1. **值是 root 上的一个节点**（`N0D`），只此一个。
2. **用到它的函数里是一个端口。** 名字解析到另一张图上的节点时，从当前图沿外层图一路向上，在每一层加一个捕获端口，并在函数类型上记下被捕获的**名字**（`builder.cpp:984-1042`）。函数图里没有指向 root 节点的边。
3. **值沿调用边传下去。** 函数第一次被直接调用时，它的捕获端口改为 `with` 端口（`builder.cpp:2734-2768`）；此后每个调用处按记下的名字**在调用处的作用域里**再解析一次，连一条 `WITH` 边（`builder.cpp:2792-2796`）。调用者自己因此也得到一个捕获端口，如此一直传到 root。`twice` 不用 `k`，但它调用 `add_k`，所以有 `N03`。
4. **root 把值交给入口。** `main` 有捕获时，root 上是 `DATA 函数值 → FILL（装入捕获的值）→ CALL`；没有捕获时是 `DATA 函数值 → CALL`（1.1）。

`var` 同理：捕获的是同一个单元的引用，端口带 `var` 标记（`builder.cpp:1022-1035`）。

这套机制满足 2.2 的假设：图与图之间只有“图引用图”，值从端口进。

`std::rgir` 里带捕获的函数端口名字错了一位：上图 `add_k` 中实际输出把捕获端口标成 `x`、把 `x` 标成 `PORT`（附录 B，D6）。上面的图按边的实际含义重新标注过。

### 1.3 顶层语句的求值是怎么驱动的

**root 就是一张普通的图，按普通的规则执行：只有从出口沿边可达的节点才执行。** 所有后端（以及宏求值器、`std::cpp`）共用同一个函数取执行序，它先从出口反向标出需要的节点【读码 `src/execute/graph_runtime_support.cpp:131-166`；调用处 `nodevm.cpp:206`、`fastvm/compile.cpp:121`、`taskflow.cpp:524`、`macro/eval.cpp:715`、`cpp/runtime_emitter.cpp:322`】。root 的出口是对 `main` 的调用，所以顶层的节点被执行，当且仅当 `main` 的调用依赖它。事实上没被依赖的顶层节点连运行时图都进不去：编码时只保留活节点【读码 `src/compile/gir/graph_encode.cpp:252-313`；实测见 2.1 的 root 输出】。

`examples/now_effects/main.cml`【实测】：

```camel
func noisy(tag: string): int sync { println(tag); return 7 }

let _banner = println('top-level println')
let unused = noisy('unused let')
let used = noisy('used let')

func main(): int sync { println('main', used); return 0 }
```

```
warning[SemanticDiag::EffectNotExecuted]: The call of 'println' has side effects but is never executed ...  (第 6 行)
warning[SemanticDiag::EffectNotExecuted]: The call of 'noisy' has side effects but is never executed ...    (第 7 行)
used let
main 7
```

逐项回答：

| 问题 | 答案 | 来源 |
|---|---|---|
| 顶层能写 `println('loading')` 这样的语句吗 | 不能，语法错误：顶层只有声明（`examples/misc/top_stmt.cml`）。效应只能以 `let _x = println(...)` 的形式出现在顶层 | 【实测】 |
| 顶层带效应、没人用的 `let` 执行吗 | 不执行；入口模块里给 `EffectNotExecuted` 警告 | 【实测】 |
| 顶层的 `let x = f()` 没人用时执行吗 | 不执行 | 【实测】 |
| 有人用时执行几次 | 一次。两个函数都用到同一个值（`examples/emu/a_merged.cml`）、八个并行的元素函数都用到（`examples/par/main.cml`），在 `std::nvm`/`std::fvm`/`std::jit`/`std::tf` 上都只打印一次 | 【实测】 |
| 顶层的 `let` 之间有顺序吗 | 没有，root 不是同步块。`examples/misc/two_effects.cml` 按 `first`、`second`、`third` 的顺序声明三个带效应的值，`main` 里写 `third + first + second`，打印顺序是 `third first second` | 【实测】 |
| 顶层的 `wait` | 与别处一样：`let _b = wait bump()` 之后的 `let seen = n` 读到修改后的值（`examples/misc/top_wait.cml`） | 【实测】 |
| 被导入模块顶层带效应的 `let` | 不执行，**也没有警告**（`examples/lib_effects/`）。警告只在建入口调用的那条路径上检查【读码 `builder.cpp:826`】，没有 `main` 的库模块走不到 | 【实测】 |

**“被依赖”是静态的，并且在 `main` 之前。** `examples/now_order/main.cml`【实测】：

```camel
let cfg = noisy('evaluating cfg')
let other = noisy('evaluating other')

func pick(flag: bool): int { return if flag then other else 0 }

func main(): int sync {
    println('main starts')
    println(cfg)
    println(pick(false))      // 运行时走不到 other
    println(cfg)
    return 0
}
```

```
evaluating cfg
evaluating other
main starts
7
0
7
```

`cfg` 与 `other` 都在 `main starts` 之前求值，`other` 虽然运行时没被用到也求值了。原因在 1.2 的图上：值是经 `FILL → CALL` 交给 `main` 的，`main` 的调用依赖它们，所以它们先于调用执行。这与函数内部一致——分支臂捕获的值也在分支之前求值。已有测试锁定这一点：`test/plans/feat/consistency/faults.plan.toml` 的 `fault.top_level` 断言“顶层值出故障时 `main` 一行都没执行”【实测 `test/cases/consistency/faults/top_level_fault.cml`】。

### 1.4 入口

- 入口模块必须在 root 上定义可解析的 `main`，否则是 `EntryModuleMissingMain`【实测 `camel examples/export_let/lib.cml`】。
- `main` 带参数时，root 自己得到同样的参数端口并原样传给 `main`【读码 `builder.cpp:776-819`】。
- `std::cpp` 生成可执行程序时要求 root 以“`FILL` 出的函数值 + `CALL`”结尾【读码 `src/passes/trans/cpp/runtime_emitter.cpp:1019-1043`】。
- 被导入的模块如果自己也定义了 `main`，它自己的 root 上也会建一个入口调用【读码 `builder.cpp:751-826`，条件只是“有 `main`”】，但那张 root 不在程序里，没有效果。

### 1.5 跨模块时发生什么

**(1) 导出的函数用到本模块顶层的值：名字找错。** `examples/lib_let/`：

```camel
// lib.cml
module lib
let k = 10
export func add_k(x: int): int { return x + k }

// main.cml                              // main_shadow.cml
import { add_k } from lib                import { add_k } from lib
                                         let k = 1000
func main(): int sync {                  func main(): int sync {
    println(add_k(1))                        println(add_k(1))
    return 0                                 return 0
}                                        }
```

【实测】`main.cml`：`error[SemanticDiag::UnresolvedReference]: Unresolved reference: 'k'`，指在 `main` 上。`main_shadow.cml`：**打印 `1001`，没有任何诊断**；图上是导入方的 `DATA 1000` 经 `FILL` 进了 `main`、再经 `WITH` 进了 `lib` 的 `add_k`。

原因就是 1.2 的规则 3：捕获记的是名字 `k`，调用处在**调用者的模块**里再解析这个名字。同一个模块里这没有问题；跨模块时找到的是别人的 `k`，或者找不到。`var` 一样（`examples/var_lib/`：`UnresolvedReference: 'cnt'`）。

**(2) 导入值：段错误。** 导出一侧已经实现：`export let pi = 3.14159` 把 root 上的节点放进模块的导出表【读码 `builder.cpp:4844-4853`】。导入一侧是 `ASSERT(false, "Cannot import a data node directly.")`【`builder.cpp:2252-2253`】，发布构建里断言不生效，接着把另一张图的节点当作当前图的节点用。【实测】`examples/export_let/main.cml` 与 `main_ns.cml`（`lib::pi`）：`Segmentation fault`，退出码 139。只导入不使用（`main_unused.cml`）正常。

**(3) 导出的泛型函数用到本模块顶层的值：被拒绝。** `examples/generic_let/`【实测】：`GenericFunctionUnsupported: The generic function 'shift' cannot be instantiated in another module while its body uses 'bias', a value of the top level of its own module.` 泛型实例建在使用方模块的 root 之下，声明方 root 上的节点不在包着它的任何一张图里，无法捕获【读码 `builder.cpp:988-1014`、`1476-1489`、`1575-1582`；`docs/technical/28_generic_functions_and_type_functions.md` 第 5 节】。这条限制给出了诊断；(1) 是同一个问题在普通函数上的表现，没有诊断。

**(4) 其余。**

| 情形 | 现状 | 来源 |
|---|---|---|
| 菱形导入（`main` 导入 `b`、`c`，两者都导入 `d`） | `d` 只编译一次，`d` 的函数只有一张图（模块按名字缓存） | 【实测 `examples/diamond/`】【读码 `context.cpp:326-339`】 |
| 互相导入（`a` 导入 `b`，`b` 导入 `a`） | 段错误（`camel` 与 `camel check` 都是）。现有的 `ImportCycle` 只认“模块导入自己” | 【实测 `examples/mutual/`】【读码 `context.cpp:330-338`】 |
| 两个导入同名、签名不同 | 合并成一个重载集，按实参类型选 | 【实测 `examples/conflict/main_overload.cml`】【读码 `src/core/module/module.cpp:33-99`】 |
| 两个导入同名、签名相同 | **静默取先导入的那个**（打印 2，不是 101） | 【实测 `examples/conflict/main.cml`】 |
| 导入改名 `import { find as str_find } from str` | 语法错误，尚未实现；`import math as m` 同样 | 【实测 `examples/rename/`】 |
| 顶层 `let` 引用写在它后面的顶层 `let` | `UnresolvedReference`；函数体引用写在函数后面的顶层 `let` 也一样 | 【实测 `examples/misc/forward_let.cml`、`self_cycle.cml`】 |
| C++ 模块的常量 | 无参的纯算子：`math::pi()` 在图上是一个没有输入的 `OPER math:pi` 节点，`std::opt::fold` 把它折成 `DATA 3.14159` | 【实测 `examples/misc/pi_today.cml`】【读码 `modules/math/numeric.cpp:187-192`】 |

### 1.6 单个模块之内，顶层的值已经能做什么

以下都只涉及入口模块自己的顶层值，全部【实测】：

- 直接调用、递归、泛型实例、作为值传递的函数、带 `with` 实参的调用、高阶算子的元素函数（lambda 与具名函数）里使用顶层的 `let`：`examples/uses/main.cml` 在 `std::nvm`、`std::fvm`、`std::jit`、`std::tf`、`std::opt::inline std::fvm`、`std::macro std::opt std::nvm` 六条流水线上输出相同。
- 顶层的 `var` 被函数读写：`examples/now_var/main.cml`、`examples/misc/var_reads.cml`（读、写、`foreach` 里写、经另一个函数读）与 `test/cases/linear/nvm/fib1.cml` 在 7 到 12 条流水线上结果一致、正确。设计原则 3.6 说这一用法“没有完全实现”；在单个模块之内本次没有找到出错的程序，找到的缺口是跨模块（1.5 (1)）。单模块内是否还有别的缺口【未查】。
- 提前求值与折叠：`examples/macro_let/main.cml` 中 `let k = square(12)`（`macro` 函数）经 `std::macro` 在 root 上变成 `DATA 144`；再经 `std::opt`，用到它的函数被按常量特化（`area$spec1`），整个程序折成 `DATA 904.778`。
- 求梯度：`examples/ad_let/main.cml` 中被求导的函数用到顶层的张量 `w`，`grad(loss)(x)` 给出对 `x` 的梯度，`w` 按常数处理。
- ONNX：`examples/onnx_let/main.cml` 把权重写在顶层，`std::macro std::opt onnx::export` 导出的模型有一个输入 `x` 和一个 2×2 的初始化器（名字是 `const`，不是 `w`）。

这些就是 (a) 合并之后跨模块应当得到的行为。

---

## 2. 合并之后的图

用同一个例子比较：模块 `config` 导出 `let cfg = read_config()` 和 `let table = build_table()`（两个都带效应），模块 `main` 的两个函数 `port_of`、`host_len` 各用到 `cfg`，`table` 没有人用。

```camel
// config.cml
module config
func read_config(): (string, int) sync { println('read_config runs'); return ('localhost', 8080) }
func build_table(): int sync { println('build_table runs'); return 3 }
export let cfg = read_config()
export let table = build_table()

// main.cml
import { cfg } from config
func port_of(): int { return cfg.1 }
func host_len(): int { return len(cfg.0) }
func main(): int sync { println(port_of()); println(host_len()); return 0 }
```

今天这个程序是段错误（1.5 (2)）。下面每个方案都用今天能编译的等价写法得到实际的图：把方案要建的结构用现有的语言元素写出来，放在 `examples/emu/`。

### 2.1 (a) 物理合并：全程序一张 root

**规则。** 一个程序只有一张 `__root__`。每个模块顶层的 `let`/`var` 是它上面的节点；每个模块的每个函数是它之下的一张子图。导入一个值不建任何节点，只是让导入方的名字指向那个节点；函数用到它时走 1.2 的捕获机制。

等价写法就是把两个文件拼成一个（`examples/emu/a_merged.cml`）。`camel examples/emu/a_merged.cml std::rgir`【实测】：

```
digraph GraphIR {
    subgraph "read_config" { ... println ... DATA (localhost, 8080) ... }
    subgraph "main" {
        subgraph "port_of" {
            N04 [PORT]  (WITH_PORT 0)             // config::cfg
            N05 [ACCS .1]
            N04 -> N05; N05 -> EXIT
        }
        subgraph "host_len" {
            N06 [PORT]  (WITH_PORT 0)             // config::cfg
            N07 [ACCS .0]  N08 [OPER :struct/len_str]
            N06 -> N07; N07 -> N08; N08 -> EXIT
        }
        N09 [CLOSURE_PORT]                        // config::cfg
        N0A [FUNC port_of]  N0B [OPER :io/println]
        N0C [FUNC host_len] N0D [OPER :io/println] ...
        N09 -> N0A [WITH]; N09 -> N0C [WITH]; ...
    }
    // __root__（全程序唯一）
    N14 [FUNC read_config]                        // config 模块的 let cfg
    N15 [DATA Function(graph=main, tupleSlots=1)]
    N16 [FILL]
    N17 [CALL]
    N14 -> N16 [WITH]; N15 -> N16 [NORM]; N16 -> N17 [WITH]; N17 -> EXIT
}
```

运行【实测，`std::nvm`、`std::fvm`、`std::jit`、`std::tf`、`std::opt::inline std::jit`、`std::opt std::tf`、`std::macro std::opt std::nvm`】：

```
read_config runs
8080
9
```

- **无跨图节点引用**：是。`cfg` 是 root 上的 `N14`，两个函数各有一个端口，值沿 `WITH` 边进去。
- **函数是子图、调用不改变其结构**：是，与今天完全相同。
- **`cfg` 只求值一次**：是，`N14` 是一个节点，root 只执行一次。
- **没人用时不求值**：是，并且是**逐个值**的。`table` 没有人用，`build_table` 不执行；它甚至不在上面的 root 里（没被依赖的节点不进运行时图）。把 `main` 改成不用 `cfg`（`examples/emu/a_unused.cml`），`read_config` 也不执行。
- **这与“模块顶层的 let 就是 root 上的一个节点”一字不差。**

**名字怎么限定。** 图上的节点本来就没有名字，名字只在两处出现：编译时的作用域，和函数类型上记的捕获名（1.2 规则 2）。

- 作用域：每个模块保留自己的顶层作用域。两个模块各有一个 `k`，是 root 上的两个节点，分别登记在两个作用域里，不冲突。
- 捕获名：从裸名字 `k` 改为“模块 + 名字”（下文写作 `lib::k`）。调用处不再在调用者的作用域里找 `k`，而是直接取 `lib` 模块顶层的那个节点。这一条同时消除 1.5 (1) 的错误。函数内部的局部捕获不变。

**模块边界在图上留不留痕迹。** 结构上不留：root 上没有“模块”这种节点或子图。可以恢复出来的信息有两处：每个节点的源码位置里有文件；捕获端口的名字带模块。建议 `std::rgir` 把捕获端口标成 `config::cfg`（今天标的是 `PORT`），把被导入模块的函数图标成 `config::read_config`。

### 2.2 (b) 层次化：公共 root 下每个模块一张 root 子图

**规则。** 每个模块的 root 保留为一张自己的图。它的导出值是它的输出，它用到的别的模块的值是它的输入。公共 root 按导入的拓扑序把每个模块的 root 调用一次，取出导出值，传给需要它的模块 root。

`examples/emu/b_hier.cml` 用 `config_root()` 代表 `config.__root__`，用文件顶层代表公共 root。`camel examples/emu/b_hier.cml std::rgir`【实测】：

```
    subgraph "config_root" {                      // config.__root__：输出是 (cfg, table)
        N08 [FUNC read_config]
        N09 [FUNC build_table]
        N0A [DATA (null, 0)]  N0B [FILL]          // 把两个导出值装成一个元组
        N08 -> N0B [WITH]; N09 -> N0B [WITH]; N0A -> N0B [NORM]; N0B -> EXIT
    }
    // 公共 root
    N18 [FUNC config_root]                        // 每个被导入的模块一次调用
    N19 [ACCS .0]                                 // 取出 cfg
    N1A [DATA Function(graph=main, tupleSlots=1)]
    N1B [FILL]  N1C [CALL]
    N18 -> N19; N19 -> N1B [WITH]; N1A -> N1B [NORM]; N1B -> N1C [WITH]; N1C -> EXIT
```

运行【实测】：

```
read_config runs
build_table runs
8080
9
```

- **模块 A 的函数用模块 B 顶层的值，是不是跨图节点引用**：不是，但值要多走几段。完整的链（入口模块的 root 也是子图时，示意）：

  ```
  公共 root:      N1 [FUNC config.__root__] → N2 [ACCS .0] ──WITH──▶ N3 [FUNC main.__root__]
  main.__root__:  P0 [PORT cfg] ──WITH──▶ [FILL main 的函数值] ──▶ [CALL]
  main:           [CLOSURE_PORT cfg] ──WITH──▶ [FUNC port_of]
  port_of:        [PORT cfg] → [ACCS .1]
  ```

  谁来传：公共 root。沿什么边：`FUNC` 的输出、`ACCS`、`WITH`。时机：模块 root 的调用完成之后、入口调用之前。比 (a) 多出“模块 root 的调用、取字段、模块 root 的输入端口”三段。
- **函数是子图**：是。
- **只求值一次**：是，公共 root 上每个模块只有一个调用节点。
- **没人用时不求值**：**只到模块为止。** 上面 `build_table runs` 被打印了：`config.__root__` 的输出是全部导出值的元组，调用它就要把每个导出值都算出来。程序只导入 `math` 的 `pi`，`math` 模块所有带效应的导出值都会执行。这与“模块顶层的 let 被依赖才求值”不符。
- **与“let 就是 root 上的一个节点”是否相容**：不相容。`cfg` 是 `config.__root__` 上的节点，公共 root 上看到的是“模块调用 + 取字段”。
- **模块之间值的依赖成环的问题**：`a` 的 `x` 用到 `b` 的 `y`，`b` 的 `z` 用到 `a` 的 `x`，在值的层面没有环；但 `a.__root__` 要 `b.__root__` 的输出，`b.__root__` 又要 `a.__root__` 的输出，在模块 root 的层面成环，无法建图。(a) 没有这个问题。

### 2.3 (c) 导入方调用被导入模块的 root

**规则。** 没有公共 root。导入方的 root 上有一个调用被导入模块 root 的 `FUNC` 节点，这是图引用图，符合假设。

问题在“求值一次从哪来”。调用的结果只在**调用它的那张图**里被共享。菱形导入时，`b.__root__` 与 `c.__root__` 各调用一次 `d.__root__`，是两个调用节点。`examples/emu/c_diamond.cml`【实测】：

```
d.__root__ runs
d.__root__ runs
112
```

要让 `d` 只求值一次，就得由一张包住 `b`、`c` 的图调用 `d.__root__` 一次，再把结果传给 `b.__root__` 和 `c.__root__`——这正是 (b)。所以 (c) 要么不能保证求值一次，要么就是 (b)。

把 (c) 再往下推一步：每个导出值是一张自己的无参子图，使用处各自调用（`examples/emu/c_thunk.cml`，这就是今天 `math::pi()` 的做法）。【实测】`read_config runs` 打印两次。无参函数不是值：调用的结果不在调用处之间共享。对 `pi` 这样的纯常量无所谓，对 `let cfg = read_config()` 就是错的。

### 2.4 (d) 惰性单元

**规则。** 函数里用到模块级的值时，在函数自己的图里放一个专门的节点，按名字指向那个值；运行时第一次执行到它才求值，结果存进一个全程序的单元，以后直接取。

- 好处：值不用沿调用链传，函数的端口不增加；只有运行时真的走到才求值。
- **违反假设**：函数图里的节点指名了 root（或某个模块）里的一个节点，这就是跨图的节点引用，只是用名字代替了边。
- 依赖不在边上，凡是沿边工作的东西都看不见它：按依赖求值的可达性、`EffectNotExecuted`、`wait` 的写入追踪、内联与特化、自动微分、导出器。每一个都要单独认识这种节点。
- 要动底层：四个后端、即时编译、`std::cpp` 都要有“一次性单元”，并行调度下两个任务同时第一次用到同一个值时要加锁。负责人的判据是这类改动要三思。
- 求值时机取决于运行路径，与今天的行为（1.3）和已有测试不同。

不推荐。

### 2.5 比较

| | (a) 物理合并 | (b) 层次化 | (c) 调用模块 root | (d) 惰性单元 |
|---|---|---|---|---|
| 无跨图节点引用 | 是 | 是 | 是 | 否 |
| 函数是子图、调用不改变其结构 | 是 | 是 | 是 | 是 |
| 求值一次 | 是 | 是 | 菱形导入时否 | 靠运行时单元 |
| 没人用时不求值 | 逐个值 | 逐个模块 | 逐个模块 | 逐个值，且按运行路径 |
| “let 是 root 上的一个节点” | 字面成立 | 是模块 root 上的节点 | 同 (b) | 不是节点间的边 |
| 例子的图（全部图合计） | 6 张图、24 节点、19 边 | 7 张图、29 节点、23 边 | — | — |
| 新的节点种类 / 运行时机制 | 无 | 无 | 无 | 有 |
| 跨模块的值依赖 | 只要求值之间无环 | 要求模块之间无环 | 同 (b) | 运行时才发现环 |
| 分离编译 | 模块产出“顶层片段”，链接时拼进 root（见 7.14） | 模块 root 本身就是单元 | 同 (b) | 最容易 |

(b) 唯一胜过 (a) 的是最后一行。7.14 说明 (a) 怎样留出同样的余地：模块单独编译时产出的“顶层片段”就是 (b) 里的模块 root，链接就是把它们并进公共 root。换句话说，**(b) 可以是 (a) 的分离编译形式，(a) 是它链接之后的形式**；两者不必二选一，但程序运行的那张图应当是 (a)。

### 2.6 对单图容量上限的影响

(a) 把所有模块的顶层节点放进一张图。用仓库里的真实程序量一下 root 有多大。`python3 docs/design/module-root/examples/tools/root_stats.py examples exemplars benchmarks test/cases`（这里的四个目录是仓库根目录下的）【实测】：

```
files: 929  with main: 910  compiled: 673
root nodes: min 2 median 2 max 9 sum over all programs 1432
      2-3 nodes: 645 programs
     4-10 nodes: 28 programs
    11-50 nodes: 0 programs
programs with a top-level let/var: 17
```

- 673 个能编译的程序里，root 的中位数是 2 个节点（函数值加调用），最大 9 个；有顶层 `let`/`var` 的只有 17 个。**把这 673 个程序的 root 全部加在一起是 1432 个节点，是上限 65,535 的 2.2%。**
- root 上每个顶层声明的节点数就是它的初始化表达式的节点数：`let k = 10` 是 1 个，`let cfg = read_config()` 是 1 个，`let tau = 2.0 * math::pi()` 是 3 个。一个很大的数组或张量字面量是 1 个 `DATA` 节点。
- 没被依赖的顶层节点不进运行时图（1.3），所以运行时 root 的大小只取决于程序实际用到的值。
- 边：root 上 `FILL` 的 `WITH` 入边数等于 `main` 传递使用的模块级值的个数；每个函数图多出的端口数等于它传递使用的模块级值的个数（3.2）。

容量不是 (a) 的约束。真正会把 root 撑大的是内联（`std::opt` 把 `main` 并进 root），那是今天就有的情况，与合并无关。

---

## 3. 函数如何取得 root 上的值

### 3.1 现有的捕获机制能否原样用于另一个模块顶层的值

机制可以原样用，**名字的绑定方式不能**。要改两处：

1. **捕获的键。** 今天是裸名字，在调用处的作用域里解析（1.2 规则 3）。改为：被捕获的是某个模块顶层的值时，键是“模块 + 名字”，调用处直接向那个模块的顶层作用域取节点，再照常沿外层图加端口。因为所有函数图的最外层都是同一张 root，这一步总能走到。
2. **建图器里按模块各存一份的状态要变成全程序一份**：哪些图已经被调用过（决定捕获端口是否已改为 `with` 端口）、每个函数捕获并写了哪些变量、哪些节点是 `var`、哪些图带效应【读码 `builder.cpp:712-722` 每次 `build` 都清空 `usedGraphs_`、`variables_`、`captures_`、`capturePortNames_`、`effectfulGraphs_`】。例如：`lib` 的函数 `f` 捕获了 `k`，模块 B 第一次调用它时把捕获端口改成了 `with` 端口；模块 C 再“第一次”调用它时，自己的记录里 `f` 还没被用过，走的是不连 `WITH` 边的分支【读码 `builder.cpp:2734-2738` 对比 `2790-2797`】，`f` 就读到没人给过值的端口。今天这条路径走不到，是因为 1.5 (1) 的错误先发生了。

### 3.2 值沿调用链传下去，图上多出多少

对每个模块级的值 `v`：凡是“自己用到 `v`，或调用了这样的函数”的函数，各多一个端口；这些函数的每一个调用处各多一条 `WITH` 边；作为值传递时多一张转发图（把捕获的值补上再调用，`builder.cpp:2852-2915`）。

`examples/uses/main.cml` 里一个 `k` 被 6 处用到（直接调用、递归、泛型实例、函数作为值、带 `with` 实参的函数、两个元素函数）。与把 `k` 换成字面量 `10` 的同一个程序（`main_literal.cml`）比较【实测 `std::stats` 与 `std::rgir`】：

| | 用顶层的 `k` | 用字面量 | 差 |
|---|---|---|---|
| 图 | 12 | 10 | +2（两张转发图） |
| 节点 | 74 | 60 | +14 |
| 边 | 75 | 55 | +20 |
| 端口节点 | 21 | 9 | +12 |
| `WITH` 边 | 21 | 8 | +13 |

代价与“用到它的函数个数 + 调用处个数”成正比，与调用深度无关。对执行的影响是每次调用多传一个槽位。常量会被现有的遍消掉：`std::macro std::opt` 之后 `examples/macro_let/main.cml` 里用到常量的函数被特化，端口没有了（1.6）。

这一代价是今天单个模块里就有的，(a) 不增加它，只是让它也适用于别的模块的值。

### 3.3 各种情形

全部在单个模块内【实测】成立（1.6，`examples/uses/main.cml`）；(a) 之下跨模块时图的形状相同，只是值的节点属于另一个模块。

| 情形 | 图上是什么 |
|---|---|
| 递归函数 | 函数有捕获端口；递归调用处把自己的端口再传给自己（`WITH`）。每一层多传一个槽位 |
| 泛型实例 | 每个实例是一张普通的函数图，有自己的捕获端口。今天实例建在使用方模块的 root 下所以不能捕获声明方的值；只有一张 root 之后这个障碍不存在（第 6 节） |
| 作为值传递的函数 | 函数值没有地方放 `with` 实参，所以取值处建一张转发图：它捕获同样的名字，再调用原函数。转发图是闭包，值在 `FILL` 时装进函数值 |
| 部分应用 `let g = f<w>` | 设计原则 4.4 已定，**尚未实现**：`examples/misc/partial.cml` 是 `ArgumentsMismatch`【实测】。实现之后它与上一行是同一种东西：一个装好了部分实参的函数值，模块级的值与 `w` 一起装入。带 `with` 实参的直接调用 `scale<3>(2)` 今天可用 |
| 高阶算子的元素函数 | lambda 是闭包，建闭包时（`FILL`）装入捕获的值，`map` 的每次元素调用从函数值里取。`examples/par/main.cml` 在 `std::tf` 上八个元素并行，值只求了一次 |

### 3.4 顶层的 `var`

`var` 是共享单元，捕获的是单元的引用。单个模块内读写都已经对（1.6）。跨模块缺的是 3.1 的两处：键要带模块；“这个函数（传递地）写了哪些捕获的变量”这份摘要要全程序可见，调用处才能把调用记为一次写入并插入控制边（设计原则 3.5 最后一条）。今天这份摘要按模块各存一份【读码 `builder.cpp:1860-1937`】，导入的函数查不到。

所以**“导出的函数读写本模块顶层的 `var`”可以随 (a) 一并做对**，不需要另外的机制：

```camel
// counter.cml
module counter
var cnt = 0
export sync func bump(): int sync { wait cnt += 1; return cnt }
export func get(): int { return cnt }
```

合并之后 `cnt` 的单元是 root 上的一个节点；`bump` 与 `get` 各有一个带 `var` 标记的捕获端口；导入方的 `main` 调用它们，自己也得到这个端口，root 把单元交给 `main`。`examples/var_lib/` 今天是 `UnresolvedReference: 'cnt'`【实测】。

**导出 `var` 本身**（`export var cnt`，别的模块直接写 `counter::cnt = 5`）：建议不允许，见 Q4。

---

## 4. 求值语义的细节

### 4.1 求值一次，还是每次用到都求值

一次。模块级的 `let` 是 root 上的一个节点；root 这张图在一次运行里只执行一次；节点的值存在 root 这一帧的槽位里，交给入口调用时写进 `main` 的函数值，再经 `WITH` 实参逐层写进被调函数的帧【读码 `nodevm.cpp:60-95`、`fastvm.cpp:209-224`、`taskflow.cpp:1666-1677`】。函数拿到的是值（对象则是同一个对象的引用），不会再去求值。

- **并行调度器下两个任务同时需要同一个顶层值**：不存在竞争。值在入口调用之前就已经算完，两个任务各自的帧里已经有它。`examples/par/main.cml` 在 `std::tf` 上【实测】只打印一次 `evaluating common`。这是 (a) 相对 (d) 的主要好处：没有“第一次使用”这个时刻，就不需要锁。
- **宏求值器**：`std::macro` 把参数全是常量的 `macro` 调用换成结果。顶层的 `let k = square(12)` 在 root 上被换成 `DATA 144`【实测】，之后它对所有使用者就是一个常量。求值器执行一张图时用的是同一套“从出口可达”的顺序【读码 `macro/eval.cpp:715`】，被提前求值的函数用到的捕获值随函数值带入【读码 `eval.cpp:682-693`】。一个 `macro` 函数捕获了**不是常量**的顶层值时，它的调用不会被提前求值（捕获的值是调用的一个输入，不是常量）：`examples/misc/macro_capture.cml` 加不加 `std::macro` 都是先求顶层值、再进 `main`，各一次【实测】。
- **`var`**：单元只有一个，所有捕获者看到同一个单元。并行下多处无 `wait` 的写入是设计原则第 15 节里未定的问题，不在本稿范围内。

### 4.2 带副作用的顶层 `let` 与同步块

- **root 不是同步块**（1.3，【实测】）。两个带效应的顶层 `let` 之间没有顺序；并行调度器可以同时执行它们。这与 3.1 一致，也是“不是命令式脚本语言”的含义。建议保持（Q3）。
- 需要顺序的初始化写进一个函数：`sync func init(): State sync { ... }`，顶层 `let state = init()`。或者用数据依赖把它们串起来（效应函数也返回值，设计原则 3.1）。
- 顶层的 `wait` 与别处含义相同（1.3，【实测】）。顶层不能写 `sync { ... }` 块作为初始化表达式（`examples/misc/top_sync.cml` 是语法错误【实测】）。
- **顶层值全部在 `main` 开始之前求值**（1.3）。`let cfg = read_config()` 的读文件发生在 `main` 的第一条语句之前。见 Q2。

### 4.3 入口模块顶层与被导入模块顶层

合并之后没有区别：都是同一张 root 上的节点，规则相同。入口模块的特殊之处只剩一条：root 的出口是对**它的** `main` 的调用。被导入模块里名叫 `main` 的函数是一个普通函数（Q8）。

### 4.4 不会执行的顶层效应：编译器的提示

顶层不能写表达式语句（1.3），所以“`println('loading')` 写在模块顶层”只能是 `let _x = println('loading')`。

现有的 `EffectNotExecuted`【实测】覆盖入口模块：带效应、且 `main` 的调用不依赖的顶层 `let` 给警告（`examples/now_effects/`）。不覆盖被导入的模块（`examples/lib_effects/` 没有任何提示）。

合并之后建议的规则（Q7）：在公共 root 建完、入口调用接好之后检查一次。

| 顶层带效应的 `let` | 提示 |
|---|---|
| 入口模块里，`main` 的调用不依赖它 | 警告（与今天相同） |
| 被导入模块里，**没有导出**，并且模块内没有任何函数或值用到它 | 警告：没有任何人能依赖它，一定是写错了 |
| 被导入模块里，导出了，或被本模块的函数用到，只是这个程序没走到 | 不提示：库的值没被某个程序用到是正常的 |

---

## 5. 导入的一般情形

### 5.1 菱形导入

模块按名字缓存，只编译一次【实测、读码，1.5】。(a) 之下 `d` 的顶层节点只进 root 一次，因为它们只被建一次；`b` 与 `c` 导入 `d` 的同一个值，得到的是同一个节点。不需要新的处理。

### 5.2 互相导入

现在是段错误（1.5）。两件事要分开：

**函数之间互相调用**应当允许：图引用图本来就能成环（同一个模块里的互递归今天就是这样）。障碍在加载过程：模块 `a` 在建自己的 GCT 时遇到 `import b` 就把 `b` 完整编译完【读码 `src/compile/gct/builder.cpp:413-470`、`context.cpp:339-354`】，而 `b` 又需要 `a` 的导出表，此时 `a` 还什么都没有。要支持，加载得分两个阶段：先把导入闭包里的所有模块都解析并登记声明（函数的签名、类型、顶层值的名字），再建函数体与顶层节点。没有写返回类型的函数要靠函数体推出类型，环上的函数因此需要写明返回类型，否则报错。这是一项独立的、规模不小的工作（第 8 节第 8 步）。

**顶层值之间的循环依赖**是编译期错误：root 是一张无环图。

- 判定方法：顶层值按需建图。建某个值的初始化表达式时把它标为“正在建”；引用到别的顶层值时，若它还没建就先去建它；引用到一个“正在建”的值就是环。
- 诊断：`ModuleValueCycle`（新），定位在闭合环的那次引用，消息列出整条链，每一环带位置：`a::x (a.cml:3) → b::y (b.cml:5) → a::x`。
- 函数参与的环：`let a = f()` 而 `f` 的函数体用到 `a`。今天同一个模块里已经有诊断 `ClosureCaptureAfterSelfCall`【实测 `examples/misc/self_cycle2.cml`】，但消息说的是“函数已经调用了自己”，位置也没有；应当并入同一条诊断。
- **在不支持互相导入、顶层 `let` 只能引用前面的 `let` 的前提下，值的环不可能出现**：一个值只能引用本模块里写在它前面的值，和已经完整编译的被导入模块的值。所以这条判定只在第 8 步之后才需要。

建议的次序见 Q5：先把互相导入改成带环路的 `ImportCycle`（去掉段错误），两阶段加载以后单独做。

### 5.3 同名冲突与改名

设计原则 10.2：两个导入的名字冲突是编译期错误；导入可以改名。现状（1.5）：改名未实现；同名同签名静默取其一；同名不同签名合并成重载集。

有了值之后规则应当是：

| 两个导入同名 | 处理 |
|---|---|
| 两个都是值，或一个是值一个是函数 | 编译期错误 `ImportNameConflict`，指出两条 `import`，建议改名 |
| 两个都是函数，签名相同 | 编译期错误（现在静默取其一，是缺陷 D5） |
| 两个都是函数，签名不同 | 见 Q11：按 10.2 的字面是错误；现状是合并成重载集 |
| 导入的名字与本模块顶层声明同名 | 本模块的声明优先（现状【读码 `builder.cpp:2165-2240` 先查本模块】）；建议也给警告 |

改名 `import { find as str_find } from str` 需要语法与模块绑定表的支持；绑定表已经能表达“本地名 → 远端名”【读码 `module.cpp:173-184` 的 `markImportedRefFromMod(localRef, remoteRef, mod)`】，缺的是语法。它与合并 root 无关，可以独立做（第 8 节第 7 步）。

### 5.4 整模块导入

`import math` 之后 `math::pi`：与 `import { pi } from math` 走同一条路，限定名经命名空间绑定解析到同一个导出实体【读码 `module.cpp:224-251`】，得到同一个 root 节点。今天 `lib::pi` 与 `pi` 是同一个段错误【实测】，修好一个就修好另一个。`using namespace lib` 同理。

### 5.5 C++ 模块（`.cmo`）导出值

C++ 模块没有 root 图，只有算子。今天的常量是无参的纯算子，`math::pi()` 在图上是一个没有输入的 `OPER math:pi` 节点【实测，1.5】。两种做法：

| | 做法 | 评价 |
|---|---|---|
| (i) `DATA` 节点 | 模块在加载时交出一个值，导入方的 root 上放一个 `DATA` 节点 | 只适用于加载时就能确定的值；字符串、张量这类对象要在程序运行之前跨模块边界交给 Camel，碰到 10.5“模块不得跨调用保存指向 Camel 对象的裸指针”那组规则 |
| (ii) 无输入的算子节点 | 模块声明“`pi` 是一个值，由算子 `math:pi` 给出”。第一次引用它时在公共 root 上建**一个** `OPER math:pi` 节点，之后所有引用都指向它 | 与 Camel 模块的 `let pi = <表达式>` 完全同构：root 上的一个节点，被依赖才求值，求值一次。纯算子被 `std::opt::fold` 折成常量【实测】。不纯的也能表达（`process::args`、`os::cpu_count`）。模块接口只多一个“这是值不是函数”的标记 |

建议 (ii)。用户写 `math::pi`，没有括号；图上与今天 `math::pi()` 所在位置的那个节点是同一种节点，只是它在 root 上、全程序一个。`pi()` 的去留见 Q6。标准库的签名文件相应地从 `inner func pi(): float` 改成值的声明（写法待定）。

### 5.6 用 Camel 写的标准库模块

`stdlib/array.cml` 全部是泛型函数，今天因为 1.5 (3) 的限制不能有模块级的常量。合并之后它与用户模块没有区别：可以有顶层的 `let`，泛型函数可以用它（第 6 节）。

---

## 6. 与现有机制统一

导入函数、导入泛型模板、导入值是同一件事的三种情况：**导入是把一个名字绑定到已经挂在公共 root 上的某样东西，不复制、不执行、不建节点。**

| 导出的是 | 它是什么 | 使用处在图上是什么 |
|---|---|---|
| 值 | root 上的一个节点 | 同一张图里是一条边；函数里是捕获端口 |
| 函数 | root 之下的一张子图 | `FUNC` 节点（图引用图） |
| 泛型函数 | 模板；它的每个实例是 root 之下的一张子图 | 指向实例的 `FUNC` 节点 |
| C++ 算子 | 不透明的算子 | `OPER` 节点 |
| C++ 模块的值 | root 上的一个无输入 `OPER` 节点 | 同“值” |
| 类型 | 建图时求值，图上没有节点 | — |

**泛型模板的那组规则怎么简化。** 今天的规则是“实例建在使用方模块的 root 之下、函数体里的名字回声明方模块解析、不能用声明方顶层的值”，围绕它有 `homeRoot`、`GenericInstance::owner`、`foreignInstanceOf_`、带 root 地址的作用域名【读码 `builder.cpp:1356`、`1481-1489`、`1575-1604`】。只有一张 root 之后：

- 实例建在 root 之下。root 只有一张，“使用方的 root 还是声明方的 root”不再是一个可以问的问题。
- 名字回声明方模块解析：不变，这是词法作用域。
- 不能用声明方顶层的值：这条限制消失。声明方顶层的值就在包着实例的那张图（root）上，照常捕获。
- 每组类型一个实例，全程序共用：实例表本来就挂在模板上，去掉“按使用方 root 区分”的条件即可。

**`DESIGN-QUESTION(generic-export)`（实例建在使用方，还是声明方持有全程序唯一实例）在合并 root 之后自然消失**：两种答案在图上是同一张图——root 之下的一张子图，每组类型一张。原来“声明方持有”的顾虑（“声明模块的图在编译完之后还会变”，技术文档 28 第 5 节）也不成立了：变的是公共 root 的子图表，不是哪个模块的图。剩下的只是实例的名字要不要带模块前缀（建议带，如 `array::reverse<int>`）。

---

## 7. 对各子系统的影响

按 (a)。改动量是估计的行数量级。

### 7.1 模块加载与名字解析

- `Context` 持有程序的 root（编译期的图）与“模块 → 顶层作用域”的表；`UserDefinedModule` 不再各自持有 root，`compileGraph_` 改为指向公共 root【`src/core/module/userdef.cpp:75-82`、`context.cpp:619-632`】。约 100 行。
- 导出表里的“节点”实体今天是一个裸的 `new node_handle_t`【`builder.cpp:4846-4851`】，改为“模块 + 名字”，使用时向表里取。约 30 行。
- 互相导入的检测：`context.cpp:330-338` 的判断从“导入自己”扩大为“导入一个正在加载的模块”，诊断带链。约 40 行。
- 两阶段加载（Q5，可选）：`src/compile/gct/builder.cpp:413-470`、`userdef.cpp:86-155`、`context.cpp:313-400`。300–600 行，风险高。

### 7.2 GIR 构建

`src/compile/gir/builder.cpp`，约 300–500 行，是主要工作：

- `build`（`:707-858`）：不再新建 root 而是取公共 root；非入口模块不放占位返回；入口调用只为入口模块建。
- 3.1 的两处：捕获键带模块（`:984-1042`、`:2740-2797`、`:2816-2819`、`:2892-2894`、`:1916-1937`）；`usedGraphs_`、`variables_`、`captures_`、`capturePortNames_`、`effectfulGraphs_`、`armGraphs_` 从每次 `build` 清空（`:712-722`）改为全程序一份。最稳妥的做法是全程序一个建图器对象、每个模块一棵作用域树，而不是每个模块一个建图器。
- 导入值：`visitDRefNode` 的 `isNode` 分支（`:2252-2256`）去掉断言，取到节点后走与本模块顶层值相同的路径。
- 泛型：删掉 `homeRoot`、`owner`、`foreignInstanceOf_` 及 `:988-1014` 的拒绝（第 6 节）。净减代码。
- `EffectNotExecuted`：`:826` 的检查改为对公共 root 做一次，按 4.4 的规则。

### 7.3 图的数据结构、提交校验与可达闭包

**不需要改数据结构。** 没有新的节点种类、边种类或图之间的关系；root 仍然是一张普通的图，函数图的外层图仍然是 root。

- 校验（`src/compile/gir/validate.cpp`，含分支臂不变量）：逐图检查，不关心模块【读码】。
- 可达闭包（`reachable.cpp:36-79`、运行时的 `collectReachableGraphs`）：从 root 出发，沿子图表、依赖表、函数值、`FUNC` 节点。合并之后 root 的子图表包含所有模块的所有函数，**包括程序没用到的**。编码与各遍按“可达”收集图，会把没用到的库函数也带上。建议编译期的 root 子图表照常登记，运行时图的收集只沿依赖表、函数值与 `FUNC` 节点（今天 `reachable.cpp:45-47` 还沿子图表）；或者保持现状，接受没用到的函数进入运行时图。规模 20–50 行，需要看各遍是否依赖“子图表里的图一定可达”【未查】。
- 图的名字：两个模块可以有同名函数，子图表是按指针存的向量【读码 `draft_graph_builder.cpp:270-274`】，不冲突；转储里建议带模块前缀。

### 7.4 各个遍

- 内联、特化、去虚化（`src/passes/opt/`）：处理的是带 `with` 端口的函数与闭包，今天就在处理“用到 root 上的值的函数”，不区分值来自哪个模块。【实测】单模块下 `std::opt::inline`、`std::opt`、`std::opt` 结果正确（1.6、2.1）。不需要改。
- `std::macro`：模块级常量的提前求值今天就工作（1.6）。`math::pi` 按 5.5 (ii) 是 root 上的纯算子节点，`std::opt::fold` 折成常量，使用它的函数再被特化——与 `examples/misc/pi_today.cml` 今天的结果（root 上 `DATA 6.28319`）相同。不需要改。
- 一处要留意：`std::opt::inline` 把顶层值的初始化函数并进 root（2.1 的例子内联后 root 上出现 `println` 等节点【实测】）。库多了以后 root 会因此变大；内联已经有“目标图不超过 4096 节点”的上限（`docs/cli.md`“Resource limits”），够用。

### 7.5 四个后端

`std::nvm`、`std::fvm`、`std::jit`、`std::tf`：root 是普通的图，执行一次；顶层值的“缓存”就是 root 那一帧的槽位与函数值里的捕获元组（4.1）。**不需要改。**【实测】单模块下四个后端一致。

### 7.6 垃圾回收

root 上的值在整个运行期间都被 `main` 的函数值与各层帧引用，是长寿命的对象；它们经由正常的帧与函数值被追踪，不需要另外登记为根【未查：没有读回收器对 root 帧的处理，依据是单模块下顶层张量在各后端上工作正常】。图的管理（`GCGraphManager`，`src/runtime/graph.cpp:1546-1616`）以一张 root 为根收集可达的图，本来就是“一个程序一张 root”，不需要改。分代回收启用之后，root 上的值应当直接进老年代，这是那时的优化项。

### 7.7 自动微分

被求导的函数用到模块级的值：**是常数。** 【实测 `examples/ad_let/`】`grad(loss)` 对 `loss` 的参数求导，捕获的 `w` 不在求导变量里。要对它求导，把它写成参数（设计原则 3.5：`with` 参数表达权重）：`with <w: Tensor> func loss(x: Tensor)`，调用处 `loss<w>(x)`，顶层的 `let w` 作为实参传入。建议保持（Q9），不需要改代码。模块级的 `var` 出现在被求导函数里按 3.5 已定的规则拒绝。

### 7.8 ONNX 导出

`onnx::export` 翻译的就是入口图（root）【读码 `modules/onnx/exporter.cpp:3970-4022`】。模块顶层的张量是 root 上的节点，经 `std::macro std::opt` 折成常量后成为初始化器：【实测 `examples/onnx_let/`】输入 `['x']`，初始化器 `[('const', [2, 2])]`，节点 `MatMul, Tanh, Identity`。**顶层放权重今天在单个模块里就能导出**；合并之后权重放在另一个模块（`import { w } from weights`）也一样，因为它们在同一张 root 上。

建议的改进：初始化器用顶层 `let` 的名字命名（`w`，跨模块时 `weights::w`）。今天只有 `--arg` 绑定的对象有名字【读码 `exporter.cpp:4005-4010`】。实现上是把“顶层声明的名字”作为 root 节点的调试信息带到导出器，约 50 行。`onnx.export_model` 导出某个函数时，函数捕获的模块级值作为捕获的常量进入模型，路径相同【未查：没有运行跨捕获的 `export_model`】。

### 7.9 `std::cpp` 转译

捕获的值在生成的 C++ 里是函数的普通参数【实测 `camel examples/now_let/main.cml std::cpp`：`f_add_k(Int64 v_arg_0, Int64 v_arg_6)`，入口是 `generated::f_main(10LL)`】。入口处的值只支持字面量：顶层值是算出来的时候（`let k = seven()`）转译器段错误【实测 `examples/defects/cpp_computed_let.cml`，附录 B D4】。合并 root 不改变这一点，但库模块里算出来的顶层值会更常见，所以需要让 `std::cpp` 把 root 上入口调用之前的节点也翻译出来，或者拒绝并给出诊断。`runtime_emitter.cpp:1005-1060`，约 100 行。

### 7.10 语言服务器的查询层

- 查询层把每个模块的文件、语法索引、诊断分别登记【读码 `src/service/semantic/analyze.cpp:517-571`】，不依赖“一个模块一张 root”。
- 导入的名字的种类只有函数、类型、命名空间【读码 `include/camel/service/semantic/model.h:144`、`analyze.cpp:801-845` 把节点实体当作函数描述】：要加“值”，带类型与定义位置，悬停、补全、跳转才对。约 80 行。
- 引用记录：`valueReferenced` 用 `declarationOf(name)` 在当前模块的作用域里找声明【读码 `builder.cpp:1118-1132`、`2176-2182`】，导入的值要给出另一个文件里的声明位置。约 30 行。

### 7.11 调试器与 `camel inspect`

- 两者都从入口模块取编译图【读码 `tools/debugger/src/compile.cpp:69-80`、`tools/camel-cli/main.cpp:312-331`】，改为取公共 root，各几行。
- 调试器的图节点视图里，root 上会出现别的模块的值；按节点的源文件分组显示即可。按行单步时，顶层值的求值发生在进入 `main` 之前（今天也是）。
- `camel inspect --gir` 打印的 root 是全程序的 root。

### 7.12 随机程序生成器 `test/tools/progen`

今天生成的是单个文件、只有函数与 `main`，没有模块级的值，也没有第二个模块【读码 `generate.py:934-970`；`test/cases/opt/progen/README.md:493`“No finding uses ... a module-level variable”】。建议加两个特性开关：

- `toplevel`：模块顶层的 `let`（常量、纯计算、带效应且被用到 / 没被用到）与 `var`；函数经各种途径使用它们。参考输出按“被 `main` 静态依赖的在 `main` 之前求值，各一次，顺序无约定”计算；带效应的顶层值之间没有顺序，所以生成时每个程序至多一个带打印的顶层值，或让它们有数据依赖。
- `modules`：把一部分函数与顶层值放进第二、第三个文件并导出，包含菱形导入。参考输出与单文件相同——这正是 (a) 的性质，生成器可以先生成单文件程序再切分，两种形态互为对照。

约 300 行 Python。它应当在第 2 步合入之前就绪，用来检验合并。

### 7.13 文档与测试

`docs/language/05_modules.md`（“模块不能导出值”一条、5.2 的改名）、`07_statements.md:36`、`08_top_level_and_grammar.md`、`docs/technical/28` 第 5 节、`docs/stdlib-reference.md`（`pi()`）、`modules/math/numeric.cpp:189` 与 `stdlib/array.cml` 头部的 `DESIGN-QUESTION` 注释。测试：`test/cases/compile/generics/err_export_capture.cml` 从“应当报错”变成“应当通过”；转储 root 的金标文件会变。

### 7.14 增量与分离编译

今天没有编译缓存：每次运行都从源码编译所有模块【读码 `context.cpp:339-354`、`userdef.cpp:86-155`】。问题是 (a) 会不会把将来的路堵死。不会，条件是守住下面三条：

1. **一个模块对公共 root 的贡献是一个可以单独描述的片段**：它的顶层节点、节点之间的边、它引用了哪些别的模块的值（按“模块 + 名字”）、它导出了哪些节点。建图时每个 root 节点记下所属模块（源码位置里已经有文件）。这个片段就是 (b) 里的模块 root；将来缓存的单元是它，链接就是把片段并进 root、按名字接上引用。
2. **跨模块的引用只经过“模块 + 名字”这张表**，建图器不保存指向别的模块节点的裸指针（今天导出表里正是这样一个裸指针，7.1）。
3. **函数的捕获列表按名字记**（今天就是）。函数图的 `with` 端口取决于它传递使用了哪些模块级的值，被调用模块内部一改，调用方的 `WITH` 边就要变；按名字记，链接时重接即可，不需要重编调用方的函数体。

合并不是“把所有模块编成一张图”：函数仍然各是各的图，合并的只是每个模块那几个顶层节点。

---

## 8. 分步实施计划

每一步都能独立合入，合入后全套测试通过。规模是净改动的估计。

| 步 | 内容 | 规模 | 风险 | 之后用户能做什么 |
|---|---|---|---|---|
| 0 | **把四处崩溃与静默错误改成诊断**（不改语义）：导入的值被使用、互相导入、导入的函数捕获了本模块顶层的值、同名同签名的两个导入；`std::cpp` 遇到算出来的顶层值时拒绝 | 约 150 行 + 测试 | 低 | 不再段错误、不再算错 |
| 0.5 | `progen` 的 `toplevel` 与 `modules` 两个特性（7.12）；单文件形态先进测试集 | 约 300 行 Python | 低 | — |
| 1 | **公共 root**：全程序一个建图器、每个模块一棵作用域树；所有模块的顶层节点与函数建在同一张 root 上；捕获键带模块；调试器与 `inspect` 取公共 root（7.1、7.2、7.11） | 300–500 行 | 中：转储金标变化；建图器里按模块各存一份的状态要逐个确认 | 导出的函数可以用自己模块顶层的 `let` 与 `var`（修正 D1；`counter` 模块可以写了） |
| 2 | **导入值**：去掉 `isNode` 分支的断言；`import { pi } from lib` 与 `lib::pi`；查询层加“值”；改文档 | 约 150 行 + 测试 | 低 | **`export let pi = 3.14159` 可用** |
| 3 | 泛型：实例建在公共 root 下、全程序每组类型一个；删去跨模块不能用顶层值的限制与相关代码（第 6 节） | 净减约 100 行 | 低到中 | `stdlib/array.cml` 这样的库可以有模块级常量 |
| 4 | 提示与显示：`EffectNotExecuted` 按 4.4 的规则覆盖被导入模块；`std::rgir` 的捕获端口与函数图带模块名，修正端口名错位（D6）；ONNX 初始化器用顶层声明的名字 | 约 150 行 | 低 | 图更好读；导出的模型里权重有名字 |
| 5 | C++ 模块的值（5.5 (ii)）：模块接口加“值”的声明；`math::pi`、`math::e` 等；签名文件与标准库参考 | 约 200 行，动模块接口 | 中：公开接口 | `math::pi` |
| 6 | `std::cpp`：翻译 root 上入口调用之前的节点（7.9） | 约 100 行 | 低 | 有顶层计算的程序可以转译 |
| 7 | 导入改名与冲突诊断（5.3）。与其余各步无依赖，可以随时做 | 语法 + 约 100 行 | 低 | `import { find as str_find } from str` |
| 8 | 两阶段加载：模块互相导入；顶层值按需建图与 `ModuleValueCycle`；顶层声明与书写顺序无关（Q5、Q10） | 300–600 行 | 高 | 互相导入；顶层 `let` 不必先声明后使用 |

第 1 步是唯一有分量的一步；第 0 步值得先做，它让现有行为先站稳，也让第 1 步的每个改动都有一条原来报错、现在通过的测试。

---

## 9. 需要负责人裁定的问题

### Q1 合并方式

程序与四种图见第 2 节。

- (a) 全程序一张 root，各模块的顶层节点都在上面。
- (b) 公共 root 下每个模块一张 root 子图，导出值是输出。没被用到的导出值也会求值（2.2 中 `build_table runs` 被打印）。
- (c) 导入方调用被导入模块的 root。菱形导入时求值两次。
- (d) 惰性单元。跨图引用，要动后端。

**建议 (a)。** 它与 10.1 的文字一字不差，图就是今天单文件程序的图，不需要新的节点种类、数据结构或运行时机制。

### Q2 求值时机：`main` 之前，还是首次用到时

```camel
let cfg = read_config()                   // 读文件，带效应
func port(): int { return cfg.1 }
func main(debug: bool): int sync {
    println('starting')
    if debug then println(port()) else println('no debug')
    return 0
}
```

按 (a)（也是现状，1.3）：`read_config` 在 `starting` 之前执行，`debug` 为假时也执行。图上 `cfg` 是 root 的节点，经 `FILL → CALL` 交给 `main`。

- 甲：保持。“被依赖”是图上的静态依赖；顶层值在入口调用之前求值。
- 乙：首次用到时求值。这就是 (d)。

**建议甲。** 它是“值是 root 上的一个节点”的直接结果，与函数内部的规则一致（分支臂捕获的值也在分支之前求值），并行下不需要锁，已有测试锁定。想推迟的计算写成函数：`func cfg(): Config`，谁需要谁调用（代价是每次调用都算）；或者在 `main` 里算好再往下传。

### Q3 root 是不是同步块

```camel
let a = open_log('a.log')
let b = open_log('b.log')
```

现状【实测，1.3】：不是；两者没有顺序，并行调度器可以同时执行。

**建议保持不是。** 需要顺序时写进一个 `sync` 函数体，或用数据依赖串起来。把 root 定为同步块等于让模块顶层成为一段脚本，并且跨模块时“谁的顶层先执行”没有自然的答案。

### Q4 是否允许导出 `var`

```camel
// counter.cml
export var cnt = 0
// main.cml
import { cnt } from counter
sync func main(): int sync { cnt = 5; ... }
```

图上没有困难：`cnt` 的单元是 root 上的一个节点，`main` 捕获它的引用，与本模块里的 `var` 一样。

- 甲：不允许导出 `var`，是编译期错误，提示导出读写它的函数（3.4 的 `bump`/`get`）。
- 乙：允许导入方读，不允许写。
- 丙：允许读写。

**建议甲。** 设计原则 3.5：`var` 只是为了方便留的口子，应当尽量少用。导出 `var` 让一个模块的状态可以被任何模块在任何地方改写，每一处都要靠 `wait` 才有顺序；导出函数则把对它的写入收在一个模块里，函数的 `sync` 修饰符与写入摘要仍然让调用处得到正确的控制边。乙是折中，但“另一个模块什么时候读到的是哪个值”同样依赖 `wait`，没有省掉问题。

### Q5 模块互相导入

```camel
// a.cml                                   // b.cml
import { odd } from b                      import { even } from a
export func even(n: int): bool {           export func odd(n: int): bool {
    return if n == 0 then true                 return if n == 0 then false
           else odd(n - 1)                            else even(n - 1)
}                                          }
```

图上没有困难：`even` 与 `odd` 是 root 之下的两张子图，互相有 `FUNC` 节点，与同一个模块里的互递归相同。困难在加载过程（5.2）。现状是段错误。

- 甲：不支持。互相导入是 `ImportCycle`，诊断列出环路。把互相依赖的函数放进同一个模块。
- 乙：支持函数互相调用（两阶段加载）；顶层值成环是 `ModuleValueCycle`。

**建议分两步：先甲（第 0 步，去掉段错误），乙作为第 8 步单独做。** 导出值不依赖它。是否做乙、什么时候做，请负责人定。

### Q6 C++ 模块的值与 `pi()`

```camel
import math
let tau = 2.0 * math::pi          // 新写法
let tau = 2.0 * math::pi()        // 今天的写法
```

图（5.5 (ii)）：root 上一个无输入的 `OPER math:pi` 节点，折叠后是 `DATA`。

- 值的提供方式：(i) 加载时给一个 `DATA`；(ii) 声明为“由某个无参算子给出的值”。**建议 (ii)。**
- `pi()` 的去留：甲，删除，只留 `math::pi`；乙，两个都留一个版本，`pi()` 给弃用警告；丙，两个都长期保留。**建议乙**：现有的测试、示例与文档里有不少 `pi()`，留一个版本的时间迁移；长期只留一种写法，与标准库“只保留一种写法”的约定一致。

### Q7 被导入模块里不会执行的顶层效应

```camel
// lib.cml
module lib
let _warm = println('lib: warming up')     // 没有导出，也没人用
export let table = build_table()           // 导出了，这个程序没用
```

今天两者都不执行，也都没有提示。

**建议按 4.4 的表：** 第一个给 `EffectNotExecuted` 警告（没有任何人能依赖它）；第二个不提示。

### Q8 被导入模块里的 `main`

**建议：只有入口模块的 `main` 是入口；被导入模块里名叫 `main` 的函数是普通函数，不被调用，也不能被导入方当作入口。** 这样一个文件既可以单独运行也可以被导入。现状事实上就是这样（1.4）。

### Q9 被求导的函数里的模块级值

```camel
let w = reshape([0.5, -0.25, 0.1, 0.3], [2, 2])
func loss(x: Tensor): float { return sum(tanh(x @ w)) }
let g = grad(loss)(x)              // 只有对 x 的梯度
```

**建议：模块级的值在被求导的函数里是常数**（现状【实测】）。要对权重求导，把它写成 `with` 参数（4.4 已定），顶层的 `let w` 是调用处的实参。不提供“对捕获的模块级值求导”。

### Q10 顶层声明的书写顺序是否有意义

```camel
let a = b + 1          // 现在：UnresolvedReference 'b'
let b = 2
```

函数之间与书写顺序无关（可以调用写在后面的函数）；值不行，函数体也不能用写在它后面的顶层值【实测，1.5】。

- 甲：保持。值必须先声明后使用。
- 乙：顶层声明与书写顺序无关，只看依赖；成环是 `ModuleValueCycle`。

**建议：目标定为乙**——程序表达的是依赖，root 上节点的先后本来就不由书写顺序决定，而且函数已经如此。实现与两阶段加载是同一件事（第 8 步），在那之前保持甲，诊断把“先声明”说清楚。

### Q11 两个模块导出同名、签名不同的函数

```camel
import { f } from b        // f(x: int): int
import { f } from c2       // f(x: string): string
println(f(1), f('a'))      // 现在：2 a!
```

- 甲：编译期错误，要求改名（10.2 的字面）。
- 乙：合并成重载集（现状）；只有签名相同才是冲突。

**建议甲**，但 C++ 模块随导入自动带入的算子组（如导入 `tensor` 之后 `+` 多出张量的重载）不算在内，它们继续合并——那是算子重载的本意，用户也没有写出这个名字。签名相同时无论如何都应当是错误（现在静默取其一）。

### Q12 导出的模型里权重的名字

`examples/onnx_let/` 导出的初始化器叫 `const`。**建议用顶层声明的名字，跨模块时带模块名（`weights::w`）。**

---

## 10. 推荐，以及最不放心的地方

**推荐 (a)，按第 8 节的顺序做。** 理由归结为一条：10.1 写下的那句话——模块顶层的 `let` 是公共 root 上的一个节点，被依赖才求值——在入口模块里今天就是事实，(a) 是把它推广到所有模块所需的最小改动；(b)、(c)、(d) 各自至少违背其中一个分句。

最不放心的四处：

1. **建图器里的捕获与参数化逻辑。** 函数第一次被调用时就地把捕获端口改成 `with` 端口，之后的调用按名字补边；`usedGraphs_` 等几张表决定走哪条分支。这段逻辑今天只在一个模块内被检验过，第 1 步要把它的状态提升到全程序。端口名错位（D6）说明这里已经有没被测试覆盖的角落。第 0.5 步的生成器是为这一步准备的。
2. **值沿调用链传递的代价在权重很多的程序里会很显眼。** 顶层有 200 个权重张量、`main → forward → layer` 三层调用时，`main` 与 `forward` 各有 200 个端口，每次调用 200 条 `WITH` 边。离容量上限很远，执行上只是多传槽位，但图会难读，遍的工作量也跟着涨。常量会被特化消掉；不是常量的（训练中的权重）不会。缓解办法是 4.4 已定的写法：权重是 `with` 参数，成组地放在结构体里传。这一点需要在语言文档里讲清楚，否则“顶层放权重”会被当成推荐写法。
3. **“在 `main` 之前求值”对带效应的顶层值可能出乎意料**（Q2）。它今天就是这样，但今天几乎没有程序在顶层放带效应的值（673 个程序里 17 个有顶层声明）。可以导出值之后会多起来，`let cfg = read_config()` 会是第一个被写出来的例子。
4. **分离编译。** 7.14 的三条是为它留的余地，但没有做过原型。函数的 `with` 端口列表取决于别的模块的内部实现这一点，在有缓存之前不会暴露问题。

---

## 附录 A 复现

示例在 `docs/design/module-root/examples/`。在仓库根目录执行（附录 B 的命令省略了前缀 `docs/design/module-root/`）：

```
camel docs/design/module-root/examples/now_func/main.cml std::rgir        # 1.1
camel docs/design/module-root/examples/now_let/main.cml std::rgir         # 1.2
camel docs/design/module-root/examples/now_effects/main.cml               # 1.3
camel docs/design/module-root/examples/now_order/main.cml                 # 1.3
camel docs/design/module-root/examples/misc/two_effects.cml               # 1.3
camel docs/design/module-root/examples/lib_let/main_shadow.cml            # 1.5 (1)：打印 1001
camel docs/design/module-root/examples/emu/a_merged.cml std::rgir         # 2.1
camel docs/design/module-root/examples/emu/b_hier.cml                     # 2.2
camel docs/design/module-root/examples/emu/c_diamond.cml                  # 2.3
camel docs/design/module-root/examples/uses/main.cml std::stats std::nvm  # 3.2
python3 docs/design/module-root/examples/tools/root_stats.py examples exemplars benchmarks test/cases   # 2.6
camel docs/design/module-root/examples/onnx_let/main.cml std::macro std::opt 'onnx::export(out="/tmp/m.onnx")'   # 7.8
```

| 目录 | 内容 |
|---|---|
| `now_func/`、`now_let/`、`now_effects/`、`now_order/`、`now_var/` | 现状：导入函数、函数用顶层值、顶层效应、求值时机、顶层 `var` |
| `lib_let/`、`var_lib/`、`export_let/`、`generic_let/`、`lib_effects/` | 现状：跨模块的各种情形 |
| `diamond/`、`mutual/`、`conflict/`、`rename/` | 菱形导入、互相导入、同名、改名 |
| `emu/` | 各方案的等价写法 |
| `uses/`、`par/`、`macro_let/`、`ad_let/`、`onnx_let/` | 单模块内顶层值的各种用法、并行、宏、求导、导出 |
| `misc/` | 顶层语句、顺序、`wait`、环、部分应用、`math::pi()` |
| `defects/`、`tools/` | 缺陷的最小复现；统计脚本 |

## 附录 B 调研中发现的现有缺陷

D1–D5 已改为带位置的编译期诊断（功能本身——导出值、合并 root、按“模块 + 名字”绑定捕获——仍待裁定，没有实现）；D6 已修。最小复现保留在示例目录里，下表“现在”一列是它们今天的输出。正文第 0、1、5 节描述的是修改之前（基线 `0c842f2c`）的行为。测试：`test/plans/feat/compile/module_imports.plan.toml`。

| 编号 | 调研时的现象 | 复现 | 状态 | 现在 |
|---|---|---|---|---|
| D1 | 导入的函数用到它自己模块顶层的值时，按名字在**导入方**里找：导入方有同名的顶层值就悄悄用它（打印 `1001`，应为 `11`）；没有就是指在 `main` 上的 `UnresolvedReference` | `camel examples/lib_let/main_shadow.cml`、`examples/lib_let/main.cml`、`examples/var_lib/main.cml` | **已改为诊断** | `ImportedFunctionUsesModuleValue`，定位在 `lib.cml` 里该函数的声明，附注导入方使用它的位置；间接用到（经本模块未导出的函数）同样报告；泛型函数原来的 `GenericFunctionUnsupported` 并入这一条。期望的行为（用声明它的模块里的值）要等合并 root |
| D2 | 导入的值一被使用，编译器段错误（退出码 139） | `camel examples/export_let/main.cml`、`main_ns.cml` | **已改为诊断** | `ConstructNotSupported`，定位在使用处；只导入不使用仍然正常 |
| D3 | 两个模块互相导入，`camel` 与 `camel check` 段错误 | `camel examples/mutual/main.cml`、`camel check examples/mutual/a.cml` | **已改为诊断** | `ImportCycle`，定位在闭合环的那条 `import`，消息列出环路（`import cycle: a -> b -> a`） |
| D4 | `std::cpp` 遇到算出来的顶层值段错误 | `camel examples/defects/cpp_computed_let.cml std::cpp` | **已改为诊断** | `TranslationRefused`，定位在那个顶层值的初始化表达式 |
| D5 | 从两个模块导入同名、同签名的函数，静默取先导入的（打印 `2`） | `camel examples/conflict/main.cml` | **已改为诊断** | `ImportNameConflict`，定位在后一条 `import`，附注前一条。同名不同签名（`main_overload.cml`）仍合并成重载集，见 Q11 |
| D6 | `std::rgir` 里带捕获的函数的端口名错一位：捕获端口被标成第一个参数的名字，参数被标成 `PORT` | `camel examples/now_let/main.cml std::rgir` 中的 `add_k` | **已修** | 捕获端口标捕获的名字（`k`），参数标参数名（`x`） |

D5 的范围：只检查按名字的导入（`import { f } from m`）；`using namespace` 带进来的名字、自动带入的全局算子组与用户导入同名，保持原状。

另外三处是已定但尚未实现的功能，不算缺陷：导入改名（`examples/rename/`）、部分应用 `let g = f<w>`（`examples/misc/partial.cml`）、`docs/language/05_modules.md` 5.2 仍写着“导入时不能改名”。
