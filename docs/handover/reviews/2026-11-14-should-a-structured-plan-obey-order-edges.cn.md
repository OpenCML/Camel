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

也就是说：**今天这两条已断言的输出，正是靠结构化计划不读 order 边才绿的。**

## 为什么这不是"实现问题"

order 边不是凭空来的：`src/compile/gir/builder_resources.cpp#orderEffectsOnOneResourceIn` 对
**footprint 没有被命名**的效应（例如一个 `sync` 函数的调用），把它排在同一个块里**它之前写下的
每一个效应**之后。两个 `rendezvous` 调用共享一个 rendezvous 点、footprint 又没被命名，于是拿到一条
order 边；`concurrent_calls.cml` 的注释写着"Not a sync block: nothing orders the two calls"，
**这句话在今天的图上已经不成立**（`std::jgir` 里能直接看到 `edge order g4n1 -> g4n2`）。

于是缺的是一条规则，不是一个写法：

1. **要么**结构化计划遵守 order 边（跟 `edges.cn.md` 4 一致），代价是上面两条程序要重写期望，
   而"两条调用真的并发"这件事要换一种写法表达（例如把它们放在不被那条保守排序覆盖的位置）；
2. **要么**承认 `orderEffectsOnOneResourceIn` 对未命名 footprint 的排序过强（"不同资源之间没有
   先后，除非在同一个同步块里"），把那条边收窄，order 边自然消失；
3. **要么**明说"结构化计划可以不遵守 order 边"，但那要写进 `edges.cn.md` 4，否则规范和实现对不上。

## 默认建议（按它开工，不等回复）

**保持现状**：不动 `planObeysPredecessor`。理由是改它会动两条已断言输出，而哪一侧对属于作者裁决；
第 4 路（执行语义）已经把差异收在一处、写进提交信息、并用 `feat/para/exec_pred` 钉住了两种后端
都同意的那部分（差分 24 条，其中 3 条把 `executionPredecessorsOf` 的 `(节点, 边种)` 集合逐行冻住，
多一条、少一条都红）。

**翻转的代价是一处**：删 `src/passes/sched/taskflow/taskflow.cpp` 里的 `planObeysPredecessor`
（当前一个 `constexpr`，只滤 `Kind::Order`），并把 `feat/para/core.plan.toml` 的
`concurrent_calls.prl` / `concurrent_recursion.prl` 两条期望改成 `0` / `0 of 8`
（或改写这两个程序让它们不再依赖 order 边未被执行）。

`predecessor` 这条差异记在 `taskflow.cpp#planObeysPredecessor` 一处，便于作者一键翻转。
## 决策者答复（2026-11-14）

见 `docs/handover/reviews/questions-2026-10-29.md`"返工（2026-11-14）：taskflow 仍不守顺序边，不必等作者裁定"：taskflow 守所有顺序边，没有例外，`planObeysPredecessor` 删掉；两个并发用例的问题在 `rendezvous` 没有如实声明访问，改为声明一个 `unordered` 资源并标 `nondeterministic`，用例写明它在确定性保证之外。
