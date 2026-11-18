# `outline` 要造一个「`f` 够不到的图」——等「没有程序层」那层定了再看

## 一、要问的那一句

`docs/design/graph-editing/README.cn.md`（`5c1f0f0c8`）第 120 行，算子表里逐字：

> | `outline(nodes)` | 把图里的一段提成一个函数值 | **新增**，`inline` 的反向。交出的是检查过的函数值，不带草稿句柄，可以交出块 |

本车道判定：**这一步做不了**，不是难，是今天的接口里没有它的位置。这一页把「为什么」量出来，并把默认写死，免得下一班再撞一次。

## 二、实测：`outline` 要的五件事，`modules/graph` 今天一件都表达不了

按执行侧的顺序：

1. **造一个图，并且让它进到这次提交里。** `outline` 的结果是一个**函数值**，也就是一个新图。C++ 侧有这个能力：`GraphDraftGroup`（`include/camel/runtime/draft_session.h:354-369`）「Encodes new graphs that may refer to one another… The members form a program with the graphs of the one the group is made over」，`reserve()` 先占一个 `GraphId`、`encode()` 再建图。但两点都不成立：**(a)** `modules/graph` 没有导出它，Camel 侧的 pass 摸不到；**(b)** 它建出来的图属于一个**由 C++ 持有的程序**（`ProgramHold`，注释说「That program is held here, in C++ … a group is made inside a running call」），而一次 `commit()` 发布的是这场会话自己的程序（`collectCommitClosure()` 收可达闭包、`fresh[graph]` 逐图编码）—— 组合出来的图不会被交付。
2. **跨图搬节点。** 一段节点要从原图移到新图，连同它们之间的边、它们引用的常量、它们所在的区域（若这一段落在一个臂或循环体里，提出去还要把这层区域一起提）。`GraphDraft` 的写操作（`setNormInputs` / `setOrderInputs` / `setSyncInputs` / `erase` …）都只作用于**本图**的节点号。
3. **算参数与结果。** 「一段」的边界由被切断的边决定：进来的边是参数，出去的边是结果。这条逻辑今天在树里只有一处参考 —— `modules/tiles/src/region_outline.cpp`（`outlineRegions`），而它是 tiles 专用（按 tiles 的区域模型、按 tiles 的调用约定）。
4. **编码并提交成一个 `::Function` 值。** 新图要能出现在提交后的程序里，并被 `graphs_of` / `callee` 看见。
5. **交出块级的函数值。** 设计第 120 行要求「不带草稿句柄，可以交出块」—— 这是 `edit` 块的类型规则，属于外壳，不是本步。

第 1 条单独就够判了：能力在 C++ 里有，但它建出来的图不进这次提交，`outline` 交不出一个「程序里看得见的函数值」，形状就不成立。

## 三、与 `05c5b1284` 的碰撞（这才是要你定的一句）

`05c5b1284`「没有程序层」第 5 条改后逐字：

> **没有"程序"这一层**（负责人 2026-11-18："程序就是普通的函数，或者说就是 main"，与"遍是函数"同一个出发点）：`edit f as d` 的草稿就是 `f` 加上从 `f` 可达的全部图（`graphs(d)` 列出它们），块尾一次提交，全成或全不成。

这一句把一次编辑的**全部对象**定义成「`f` 加上 `f` 可达的图」。而 `outline` 交出的图**不在**这个集合里 —— 它在编辑之前不存在，编辑之后 `f` 才够得到它。也就是说：

- `inline` / `replace` / `erase` / `order` / `eval` 都只在「已有的图」上加删改，`d` 的对象集合在编辑前后不变，第 5 条对它们够用；
- `outline`（以及任何「造新函数」的算子）会**扩大**这个集合，而第 5 条没有说 `graphs(d)` 能不能长大。

所以这不是「等一个更全的接口」，是**那条草案的一句话要不要扩成「`d` 也可以新造图」**。若扩，`GraphDraftGroup` 那层要作为 `modules/graph` 的公开能力被导出（等于给 Camel 侧一个「造图」的算子）；若不扩，`outline` 就永远只能用别的方式表达（例如设计第 124 行「提出新函数：`outline` 一段，在原处 `inline` 一次对它的调用」这条组合要整体撤回，或改成「提出去」只允许提到**已存在的**函数上）。

## 四、默认（写死）

**默认：`outline` 这一步不做，`edit` 块只带 `inline` / `order` / `replace` / `erase` / `eval`；等 `05c5b1284` 那一层落地，并且那一层明确说「一次编辑可以新造一个函数/图」之后再开 `outline`。**

理由：

1. 设计第 120 行标的就是**新增**，且没有写它造出来的函数值属于哪个集合；照今天的接口做，只能造出一个**不属于任何程序的图**，那是把投影造坏，不是缺功能。
2. `outline` 需要的第 1 件（新建图并接进程序）如果要作为公开算子给 Camel 侧，会与设计原则 9.5（图接口看不见拼写）和「新节点只能由已类型检查的 Camel 代码产生」（第 119 行 `inline` 的注）一起被重新讨论一轮 —— 这一轮属于外壳，不属于本步。
3. 反过来做（先在 `modules/graph` 里私开一条造图通道）会把一条**未定的接口**钉进 `stdlib/graph.cml`，而 `stdlib/graph.cml` 是下游全部 pass 的公共面。

**本车道与本页无关的产出**：`order` 与 `exit_of` 已落（`19ac060cd`），三个 case 断言的是图的形状；`inline` 的定序问题见同日的 `2026-11-18-the-at-of-inline-says-a-region-not-a-place.cn.md`。
## 决策者答复（2026-11-18）

**草稿可以新造图；`outline` 照做，排在 `inline` 之后，不搁置。** "没有程序层"那条说的是"程序就是 `main` 这个函数"，没有说草稿的对象集合不能长大。按"图是值"：`outline` 交出一个新的函数值，它就是一个新图；交出的 `f'` 若调用它，它就是 `f'` 可达的图，随这次提交一起发布；若只作为块的值交出去，它作为一个独立的函数值发布。设计稿第 6 节第 5 条按此补一句："草稿是 `f` 加上它可达的全部图，以及这次编辑新造的图"。
- 这不违反 9.5：新图里的节点都来自原图（原本就是已类型检查的 Camel 代码产生的），`outline` 只是把它们收成一个函数，不按名字造节点。
- 执行侧用已有的 `GraphDraftGroup`，作为图编辑接口内部的能力，不把"造图通道"单独暴露进 `stdlib/graph.cml`；对外只有 `outline` 这一个算子。
- `tiles` 的 `region_outline.cpp` 有参数与结果的计算可参考，按"一件事一套实现"收成一处，`tiles` 改用它。
