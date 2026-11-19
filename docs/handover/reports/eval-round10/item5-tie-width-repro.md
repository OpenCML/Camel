# 一个 `inspect --ties` 看得见的复现：等键段宽 7，走 `stable_sort` 那条支路

**程序**：`test/cases/trans/tie_width_while.cml`（1571 字节）。
**构建**：`2d7c5c3a1` Release（build-info revision `2d7c5c3a14`，源指纹 `0244fca0cbac`，宿主 `88zjwei`，构建前 load `25.06`）。
**没改任何 `src/`、没改任何语义。**

## `[TIES]` 原行

```
canonical ties: 1 group(s) wider than one node, widest 7, over the limit 1 (kMaxTieWidth 6)
  an equal-key part wider than the limit is cut by each node's own fragment and by the numbers of the nodes that read it, and what stays equal is a known limitation: it is not enumerated, so the numbers of those nodes are their settled structural order and not a compared minimum
tie width 7 (equal keys 7, not enumerated) in graph main
```

三个数：**group(s) 1 · widest 7 · over the limit 1**。第二行那句正是这一支的自述。

同一份文档的读数：`camel … std::jgir canonical=true` → 18709 字节，
md5 `68f349c2ae25907323234c95d037a1fb`，sha256 `dbe0a797ae659000e949c16409284cd5b15313e116a6aa998dd18911896a44ff`；
连跑两次逐字节相同；程序输出 `(4, 4, 4, 4, 4, 4, 4, 4)`。

## 为什么它触发：入口条件原文

1. **组的来源**：`canonical.cpp:128-137`，每个 region 收"没有编号的、属于本 region 的节点"成 `leftovers` 再 `arrange`；
   紧挨着的注释（`:125-127`）点名的就是这个形状：
   > `… that nothing in its region reads -- loop a = 0, b = 0 while b < n do (a + 1, b + 1) leaves the condition port of a with no reader.`
2. **等键段**：`arrangeEqualKeyRun`（`canonical.cpp:496`）对键相等的一段做决定：
   > `if (enumerating_ || run.size() > kMaxTieWidth) {`（`:503`）
   而 `kMaxTieWidth` 是 `6`（`include/camel/runtime/canonical.h:97`：`inline constexpr size_t kMaxTieWidth = 6;`）。
3. **越限后走的那一支**：
   > `std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return fragments[a] < fragments[b]; });`（`:513-515`）
   `stable_sort` 在片段相等时**保留进来的次序**，而 `run` 的次序就是 `refs_`（创建次序，即书写次序）——这就是那个 latent 并列。

程序里：`while` 循环带 8 个状态，条件只读 `x1`，其余 7 个状态的 condition 端口**没有任何读者**，
于是 7 个同 kind、同 role、同类型的端口落进同一个 `leftovers` 组，键全相等 ⇒ **等键段 7 > 6** ⇒ 不枚举、走 `fragmentKey` 的 `stable_sort`。
`over the limit 1` 与 `not enumerated` 是同一条支路的两个说法。

## 试过但没触发的形状（供参考）

| 形状 | 读数 |
| --- | --- |
| 8 个没人用的 `let c<i> = 1`（int / float / string / array 各试一次） | `0 group(s) … widest 0` —— 纯常量语句被 builder 收掉，进不了组 |
| `loop … for i in 0..4`（range 循环，8 个状态） | `0 group(s) … widest 0` —— range 循环没有 condition 区，没有无读者的端口 |
| 闭包捕获 8 个值 | `1 group(s) … widest 8`，但 `equal keys 1`（每个捕获的 with 输入不同）⇒ 只到"宽"，不到"等键宽"，不碰这条支路 |
| **`while` 循环 8 个状态、条件只读第一个** | **`widest 7`、`equal keys 7`、`over the limit 1`** ⇒ 命中 |

## 还没做的

**修前/修后两份读数**里的"修后"要改 `canonical.cpp`，按本项禁令（不修、只造复现）留给截稿后；
修法的效果预期是：把 `stable_sort` 换成对 `fragmentKey` 相等者再按只依赖结构的键排，
届时这个程序的 `[TIES]` 仍应是 `1 group(s) … widest 7`，但 `over the limit` 会变 0（或那条支路被替换掉）。
**这一句是预期，不是读数**——没有"修后"的测量。