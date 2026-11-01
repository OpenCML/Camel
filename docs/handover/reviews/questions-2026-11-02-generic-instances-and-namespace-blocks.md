# 两张没有源码写法的图：泛型实例图要不要内联打印，`namespace` 块要不要打印

> **问什么**：`std::cml` 拒绝的 358 个图里，**293 个文件 / 291+2 个名字**都是同一件事——
> **那两张图在源码里没有写法**。它们分成两族，各自要的不是"把名字拼对"，而是一个**新功能**：
> **(i) 泛型实例图**（`array::vmap<float[3], float>`、`rulesets.jvp::lift<float>`，219 个文件）
> 要不要**内联成 lambda 打印**；**(ii) `namespace`/模块限定的图**（`Tools::inc`、`rules::array_log_softmax_of_softmax__lhs`，
> 74 个文件）要不要**打印 `namespace` 块**。
> **堵着谁**：`std::cml` 的打印数（`printed 549`）与这一族的 293 个文件；今天它们全部具名拒绝，理由都对。
> **为什么我不自己定**：第一条是**语言有没有"实例的值"这种写法**的问题（不是打印机的手艺），
> 第二条是**语言构件**（`namespace` 块）的问题；两条都超出"补名字"这一层，按纪律要作者裁。

## 分类计数（实测 2026-11-02，`lane-m5`，树 = `ea9bf0915` + `374488bf4`）

命令：对 `test/cases/**/*.cml` 逐个跑 `std::cml`，取**首次**拒绝里那句
`the graph name '<name>' is not a source identifier` 的名字，按形状分类。

| 形状 | 判定 | 文件数 |
| --- | --- | --- |
| 含 `<…>`（泛型实例） | `GENERIC` | **219** |
| 含 `::`（模块/命名空间限定） | `MANGLED` | **74** |
| 其它（不含 `<`、不含 `::`） | `PLAIN` | **0** |
| 合计 | | **293** |

**尾随分隔符的名字：0 个**（`name` 以 `::`、`.` 或 `<` 结尾的计数为 0）。此前报的
"`rulesets.`（以句点结尾）"是**我那条统计脚本的截断错误**（`sed` 在第一个句点处截了），
真名是 `rulesets.jvp::lift<float>` 与 `rulesets.probe_missing::add_ok__lhs`，都是 `MANGLED`/`GENERIC`。
**所以"前端拼名字漏了成员名"这条缺陷不成立**，没有可修的点。

三个代表：

| 文件 | 拒绝里的名字 | 源里写的是什么 |
| --- | --- | --- |
| `compile/transforms/vmap_whole_reduction.cml` | `array::vmap<float[3], float>` | `vmap(f)`（实例由调用点定） |
| `compile/rulesets/rulesets_lookup.cml` | `rulesets.jvp::lift<float>` | `import { lift } from rulesets.jvp` |
| `compile/namespace/namespace_lib.cml` | `Tools::inc` | `namespace Tools { export func inc … }` |

## 路 (i)：泛型实例图内联打印（219 个文件）

**要做什么**：`array::vmap<float[3], float>` 这张图在源码里没有名字，源程序写的是 `vmap(f)`。
要把它写回来，只能把**实例图本身**当成一个函数值打印——即打印成 lambda（`(xs) => …`）或另起一个
`func cmlN`，并把调用点改写成那个值。

**代价**：
- **递归**：实例图的调用可能回到自己，内联会无界展开；需要有"遇到环就退回具名"的判据。
- **共享**：同一个实例被多处使用，内联一份还是每处一份，会改变图（节点数与共享关系），
  而往返判等是**按规范化文档同构**比，共享一变就不等价。
- 实例图里若又有实例图，内联深度没有天然上界。

## 路 (ii)：`namespace`/模块限定的图打印（74 个文件）

**要做什么**：`Tools::inc` 要写成 `namespace Tools { export func inc(…) … }`，`rules::…__lhs`
要写成 `module rules { … }`。这要求打印器会打印**模块/命名空间区域**，并把限定名还原成块结构。

**代价**：`namespace` 是**语言构件**（不是打印技术）：它牵涉 `export`、可见性、模块边界，
以及"哪些图必须留在模块里、哪些可以摊平"的判断。打印器今天连区域种类都只支持 `sync`，
这一族等于要它先支持一种新区域。

## 默认（写死，除非作者另有裁定）

**两条都不做。** 它们不是"名字拼错"，是**语言没有写法 / 打印机不支持区域**：
- `GENERIC` 219：**语言没有"实例的值"这种写法**，实例图只在编译期存在；
- `MANGLED` 74：**`namespace`/`module` 区域打印机不支持**，支持它是一项新功能。

这两族因此**留在拒绝栏**，拒绝理由保持现状（`the graph name '<name>' is not a source identifier`），
下一轮的活不在这一层找。