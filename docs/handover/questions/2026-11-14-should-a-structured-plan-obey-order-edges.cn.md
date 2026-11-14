# 结构化计划该不该遵守 order 边？

## 问什么

`std::prl`（`std::taskflow`）有两条执行路径，它们对 **order 边**的答案不一样：

- **逐节点任务图**（`connectRuntimeDependencies`，`std::tfdump` 画的就是它）：遵守三种边，
  order 边只让两个**都要跑**的节点分先后（`edges.cn.md` 2）；
- **结构化计划**（`buildStructuredPlan` 的 `collectWaits`，真正跑的那条）：只读 control / data /
  sync，**不读 order**。

`docs/design/effects/edges.cn.md` 4 的裁法是"调度：拓扑序遵守三种边"，`std::nvm` 只有一条线性
顺序，天然遵守三种边。所以同一个 pass 的两条路径里，**只有结构化计划违反这条裁法**。

2026-11-14 把"一个节点等谁"抽成唯一实现 `execute::executionPredecessorsOf` 时，这个差异被
显式留在一处：`src/passes/sched/taskflow/taskflow.cpp#planObeysPredecessor`（一个 `constexpr`，
只把 `Kind::Order` 滤掉）。第 4 路（执行语义）没有擅自统一，因为它不是"实现分歧"，是**语义裁决**。

## 读数

**同一个程序，两个后端今天就不一致**（基线 `11dce6488` 的构建，`-t 4`）：

| 程序 | `std::nvm` | `std::prl` | 断言它的计划 |
| --- | --- | --- | --- |
| `test/cases/para/core/concurrent_calls.cml` | `calls that met a peer: 0` | `...: 2` | `feat/para/core.plan.toml#concurrent_calls.prl`（期望 `2`） |
| `test/cases/para/core/concurrent_recursion.cml` | `leaves that met a peer: 0 of 8` | `...: 8 of 8` | `... #concurrent_recursion.prl`（期望 `8 of 8`） |

**把结构化计划改成遵守 order 边之后**（2026-11-14 未加过滤的那一版构建，只改前述一处）：

| 读数 | 加过滤（今天） | 不加过滤 |
| --- | --- | --- |
| `concurrent_calls.cml`，`std::prl bytecode=false` | `calls that met a peer: 2` | `calls that met a peer: 0` |
| `concurrent_recursion.cml`，`std::prl bytecode=false` | `leaves that met a peer: 8 of 8` | `leaves that met a peer: 0 of 8` |
| `concurrent_calls.cml`，`std::prl segments=true fanout_all=true` | `both` 切 3 chain、as tasks | `both` 0 chain、inline（两个调用被排成一前一后） |
| `merge_sort_small.cml`，同上 | `mergesort` 11 chain(s) | 12 chain(s) |
| `feat/para/exec_pred`（27 条）与 `std::prl` 的 89 个程序 × 3 配置 | 27/27 绿；267 次比较零差异 | 上述两条计划断言变红 |

也就是说：**改动之前**这两条已断言的输出，正是靠结构化计划不读 order 边才绿的；而"两次调用真的
并发"这件事本身是对的，错的是它们被一条不该存在的顺序边排成了一前一后（下面"答"一节说明那条边
是怎么消失的，以及期望为什么要按后端分开写）。

## 为什么这不是"实现问题"

order 边不是凭空来的：`src/compile/gir/builder_resources.cpp#orderEffectsOnOneResourceIn` 对
**同一资源**上的两个效应按写下的先后写一条顺序边（原则 3.3，"同一资源自动有序"），而对
**footprint 没有被命名**的效应，把它排在同一个块里**它之前写下的每一个效应**之后。两个
`rendezvous` 调用共享一个 rendezvous 点，于是拿到一条 order 边；`concurrent_calls.cml` 的注释写着
"Not a sync block: nothing orders the two calls"，**这句话在今天的图上已经不成立**（`std::jgir` 里
能直接看到 `edge order g4n1 -> g4n2`）。

于是缺的是一条规则，不是一个写法：

1. **要么**结构化计划遵守 order 边（跟 `edges.cn.md` 4 一致），代价是上面两条程序要重写期望，
   而"两条调用真的并发"这件事要换一种写法表达（例如让那个 rendezvous 点声明成无序资源，
   构造器就不再写那条边）；
2. **要么**承认 `orderEffectsOnOneResourceIn` 对未命名 footprint 的排序过强（"不同资源之间没有
   先后，除非在同一个同步块里"），把那条边收窄，order 边自然消失；
3. **要么**明说"结构化计划可以不遵守 order 边"，但那要写进 `edges.cn.md` 4，否则规范和实现对不上。

## 答（按原则 3.1 与 3.3 落地）

上面三个选项都不是单独一条，而是 **1 和 2 各取一半**，因为两处各自缺的是不同的东西：

- **计划遵守所有顺序边，没有例外**（原则 3.1：顺序边表示"两端都跑时，前者先于后者"，任何后端都
  不能无视它）。`planObeysPredecessor` 已删掉：`collectWaits` 与逐节点任务图现在读同一份
  `executionPredecessorsOf`，四种边全等。删之前计划唯一不等的边种就是 `order`，删之后一种也不剩。
- **错在 `rendezvous` 的声明，不在排序规则**。上面的读法要修正一句：`rendezvous` 的 footprint
  一直是**被命名**的（`modules/test_support/module.cpp` 声明它碰 `rendezvous_point`），那条边来自
  "同一资源自动有序"（原则 3.3），而不是未命名 footprint 的兜底。真正的缺口是那个资源没有说出
  "无序"：`test_support` 现在把它声明成 `export resource rendezvous_point unordered`，构造器于是
  不再在两次调用之间写顺序边，而计划仍然遵守它所看到的每一条边。

于是两个后端各按自己的方式都是对的：`std::nvm` 只有一条线性顺序，两次调用一前一后，`0` / `0 of 8`；
`std::prl` 把两次调用并发，`2` / `8 of 8`。两条用例的期望因此**按后端分别写**
（`feat/para/core.plan.toml` 的 `concurrent_calls.nvm` / `.prl`、`concurrent_recursion.nvm` / `.prl`），
头部注释写明它们看的是调度器能不能并发、在语言的确定性保证之外，并且它们**不进差分测试**：
`feat/para/exec_pred` 只收两个后端必须逐行相同的程序。

读数（本分支构建，`-t 4`）：

| 程序 | `std::nvm` | `std::prl` |
| --- | --- | --- |
| `concurrent_calls.cml` | `calls that met a peer: 0` | `calls that met a peer: 2` |
| `concurrent_recursion.cml` | `leaves that met a peer: 0 of 8` | `leaves that met a peer: 8 of 8` |

`std::tfdump` 上能直接读到这两件事：`concurrent_calls.cml` 的 `both` 里两次调用都只等参数
（`# 1 <- [data:0]`、`# 2 <- [data:0]`，其间没有 `order:`），而 `exec_pred/order_edge.cml` 的
第二次 `println` 等的正是 `# 5 <- [order:2, ...]`。

`feat/para/exec_pred` 里那条"计划可以无视 order 边"的限制句连同它举的两个例子一起删了，换成一个
反方向的用例 `order_edge.cml`（两次 `println` 之间唯一的边就是顺序边，`std::nvm` 与 `std::prl`
的三种配置必须逐行相同）。