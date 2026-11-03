# 两集合确实不同，但今天不同不改变任何判断

**日期**：2026-11-03 · 分支 `lane-tloop` · 起点 `0fc4974b6`
**缘起**：P0-2 第二半要把"仍按控制边读链位的 pass"改成读 sync 边。清单一轮做出来是 9 处，
但其中第一处（`algebra.cpp#constant`）加完那行**读数一点没变**，撤掉之后要回答一个更基本的问题：

> `syncInputsOf(x)` 与 `ctrlInputsOf(x)` 在同一个节点上，是不是同一个集合？

如果是，那 5–9 处"换个读法"全部**行为等价**，价值只在"ctrl 边退役那天"；如果不是，那哪些地方真有差别。
本页是答案，也是这一夜唯一没有改任何语义的产出。

## 1. 方法

在 `src/passes/trans/stats/stats.cpp` 的节点循环里临时塞一句：

```cpp
std::fprintf(stderr, "EDGES node=%d kind=%d op=%s ctrl=… sync=…\n", …);
```

`std::stats` 在冻结后的图上走，`ctrlInputsOf` 与 `syncPredsOf` 都从图本身读，所以打印的是**同一时刻、
同一张图**上的两个集合。**测完 `git checkout`，仪器没有进任何提交。**

覆盖 7 个程序（都在 `test/cases/compile/transforms/` 下临时放过，同样已删）：

| 程序 | 形状 |
|---|---|
| `tmp_e1_scalar` | 纯标量表达式与打印 |
| `tmp_e2_branch` | `if … then … else`（唯一的差异来源） |
| `tmp_e3_loop` | `loop … while … do` |
| `tmp_e4_call` | 普通函数调用 |
| `tmp_e5_sync` | `sync` 函数里若干 `var` 写与读 |
| `tmp_e6_array` | 数组字面量、下标读、`f32[]` |
| `tmp_e7_block` | `loop … do sync { println(v); acc + v }` |

## 2. 读数

**78 个节点实例，20 个带边；17 个两集合逐节点相同，3 个不同。** 三处差异全部在 `tmp_e2_branch`：

```
node=3 kind=7  op=          ctrl=      sync=2,     ← Join
node=6 kind=11 op=:op/mul_l ctrl=4,    sync=      ← 分支臂
node=8 kind=11 op=:op/add_l ctrl=4,    sync=      ← 分支臂
```

`kind=7` 是 **Join**（`GCNodeKind`：Data 0、Port 1、Cast 2、Copy 3、Fill 4、Accs 5、Brch 6、**Join 7**、
Call 8、Bind 9、Func 10、Oper 11）——`if … then … else` 那个值的汇合点：它有 sync 前驱（`2` 是条件
`:op/gt_l`），**没有** ctrl 边。两个算术节点挂在 `Brch`（`4`）的控制边上，是**分支臂**：控制边在那里的
含义是"只有这个臂被选中时才跑"，不是"链的上一条语句"。

所以：**不是恒等**。三选一里出现的是 **③（有节点只有 sync）**，同时也出现"只有 ctrl"的节点。

## 3. §5.9 那一行为什么没有靶子

`src/passes/opt/generic/algebra.cpp#constant` 的第一个条件就是

```cpp
if (!h || h->kind != GCNodeKind::Data || …)
```

而实测唯一"只有 sync"的节点是 **Join**，它永远进不了 `constant()`。要让那一行（`!syncInputsOf(id).empty()`）
生效，需要一个**只有 sync 边的 `Data` 节点**——7 个程序里没有。**撤掉那行是对的**（`0fc4974b6`）。

## 4. 清单从 9 缩到 5 的过程

| 轮次 | 数字 | 依据 |
|---|---|---|
| 清单一轮（§5.8） | 13 行只读 ctrl，其中 9 行"该补 sync" | `git grep` + "同文件邻行是否有 sync 字样"的启发式 |
| 加完第一处（§5.9 / `0fc4974b6`） | **9 → 8** | 探针两次读数相同；`writeSyncStatement` 成对写边，所以该处不可达 |
| 逐处读（§5.11） | **8 → 5** | 三条假阳性如下 |

三处的逐行依据：

- **`rewrite.cpp:512` 不是候选**：同一个表达式里两侧都读了——
  `!draft.syncInputsOf(user).empty() || !draft.ctrlInputsOf(user).empty()`。启发式只看 ±1 行，漏了它。
- **`rewrite.h:115` 是写侧**：`inheritWait` 里 `draft.setSyncInputs(user, only)` 之后补
  `appendInput(DraftEdgeKind::Ctrl, …)`——那是**成对写边**的那一半，不是读判断。
- **`screen.cpp:274` 问的不是"谁等谁"**：它数的是"多余的 ctrl 输入"（`ctrl >= 2`），问的是**正在退役
  的那种边自身**的冗余，与 sync 无关。

剩下 **5 处**：`gates.cpp:62/90/94` 三处，`modules/tiles` 的 `compiler.cpp:536`、`region_extract.cpp:108`、
`graph_regions.cpp:513`（后三条是同一件事）。它们**按形状推断会**遇到只有 sync 的节点（Join 可以是 gate
的后继、区域闭包会收任何节点），**但还没有逐处逐行读**——所以是"待读"，不是"该改"。

## 5. 一条观察，不判成缺陷

`tmp_e2_branch` 里那个 Join 有 sync 边却没有 ctrl 边，说明它是由 `linkNodes(LinkType::Sync, …)` **直写**的，
没有走 `builder_helpers.h#writeSyncStatement`（那条路是 `writeSyncLink` + `writeControlLink` 成对写）。

**记成观察，不判成漏写**：Join 不是可折叠、可搬动的 `Data`，缺一条 ctrl 边今天不改变任何 pass 的判断；
而"某处直写了一种边"本身是这个模型允许的（`docs/design/effects/edges.cn.md` 里 sync 边就是链的位置）。
它将来会不会变成问题，取决于有没有一个 pass 把 Join 当成"只有 ctrl 等待"的节点来处理。

## 6. 抽样声明

**以上是这 7 个程序上、78 个节点的读数，不是全称结论。** 至少两个写点没有被覆盖：inline 的 splice
（`draft.cpp#setSyncInputs` 的搬运）与各 pass 自己写的 sync 边。要把它变成全称结论，需要在更多
pass 之后的图上重复同一测量。

## 7. 给以后改这 5 处的人

**先找一个"只有 sync 边的 `Data` 节点"出来，找不到就先别改。**
今天的 5 处即便补上 sync 读取，也可能和 §5.9 那行一样**永远不生效**——那比不补更糟：它会让"读边是对的"
变成一句没人验过的话。能找到那个节点，就同时拿到了用例（放 `test/cases/`，不是闸门检查项）。

这一夜这条线的产出不是绿数，而是：**把一个 9 处的批量改动缩到 5 处，并证明它今天不生效。**