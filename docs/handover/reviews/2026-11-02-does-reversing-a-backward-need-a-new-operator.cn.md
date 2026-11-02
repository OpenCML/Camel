# 让反向半程按轮倒序，需要新增一个数组反转算子 `:struct/reverse_arr` 吗？

**状态**：待裁；我方默认见第 3 节（不新增算子，也暂不落改体的那条）。

## 1. 三处读数（原文）

- **`:struct/range` 收 `step`**：`range(start, stop[, step])`，只有 `step = 0` 是非法（`include/camel/standalone/array.h#range`；执行器表 `src/builtin/executors/builtin.cpp` 的 `struct/range`）。`range(n-1, -1, -1)` 一步就是倒序整数序列。
- **`LoopKind::For` 吃的是区间值，不是 start/end**：`include/camel/runtime/loop.h` 写着 For/ForWhile 的 Norm input 0 就是被迭代的数组，body 的第一个口是该轮的 element。**所以循环里没有"轮次"这个下标**——第 t 轮看到哪一份残差由迭代顺序决定，不由任何 `Accs` 决定。
- **只用已有算子拼不出"数组反转"**：`:struct/slice_arr` 不在算子注册表里（`src/builtin/operators/struct.cpp` 的 `:struct/*` 只有 `concat_arr`、`copy_arr`、`len_arr`、`append_arr*`、`zip`、`range`），`len_arr` 只回答 n，没有"按下标倒序写数组"的算子；硬拼出来就得再造一个循环（为另一个循环的区间造循环）。

`split` 侧也确认过：反向体里的读点是 `modules/transforms/split.cpp#cutLoopGraph` 造的 `Accs`（`addAccsNode(type, value, 0)`），`value` 是**胶带元组的第几个成员**，不是轮次；每轮读 N 处，N = 胶带成员数，**没有一处带轮次下标**。

## 2. 三条路

| 路 | 做法 | 代价 |
| --- | --- | --- |
| **A** | 新增 `:struct/reverse_arr`（执行器 + 语义 + 规则），`transpose` 把区间换成 `reverse(range)` | **语言/算子层**改动，要作者裁；`transpose` 只动一处 |
| **B** | 区间换 `range(n-1, -1, -1)`，体内改成按索引取 | 只动 `transpose`/`split`，但**改体**：每个读胶带的点从"区域端口"改成"下标读" |
| **C** | 把轮次下标从 `i` 换成 `n-1-i` | **不成立**：图里没有轮次下标（第 1 节第二条） |
| **D** | 不动 | 反向半程继续按正序走 |

## 3. 我方默认

**不新增算子**（A 是语言层的改动），**也暂不落 B**：B 改体，而在标量状态下「倒序」与「正序」打出同一个数——没有人能验证。等 P1-9 的四处落完、第 3 节的判别用例能分辨 Y1 与 Y2 之后，再按那时的读数在 A 与 B 之间选，比现在猜要值。四条路里没有一条是"换一个下标就好"。

## 要作者答的一句

**「反向半程按轮倒序需要『一个倒序的数组值』，而 `LoopKind::For` 只吃数组、循环里没有轮次下标：要么新增 `:struct/reverse_arr`（语言层），要么改体用整数索引倒着读。我默认不新增算子，等 P1-9 落完、判别用例能分辨之后再选；除非你裁要新增。」**