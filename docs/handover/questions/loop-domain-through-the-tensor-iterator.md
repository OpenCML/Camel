# 翻转线上 `for` 的 iterable 是 `tensor:iter`：一轮多少次归谁算（实测，2026-10-24，h448 lane）

本文件只报一件**实测**到的事，并提出一处需要作者/协调者裁定的归属问题。本 lane 没有动
`core_facts`，也没有新增语义：改动只在 `modules/tiles` 内（见下「本 lane 做了什么」）。

## 一、实测

`test/cases/modules/tiles/loop_source.cml:10` 写作 `loop y = a for k in [1, 2] do y + a`，只
`import { Tensor, reshape } from tensor`。本树（`handover/h448-flip-tiles`，基线 = h419 翻转线
合入 develop `3e927a7dd`）上：

```
$ camel inspect --gir test/cases/modules/tiles/loop_source.cml
    N01 [label="[1, 2]", type="DATA", value_type="int[]"]
    N02 [label="tensor:iter", type="OPER", value_type="int[]"]
    N02 -> N04 [label="0|0", type="NORM"]        # N04 = LOOP for

$ camel inspect --facts test/cases/modules/tiles/loop_source.cml
FACT {"kind":"core.loopDomain","anchors":[{"graph":"twice","node":6},{"graph":"twice","node":3}],
      "value":"for: t sequential, t < len(node 2); element port 3 reads xs(t)"}
```

即：iterable 位置是 `tensor:iter`（`__iter__`，`613eb92c0` 的决定——数值数组就是张量，按张量
规则沿第一维取行），而 `core.loopDomain` 的 `value` 里**没有 ` = 2`**：`extent` 是空的，因为
`constantLength`（`src/runtime/core_facts.cpp:227`）只认 `GCNodeKind::Data` 的常量数组，
`__iter__` 的 OPER 节点不是。`int[]` 的一轮到位元素类型仍是 `Int64`（`iterInfer` 走
`tensorOf(dtype, {})`，秩 0 = 元素类型本身），所以这不是元素类型的问题，是**一轮多少次**的问题。

消费者只有一个：`modules/tiles/src/compiler.cpp` 的设备编译器（`git grep LoopDomain` 只有它
一处，加上注册）。它原来要求 `domain->extent` 非空，于是 `for k in [1, 2]` 这个**写死的**域
反而变成了"域不静态"，报 `tiles: twice node 6: tiles loop requires a static int[] domain`
（`loop_structure_{nvm,fvm,jit,prl}` 4 条）。

## 二、要裁定：`len(xs)` 归谁算

- **选项 A：`core.loopDomain.extent` 跟进。** 代价：core 要认出**模块**的 `__iter__`（tensor 的
  `iter` 在 `modules/tensor` 注册，`core_facts.cpp` 按分层看不到它），需要一条通用判据
  （例如"这个算子登记了 `__iter__` 出口/迭代语义"），而且 rank ≥2 时常量数组的**行数**
  （`shape[0]`）与它的元素数（`size()`）不同，DATA 那条路径的 `len` 也要分情况。
- **选项 B：各消费者自己看穿（本 lane 现在的做法）。** `tiles` 读 `__iter__` 所读的那段序列
  （秩 1 的 `int[]` 的行就是它的元素，所以序列本身即域）。代价：每个想读 trip count 的读者都
  重复这一遍，事实对别的读者仍是"未知"（今天没有别的读者）。
- **选项 C：让常量折叠把 `__iter__` 消掉**，iterable 回到常量数组，事实自然成立。代价：折叠遍
  的工作与它相对事实层的顺序，超出本 lane。

本 lane 取 B，理由：被裁掉的只是"域必须静态"的**来源**，要求本身没变——域仍必须是读得出来的
常量 `int[]`（`iterable.constant` 里那个 `std::vector<int64_t>`，比 `extent` 更强），
`domain->extent` 有值时仍与它核对（不一致照旧报 `core loop domain disagrees with static array`）。

## 三、取 B 之后的实测（四个后端一致）

```
$ camel --devices cpu test/cases/modules/tiles/loop_source.cml std::nvm|fvm|jit|prl
[3.0, 6.0, 9.0]                                   <- 值对，形式是数组（见下）
const long long constant_3[2]                     <- 三处设备代码断言都在
for(unsigned long long k_iteration=0ULL;k_iteration<2ULL
k_loop_input0=k_next0;
exit 0
```

即 `loop_structure_{nvm,fvm,jit,prl}` 的 4 条里，设备代码那三条断言已满足、值也是对的；剩下的
一条断言是**打印形式**（期望 `tensor([      3.,       6.,       9.], dtype=float32)`）。

## 四、没做的

- 没动 `src/runtime/core_facts.cpp`（分层，且选项 A 里 rank ≥2 的"行数"要一并想清楚）。
- 没动 `src/compile/gir/builder_control.cpp`：数值数组走 `__iter__` 是 `613eb92c0` 的决定。
- 没动用例金标：本 lane 的 `cast_like_aliases_the_same_element_type` 与 `compiled_region_*` 的
  红是**打印形式**（期望 `tensor([...], dtype=float32)`，实际 `[4.0, 16.0, 36.0]`），归 X 桶；
  已有的问题文件见 `docs/handover/questions/unstated-element-type-scalar-and-print.md` 第二节。