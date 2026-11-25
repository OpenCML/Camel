# 给不出读取的读者，该不该有身份入口？—— `collectReachableRuntimeGraphs` 的 20 处调用点

2026-11-24 ｜ 来自 `lane/view-graph-identity` ｜ 对象：`camel/runtime/draft_session.h#collectReachableRuntimeGraphs`

## 0. 这一页问什么

视图车道的 E 段把三个入口改成了**按图身份作答**：`Program::calleeOf`、`GCGraphManager::programOf`、`Context::programOfRuntimeGraph` 都收 `const graph::Graph &`，答不出时答 nullptr（提交 `6b396eeff1`）。而**同一个问题在第四处没有做**：`RuntimeGraphDraftSession::collectReachableRuntimeGraphs()` 仍然返回 `std::vector<GCGraph *>`。

**这一页不是「漏了一处」，是一个问题：当一个读者紧接着要做的事只有某个表示能给出（编辑、记录、负载字节），它的入口该不该先按身份答一遍？** 答案会影响第 21 处调用点怎么写，所以记下来。

三条读数（本车道 `ed70bf059c`，`git grep -n "collectReachableRuntimeGraphs" -- src`）：

| 读数 | 值 |
|---|---|
| 该名出现处 | 22（1 处声明、1 处定义、**20 处调用点**） |
| 分布 | 13 个文件 |
| 20 处里「只用身份」的 | **1 处**（`src/stdlib/test/edges.cpp:448`：`if (graph != program->root())`） |

## 1. 20 处调用点，紧接着要什么

按调用点下来第三行读的，不是猜的：

| 位置 | 紧接着要的东西 |
|---|---|
| `kernel/runtime/retype.cpp:362` | `session.edit(graph)` |
| `std/…/generic/generic.cpp:1998` | `graph->name()`、`graphContentHash(*graph)`（负载） |
| `std/…/generic/rewrite.cpp:755` | `mayRewrite(*g)`、`session.edit(g)` |
| `std/…/inline/engine.cpp:864` | 计数日志，随后同闭包内 `edit` |
| `std/macro/macro.cpp:104` | 传给 `collectMacroCandidates(std::span<GCGraph *const>)` |
| `tensor/passes/fuse.cpp:216` | `session.edit(g)` |
| `tiles/src/region_outline.cpp:326` | 与 `session.madeGraphs()` 做成员比较 |
| `tiles/src/region_outline.cpp:462` | 遍历闭包读区域 |
| `tiles/src/region_outline.cpp:1021` | `graph->funcType()` |
| `tiles/src/region_test_pass.cpp:295` | `graph->id()`、`graph->node(i)` |
| `tiles/src/region_test_pass.cpp:315` | `analyzeRegions(*result, *graph, …)`、节点种类 |
| `test/fault.cpp:23,113,312` | `session.edit(owner)` |
| `test/fault.cpp:51` | `owner->name()` |
| `test/bounds.cpp:68`、`test/inlineuse.cpp:105` | `session.edit(graph)` |
| `test/loop_collect.cpp:240` | 交给 `rewriteGraph(session, owner, …)`，里面 `edit` |
| `test/native_call.cpp:49` | `owner->node(id)` |
| `test/edges.cpp:447` | 只与 `program->root()` 比身份 |

**19 处要表示，1 处只要身份。** 这 19 处里没有一处是在问「这是哪张图」，它们都在问「我拿到了这张图的哪一份东西」——编辑要 committed 的那一份，哈希要字节，`funcType` 要记录。`GraphView` 三者都不给（`docs`、`graph_view.h`）。

## 2. 已经做了的那一半，和它的读数

同一段里能按身份问的已经改完：三个入口 + `GraphAccess::callee`（`Context::create` 注入）+ `ArrangeWalk` 跨调用 + 7 处调用点解引用。读数是 `test::program_identity` 的两行：

```
[PI] sameness callees=2 sites=3 shared=2      # 两处不同文本命名同一张图
[PI] foreign draft=1 holds=0 program=0        # 同一份文本、同节点同类型、另一个身份
```

## 3. 没做的那一半，为什么

**换 handle 只是把 cast 挪个位置。** 19 处里有 9 处紧接着 `session.edit(graph)`，而 edit 要的就是 committed 图（它要写一份草稿再和新程序对比）；把返回类型换成 `const graph::Graph *`，这 9 处就要各写一次 `static_cast<const GCGraph *>` 才能开事务——那次 cast 与今天的 cast 是同一个事实，只是位置从 `collectReachableRuntimeGraphs` 换到了 9 个读者手里。

**今天有两条证据说明「挪一次 cast」不是无害的**：

- 破坏实验 A：把 `Program::calleeOf` 改成按节点编号在 program 的 root 里解析，程序**在编码阶段**就被拒（`Runtime graph 'pair' has FUNC node 1 whose callee graph is not encoded.`）。也就是说这个入口被编码器自己用着，改错它会先炸在别人那里。
- `[PI] foreign` 那条读数：一份草稿与它草拟的图有同一个节点数、同一批类型，而身份不同，Program 答「我不持有」、Context 答「没有 program 命名它」。如果入口只说「这是一张图」，读者就会把草稿当图读；差异只有在有人按身份问的时候才显形。

所以这一处的判断是：**入口的形状应当由「读者接下来能做什么」决定，而不是由「参数看起来该是什么」决定**。19 处读表示，入口就答表示；那 1 处（`edges.cpp`）已经在用 `program->root()` 比身份，它不需要入口帮忙。

## 4. 三个候选答案

- **A（现状，默认）**：留 `std::vector<GCGraph *>`，理由写在本页 §3。代价：第 21 处若真的是个「只问身份」的读者，它得自己从 `GCGraph *` 退到 `graph::Graph`（一次隐式上转，不写 cast）。
- **B**：返回 `std::span<const graph::Graph *const>`，19 处各 `static_cast` 一次。好处是 cast 显形（谁要知道它是哪一族，谁就写出来）；代价是 19 处各写一次，且这 19 处其实都不是在猜。
- **C（不推荐）**：给 session 加 `edit(const graph::Graph &)`，让它按身份开事务。那等于让 session 也按身份作答——而 session 恰好是「我知道这是本程序 committed 图」的持有者，按身份开事务会把它变成「谁都能递进来一张图」。

## 5. 什么会让这一页有答案

第 21 处调用点出现时，看它紧接着那一行：若它要的是 `edit`/`node`/`name`/`funcType`/字节，A 继续成立，本页只需加一行；若它在问「这是哪张图」（像 `edges.cpp` 那样），那么判据出现了，答案变成 B 或一个只答身份的新入口，而 19 处的 cast 那时才值得。

## 6. 这一页的一句读数（给结论页）

> **root 的草稿 = 同一份文本、同一个节点、同一个类型，而图身份不同：Program 答「我不持有它」，Context 答「没有 program 命名它」。** —— `[PI] foreign draft=1 holds=0 program=0`

## 7. 出处

- 分支 `lane/view-graph-identity`，提交 `6b396eeff1`（D/E 主体）、`ed70bf059c`（读数钉住）。
- 命令：`git grep -n "collectReachableRuntimeGraphs" -- src`（20 处调用点）；本页表格第三列读的是每个调用点后三行。
- 读数所在计划：`test/plans/feat/opt/graph_identity.plan.toml`（`test::view_types`、`test::program_identity`）。