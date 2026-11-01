# `tensor:` 与 `array:` 的同名算子，是不是同一个调用形状？

作者 19:50 的裁定说"键从声明读"，并且"同一个键可以有多条规则，按类型选"。把这条用到张量上就会出现这个问题：`sum(xs)` 只有一个键，而树里有两条同名的声明候选——`stdlib/array.cml:155` 的 `sum::<T>(xs: T[..]): T`（数组归约，带函数体）和 `tensor:sum` 那条只有计算签名、没有声明的注册。

## 事实

1. **数**：`tensor` 协议注册 **86** 个 URI，其中 **18** 个已经有声明造出的重载（`norm >= 0`，孪生行），**68** 个只有 C++ 的计算签名（`norm = -2`）。按调用形状（名字 + 元数）扫全树 585 行注册重载：**已覆盖 413 / 真缺 206 / 签名表达不了 36**（`docs/handover/reports/2026-11-02-p1-13-three-columns-call-shape.cn.md`）。
2. **`sum_axis` 早就有声明**：`stdlib/tensor.cml:162` 那条 `export func sum_axis::<T>(xs: T[..], axis: int, keepdims: bool): T[..]` 就是它，给它补 `#op('tensor:sum_axis')` 之后 `camel inspect --ops` 的两行**逐字未变**——tag 只在声明不遵循"模块名 + 名字"约定时才需要（设计原则 6.4 第 1 步）。
3. **`reshape` 的注册签名是联合**：`(t: float[..] | number[], shape: int[]) => float[..]`，声明今天表达不了联合参数，所以它属于"签名表达不了"，不是"缺声明"。
4. **真跑过的证据**（`import { ones } from tensor` + `import { sum, mean, min, max, argmax, argmin } from array`，`ones::<f32>([2, 3])` 实参，`std::nvm`）：`sum` 6.0、`mean` 1.0、`min` 1.0、`max` 1.0、`argmax` 0、`argmin` 0——**`array:` 的声明接受张量实参**。
5. **反例也在同一批里**：`abs`、`exp`、`log`、`erf`、`erfc`、`pow`（`stdlib/math.cml`）**拒绝**张量实参，报的是 `WhenConditionUnmet`——"every candidate's `when` is false"，因为它们是 `when is_float::<T>()` 这类**元素类型**条件；`dim` 的轴是 `<1>` 里的**静态**参数，不在括号里，所以它的"同名同元数"是假匹配。

## 问题

`tensor:sum`（以及 `mean`、`min`、`max`）**需不需要自己的声明**？

- **A. 不需要**：`sum(xs)` 是同一个调用形状，`array:` 的声明按形状覆盖了它；`tensor:` 这一层缺的是**类型条件**（把 `when is_numeric::<T>()` 用到张量元素类型上，或让 `T[..]` 参与），不是第二条声明。
- **B. 需要**：`tensor:` 应当有自己的声明，哪怕与 `array:` 同名同形——两条声明各自服务一层。
- **C. 都不是**：`tensor:` 这一层从 P1-13 的待补清单里划掉，并说明由谁承担。

## 默认（作者未另裁时按此开工）

**A，并按 19:50 的"同键多规则按类型选"**：`sum`/`mean`/`min`/`max` 记成**已覆盖**（事实 4 是证据），`tensor:` 这一层要补的是**类型条件**而不是第二条同名声明；`reshape` 记成**签名表达不了**（联合参数）；`abs`/`exp`/`log`/`erf`/`erfc`/`pow`/`dim` 记成**声明在、但类型条件挡着**（类型问题，不是声明问题，也不许加第二条同名声明）。

按 A 之后，`tensor:` 真正"真缺"的是那 **17** 个：`zeros`、`ones`、`full`、`eye`、`from_bytes_*`（7 个）、`load_npy_*`（5 个）——它们没有任何同名声明，签名也写得出来。

`等点名`。