# 归属：`grad` 在运行期建的求导图归 `transforms`

## 裁定

`grad(f)` 在运行期由算子自己造出来的那张求导图，归 **transforms** 那条线（建图期就把 `reinterp` /
`split` / `transpose` 三张图建出来）。去函数化只管**程序里写出来的函数值**；不给算子造出的值留一个
运行期标签变体，也不让去函数化去认它。

## 谁负责

- **transforms 线**：`grad` 的建图时机与那三张图。现状的入口是
  `modules/autodiff/operators.cpp#gradFunction`，它在运行期调 `#buildGradientGraph` 造图，再
  `::Function::create` 出一个函数值。
- **去函数化线**：`src/passes/opt/defun/`，只处理程序里写出来的函数值；这一条不碰 `modules/autodiff`。

## 为什么归那条线

- 设计原则 9.9：自动微分是**建在遍机制上的库**，不是核心算子。`grad` 现在自己造图、自己持闭包，
  正是 9.9 要消掉的那件事；消掉它属于 transforms 的改写，不属于去函数化的表示。
- 去函数化没有可改写的输入：程序里写出来的函数值在编译期就在图上，能按类型分族、按调用点改写成
  直接调用；`grad` 造出的值在运行期才有，`gradFunction` 还把 `f` 的闭包拷进运行期 `::Function`
  的 `tuple()`，那个闭包不在任何图上，`std::stats` / `std::gir` 数不到它。
- 验收口径因此分开：去函数化的 0/0 在「去函数化之后、`std::opt` 特化之前」读；`grad` 那张图的
  0/0 等 transforms 落地后，用同一个小程序读整体，作为联合验收写成一个用例。

## 现状：`grad` 的间接调用计数

小程序（`grad(f)<2.0>(4)`，`f` 带一个 `if` 和一个 `loop`；见裁定页的源码），本树读数：

| 流水线 | closures | indirect_calls | specializations |
|---|---|---|---|
| 原样 | 0 | 2 | 0 |
| `std::macro` | 4 | 7 | 0 |
| `std::opt` | 0 | 2 | 2 |
| `std::macro std::opt` | 8 | 8 | 0 |

- 剩下那 2 次间接调用的被调者是 `OPER autodiff:grad` 造出的函数值：既不是 `FUNC` 边，也不是静态的
  函数值。
- `std::opt` 的 0 闭包是**两个特化副本换来的**（`specializations=2`）：同一份程序 `std::macro`
  单跑读 4/7，`std::macro std::opt` 读 8/8，0/2 只在带那两个副本时出现。所以 0/0 不是去函数化
  成功的读数。

## 没做

- 代码未动：`modules/autodiff` 与 `src/passes/opt/defun/` 都没有为这一条改动。
- 联合用例（同一小程序在去函数化之后、特化之前读到 0/0）留给 transforms 线。