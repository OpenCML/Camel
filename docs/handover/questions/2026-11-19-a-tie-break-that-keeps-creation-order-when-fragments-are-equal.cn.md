# 一个未被触发的并列：`arrangeEqualKeyRun` 的 `stable_sort` 在片段相等时保留创建次序

**问题给作者。** 规范编号里还有一处会让书写次序漏进编号的地方，而这一棵树里**没有任何程序触到它**
（`camel inspect --ties` 对案例程序、`writings/swapped`、`case-study-variants/train-swap` 都报
`0 group(s) wider than one node, widest 0, over the limit 0 (kMaxTieWidth 6)`）。

## 在哪

- `src/runtime/canonical.cpp:496-541` `arrangeEqualKeyRun`：键相等的一段，宽不超过 `kMaxTieWidth = 6`
  时枚举排列、取 `tableOf` 最小者（`canonical.cpp:545`，上限 `kMaxTieCandidates = 4096`）。
- 宽超过 `kMaxTieWidth`，或已在枚举里（`enumerating_`，嵌套并列）时走另一支：
  `canonical.cpp:504-521`，按 `fragmentKey`（`canonical.cpp:569`）**`std::stable_sort`**（`:513`）。
- `stable_sort` 相等时保留**进来的次序**；`run` 的次序来自 `sorted`，`sorted` 来自 `tied`，
  而 `tied` 是 `arrange` 的 `pending`——即 `refs_`（节点创建次序）与语句次序，也就是**书写次序**。

## 触发条件

1. 某个并列段的**等键宽度 > 6**（同一键、同一段里超过 6 个节点）；或
2. 正处于外层并列的枚举里（`enumerating_`），内层并列一律走这一支（注释写明：否则两个宽度的阶乘相乘）。

## 影响面

改这一处会改变**触发它的每一个程序的规范文档字节**：`same_graph`、`cjk` 分块、以及任何按规范
文档做比较的读法都跟着变。今天两份报告（第 1、2、3b 项）的字节数全部建立在此行为之上。

## 建议（默认）

**定版后不动，作者答了再改。** 理由：
1. 未触发——不为没发生的病动刀；
2. 改动会让今晚的读数作废，而截稿只剩一天多；
3. 它实质上会改等价关系的判定边界（哪些写法算同一份图），这类改动作者今晚已两次否过。

若作者要修，最小改法是：把 `stable_sort` 换成对 `fragmentKey` 相等者再按一个**只依赖结构**的键排序
（例如 `nodeKey` + 各自读数的编号序列），不触碰 `tableOf` 与 `kMaxTieWidth`。需要先给出一个
**能用 `inspect --ties` 看到**的复现程序，否则改完无法证明修的是真问题。

## 复现

```
camel inspect --ties <程序>      # 打印 ties 组数、最宽组、超限组
```
本树内没有触发它的例子，所以本页没有写出「修前/修后」的读数——**没有就是没有，不拿估计代替**。