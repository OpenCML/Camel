# 求导写模块 `var` 的同步函数，经 `std::opt` 后段错误：复现与根因

2026-10-30。作者原报：把写了模块级 `var` 的 `sync` 函数拿去微分，`std::opt` 之后段错误，三个调度器都在打印 `f body runs` 之后死（`(9.0, 6.0)` 本身是对的）。原程序在 `Camel-Archive/ICPC2027/.../c11d_grad_effect_inside.cml`，该仓库私有（raw 与 API 均 404），机器上也没有副本，只能按描述重建。

## 一、按描述重建不崩，"四要素"不是充分条件

重建（`f` 带 `with <w>`、写模块级 `calls`、`with_grad(f)<3.0>(1.0)`、打印 `.0`/`.1`）在 `std::opt` 前后都正确输出 `f body runs / 9.0 / 6.0 / 1`；develop tip（`c2d6abbac`）与作者复现的 `8fb0e0b1e` 上都不崩。矩阵扫过的格子（全部 exit 0）：

| 去掉/改动 | 结果 |
| --- | --- |
| 去掉 `sync`（写不再执行，`IgnoredSideEffect`） | 正常，`calls` 读 0 |
| 去掉模块 `var` | 正常 |
| 去掉微分（只调 `f<3.0>(1.0)` 并读 `calls`） | **崩**（见下） |
| 去掉 `std::opt` | 正常 |
| `int`/`float`/`f32[]`/结构体 `var`、写两次、两个 `var`、`+=`、写值依赖 `with` 参数、读后再写、`if` 里写、闭包、路径写 `arr[0] = ...` | 正常 |
| `let step = with_grad(f)` / `let gf = with_grad(f)<3.0>` / `grad` 之后再 `with_grad` / 元组打印 / 解构 | 正常 |
| 四个调度器（nvm/fvm/jit/prl）、`std::macro` 前后加 `std::opt` | 正常 |

所以缺的那一格是：**普通（非微分）调用 `f<3.0>(x)`，并在调用之后读那个 `var`**。微分不是必需的。

## 二、最小复现（exit 139，四个调度器都崩）

`test/cases/linear/runtime/partial_application_written_var.cml`：

```
var calls: int = 0

with <w: float>
func f(x: float): float sync {
    println('f body runs')
    calls = calls + 1
    return w * w * x
}

func main(): int sync {
    println(f<3.0>(1.0))
    println(calls)
    return 0
}
```

`camel p.cml` → `f body runs / 9.0 / 1`；`camel p.cml std::opt std::nvm` → 打印 `f body runs` 后 SIGSEGV（exit 139），fvm/jit 同；Debug 版是断言：`graph_runtime_support.cpp:917`，`A call that writes captured variables must pass each of them as a With input.`

## 三、第一个把图弄坏的遍

`std::opt` 的十一个子遍逐个试：只有 `std::opt::specialize` 崩（其余 inline/small/arm/hybrid/devirtualize/fold/algebra/extract/saturate/dce/order 都正常）；`std::opt::specialize` 之后再接 nvm/fvm/jit 都崩。

引入提交是 `dfbc28b6d`（2026-10-26，"Check a call against the value type its written captures give it"）。它的父 `1fba0e063` 上同一个程序**不崩**，而是在封存期报出来：

```
camel: internal error: Runtime graph 'main' breaks its edge invariants (1 violation(s)):
  graph 'main' node 2 (10): calls 'f', which returns a float, and is a (float, var int)
```

坏图 10-26 就已经在了，`1fba0e063` 的封存检查还看得见它；`dfbc28b6d` 把"这个调用该是什么类型"改成用 `callValueTypeOf` 现算，而建图侧也是同一个函数算的，两边永远一致，检查从此对这类坏图失效，崩溃才出现。作者那条报告 10-29 出现，正在此之后。

## 四、坏在哪

`specializeDirectFuncInDraft`（`src/runtime/draft_inline.cpp:1010`）把 `f<3.0>` 的声明 `with` 参数 `w` 绑成常量，`eraseFormalFromDraft` 调 `GraphDraft::removeWithPort` 把该 `with` 端口删掉——但**没有同时删掉图函数类型里的那条 `with` 参数类型**。于是 `f$spec0` 的 `withPorts` 只剩捕获单元（1 个），而 `funcType()->withTypesCount()` 仍是 1。

约定是"声明的 `with` 参数在前，捕获紧随其后"（`callValueTypeOf`、`runtimeFuncValue` 都这么读）。类型多算一个，捕获就整体后移一格：

- `callValueTypeOf`（`graph.cpp:1945`）算 `port = declaredWith + k = 1`，越过了 `withPorts`（size 1），越界检查把它跳过 → 调用节点的值类型退化成裸 `float`，**新版本从值里消失**；
- 封存期的 `checkCall`（`graph_edges.cpp:679`）用同一个坏函数算"期望类型"，算出来也是 `float`，与节点一致，于是**坏图过了封存**；
- 运行时 `runtimeFuncValue` 同样算 `at = 1`，`withValues` 只有 1 个（就是那个 cell）→ 越界读；release 版没有断言，读到的垃圾槽位当 int 用，随后 SIGSEGV。

`std::gir` 里一眼可见：`N1B FUNC f$spec0 value_type="float"`，而它的用户仍按元组取 `.0`（float）和 `.1`（var int）。

## 五、修法

`GraphDraft::removeWithPort`（`src/runtime/draft_construct.cpp`）删端口时，若该端口在"声明区"（下标 < `withTypesCount()`），同时从函数类型里删掉对应那条参数类型，重建 `FunctionType`。这样类型与端口重新对齐：`declaredWith = 0`，捕获回到端口 0，调用的值类型是 `(float, var int)`，版本又能被读到。

修复后最小复现 exit 0、输出仍为 `f body runs / 9.0 / 1`（默认、`std::opt` 与四个调度器、`std::opt::specialize` 全部一致）。

## 六、留给作者的一句

原 c11d 程序里 `f` 是否在**微分之外**还被直接以 `f<3.0>(x)` 调用并读那个 `var`？若是，槽位错一格就是它；若否，请给那条调用形状（或那 10 行源码），因为按作者标题（"对 sync 函数做微分"）重建的三种写法治愈前都不崩。