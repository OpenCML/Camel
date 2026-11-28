# 页：`edit`/`view` 块写法（task-464 施工稿）

> 状态：**施工稿，未落代码**。只读准备（2026-11-28 夜）。基线 `origin/integration@6aaf4555da42d147f4da1d977ce11616b369e71f`。
> 闸门：未合对数 ≥ 20 时不开枝；开枝由负责人下令。
> 本页只写三件：①规格清单（带出处）②三处改点（共用判据）③例子逐行改写（含两处未定的默认写法）。

## 1 规格（逐条，带出处）

**任务行** `internal/plans/2026-11-28-to-midnight.cn.md:11`：
1. 顺序边挂在**执行读的算子**上；
2. "哪些节点吃顺序"**只有一个判定**；
3. **建边与校验都调它**；
4. `examples/case-study/edits.cml` 改回**块写法**；
5. 21:00 读数：块里的 `graphs(d)`、`erase`、`replace` 各跑通；
6. 24:00 读数：例子 8 条在块写法下全绿、**金标不改**。

**规范** `docs/design/graph-editing/README.cn.md`（§1–13 已定）：
7. 块=作用域构造：句柄出不了块（§4）；进块自动开草稿、块尾自动提交、中途失败整份作废（§6、L11）。
8. 编译之后块体是**普通闭包**，执行时没有特殊对待（L11）。
9. `view` 块的值=体最后的表达式；`edit` 块的值=提交得到的新函数（L53）。
10. `edit` 值类型 `G!` 由块外期望类型给出，块头不写类型（L54）。
11. `edit` 本身纯；体里有外部访问时整个表达式才有该效应（L56）。
12. 块内语句照普通语义；同一草稿上的写**按书写顺序**，不写 `sync`（L102）。
13. 改图算子只收 `edit` 块的句柄；查询算子两种块都收（L68）。
14. "只能在块里调用"由**签名里的句柄类型**推出，不加关键字/标记（§3、L61–70）。
15. `order(a,b)` 新增；提交按"顺序完整"检查；跨区域先提升到公共区域代表再连边（L119）。
16. `inline` 展开的代码接过被替换节点在同步链与顺序边上的位置（L117）。
17. 提交六项（L155）：出口在顶层 / 无环 / 作用域（**每条输入**：数据、同步、顺序、GATE 等待）/ 端口与出口类型 / 效应包含 / 冲突有先后。

**替换目标（不是本页范围，但决定"不许在旧机制上补"）** `internal/plans/2026-11-27-blocks-as-scopes.cn.md`：
18. 删 `BlockPrograms`、`block_form`、块专用内核、`src/stdlib/test/block_body.cpp`、`function_emitter.cpp` 的拒绝段；验收 `git grep -n "BlockPrograms\|blockPrograms" -- src` = 0。

## 2 改动面：同一个"块体即程序"今天有三套写法

| # | 位置 | 现在怎么写 | 页要求 | 判定 |
|---|---|---|---|---|
| 1 | `src/compile/src/gct/builder_expr.cpp:166` `visitBlockHeaded`（`open` L226-229、`commit_of` L230-232、`attempt` L266-268） | 块体=闭包，edit 在故障边界内 open→体→commit_of | 块体=普通闭包（L11） | **保留侧，不动** |
| 2 | `src/kernel/runtime/block_programs.{h,cpp}`、`block_form.cpp`、`context.cpp:337`、唯一注册点 `src/stdlib/test/block_body.cpp:457` | 块体编成**单独程序**、专门内核跑 | 设计 L45「没有'程序'这个单独概念」 | **整条删**（替换） |
| 3 | `src/stdlib/std/cpp/function_emitter.cpp:1129` | 见块算子就拒绝导出 | 目标里没有块算子 | 随之消失 |
| 4 | `examples/case-study/edits.cml:10,44-49,87-106,109-149` | 函数写法（`open`/`commit`/`abort`、`Draft<F>`、`graphs_of`） | 改回块写法 | 本页 §4 |
| 5 | `src/compile/src/gir/builder_helpers.h:747` `nodeTakesAnOrder`（建边）｜`src/kernel/runtime/graph_edges.cpp:647`→`:1059` `nodeTakesNoOrder`（校验）｜`src/compile/src/gir/graph_encode.cpp:322`（编码器丢边） | **同一个问题被问三次、判据两套** | 只有一个判定，三处都调它 | 本页 §3 |

## 3 共用判定：`orderBearingNode(adapter, ref)`（三处都改）

**今天的两套判据（逐行读过）**

- 建边侧 `builder_helpers.h:747 nodeTakesAnOrder(node)`：
  1. `nodeStandsForItsRegion(node)` → `true`；
  2. `kind == Oper` → `operIdOf(node) != kNoOper && !camel::facts::operatorArrangeable(id).all()`；
  3. 其余 → `!camel::facts::nodeIsFreelyArrangeable(*nodeDraftOf(node), nodeIdOf(node))`。
- 校验侧 `graph_edges.cpp:1059 nodeTakesNoOrder(graph, ref)`：
  1. `takesItsOperandAsItsType(shapeOfOperation(graph, ref))` → **不拒**（`false`）；
  2. 否则 `nodeIsFreelyArrangeable(graph, ref) && !nodeStandsForItsRegion(graph, ref)`。

**两处差异的性质不同，不要混为一谈**

- **转换（cast）这一项：不是"两种读法"，是缺陷。** 冻结图上按 `shapeOfOperation` 能问出"它把操作数当自己的类型"，草案里按算子 id 同样问得出（`:op/cast`）。注释 `graph_edges.cpp:1060-1066` 自己记了：按 kind 半边问答 None，于是编码器丢掉了一条校验必须留的边（量过 `opt.edges/order_edge_survives_the_commit` 等五条）。⇒ **共用判定里这一项两个 adapter 都必须答**（draft 用 operator id，冻结图用 shape），不许留成"只有校验侧认"。
- **算子未知（`kNoOper`）这一项：是真正的适配参数。** 草案里节点负载还没写，可能没有算子；冻结图里不存在这种节点。⇒ 成为 adapter 的显式项 `unknownOperatorTakesOrder`（草案 `false`：不建边，注释说"缺一条顺序只是让两笔写保持书写顺序"；冻结图 `true`，不可达）。

**形状（示意，落代码时按仓库命名习惯改）**

```cpp
// camel/facts/arrangeable.h
struct OrderBearingAdapter {
    void *subject;                                   // GraphDraft* / const GCGraph*
    bool (*standsForItsRegion)(void *, NodeRef);     // 区域主人由区域承载顺序
    bool (*freelyArrangeable)(void *, NodeRef);      // 3.4 的四项
    bool (*takesItsOperandAsItsType)(void *, NodeRef); // cast：两侧都答（草案按 oper id，冻结图按 shape）
    bool unknownOperatorTakesOrder;                  // 草案 false / 冻结图 true
};
bool orderBearingNode(const OrderBearingAdapter &, NodeRef);
```

**三处改点（都改，不许只改一处）**

1. `src/kernel/runtime/graph_edges.cpp:647`（`EdgeChecker::checkOrderEdges()` 内）：
   `if (nodeTakesNoOrder(graph_, edge.user))` → `if (!orderBearingNode(frozenAdapter(), edge.user))`；
   `:1059 nodeTakesNoOrder` 降成 `frozenAdapter()` + 取反，**只此一处**保留 `takesItsOperandAsItsType` 的 shape 读法；
2. `src/compile/src/gir/builder_helpers.h:747 nodeTakesAnOrder`：降成 `draftAdapter()` 的薄包装（不再自己拼三项）；`Oper` 分支的 `kNoOper` 处理搬进 adapter 的 `unknownOperatorTakesOrder`，**cast 项补上**（草案按 `operIdOf` 问 `takesItsOperandAsItsType`）；
3. `src/compile/src/gir/graph_encode.cpp:320-325` 的 `OrderDropped` 回调：改成调同一个 `orderBearingNode(frozenAdapter(), ref)`。**它是第三个问同一问题的地方**：它今天把"自由排布的节点"上的边丢掉，坏边因此进不了冻结图。

**验收读数（开枝后）**：`test/plans/feat/opt/edges.plan.toml`（含 `order_edge_on_a_pure_node_is_refused`、`order_edge_orders_the_nodes_that_run`、`order_edges_survive_a_removed_node`）、`feat/linear/order_edges*`、`feat/modules/graph_edits`、`probe/editview`。基线读数见 §5。

## 4 `examples/case-study/edits.cml` 逐行改写（默认写法已按负责人裁定）

**默认（负责人 2026-11-28）**：① 块写法里"主动不提交"取"**不改稿就提交**"（可测、不引入新语义）；② 体内**裸 `return` 先按不允许**处理（要么诊断拒绝，要么页里明写"结束块并丢弃草稿"）——宁可响亮拒绝，不许静默丢弃。
⇒ 下面两处 `break_rate` 的写法**不用裸 return、不用 `if … else` 当语句**：把"必须是 1"收进已有的 `holds_the_pair`，用空集合的 `foreach` 表达"不改稿"。

| 改前（行） | 改后 |
|---|---|
| `L10 import { Draft, Graph, Node, abort, commit, erase, graphs, graphs_of, is_constant, nodes, op, op_name, open, replace, type_name, type_of } from graph` | `import { Graph, Node, erase, graphs, is_constant, nodes, op, op_name, replace, type_name, type_of } from graph` |
| `L44-49 all_nodes::<F>(p: F) … graphs_of(p)` | **删**；与 `L51-56` 合并成一个：`func all_nodes(g: Graph) => Node[] { return flatten(graphs(g)->map<nodes>) }`（`draft_nodes` 这个名字随之消失，块里两个调用点都用 `all_nodes(d)`） |
| `L87-93 func erased::<F>(d: Draft<F>, hits: Node[]) => F! sync { …commit(d) }` | **删**（体搬进 `drop_print` 的块） |
| `L95-100 func declined::<F>(d: Draft<F>, p: F) … { abort(d); return p }` | **删**（默认①：不改稿就是提交） |
| `L102-106 func swapped::<F>(d, old, new) … { replace(old,new); return commit(d) }` | **删** |
| `L108-114 list`：`all_nodes(p)->foreach<func (n: Node) { println(looks(n)) }>` | `return view d = p { all_nodes(d)->foreach<func (n: Node) { println(looks(n)) }> }`（`view` 的值=体的值=void；`return` 交回 `p` 不变？——**见待确认 a**） |
| `L116-121 drop_print`：`let d = open(p)` / `let prints = draft_nodes(d)->filter<is_print>` / `return if len(prints)==0 then declined(d,p) else erased(d,prints)` | `return edit d = p { all_nodes(d)->filter<is_print>->foreach<func (n: Node) => void { erase(n) }> }`（空集合=不改稿=原样提交） |
| `L136-149 break_rate`：`let d = open(p)` / `let where = graphs(d)->filter<holds_the_pair>` / 三层 `match` + `declined`/`swapped` | `return edit d = p {`<br>`    graphs(d)->filter<holds_the_pair>->foreach<func (g: Graph) => void {`<br>`        replace(floats_in(g)[0], models_in(g)[0])`<br>`    }>`<br>`}` |
| `L77-80 one_graph` / `L82-85 one_node` | **删**（"必须是 1"已由 `holds_the_pair` 保证，`[0]` 因此安全） |

**改写后 `break_rate` 为什么仍是同一件事**：`holds_the_pair(g)` = `len(floats_in(g))==1 && len(models_in(g))==1`（L72-75，未改），所以 `[0]` 恰是"唯一的那一个"；没有这样的图时 `foreach` 体一次也不跑，草稿没写，块照常提交 ⇒ 程序原样。这与今天 `declined` 的可观察结果相同（程序不变、exit 0、读数为空），差别只在"提交了一份没改的副本"（默认①已裁定）。

**待确认（页里没写、我不自定）**
- **a**：`view` 块当语句用、值没人要，是否也报"结果没人用"（L56 只对 `edit` 说了）。若报，`list` 改为 `view d = p { … }` 之后的 `return p` 处就要另找写法；`list` 本身也可改成 `edit` 块并空提交，但那是另一条语义选择。
- **b**：`view` 的块值类型由体推出（L55），`all_nodes(d)->foreach<…>` 的值是否为 `void` 决定 a 的答案。
- **c**：`foreach` 的 lambda 形参 `g: Graph` 在块内是"句柄绑定"，句柄不得出 lambda——本页写法里它不出（只在体内用），符合 §4。

## 5 基线读数（`origin/integration@6aaf4555da`，`BUILD_EXIT=0`，`[TREE] clean`，tier fast，jobs 2）

宿主不静默（`[load] 32.78 → 44.99`，脚本自己打 WARNING）⇒ **只有名字集可用，时间不可比**。

| 计划 | `[SUMMARY]` | 红名 |
|---|---|---|
| `feat/linear/order_edges`、`feat/linear/order_edges_regions`、`feat/opt/edges`、`feat/modules/graph_edits`、`probe/editview`（一次跑） | `total: 58, pass: 56, fail: 2` | `order_edge_on_a_pure_node_is_refused`（`feat/opt/edges`）、`editview.sec2_log`（`probe/editview`） |
| `feat/examples`、`feat/modules/graph`（一次跑） | `total: 204, pass: 197, fail: 6, skip: 1` | `types_and_node_kind`、`node_kinds_match_the_graph`（`feat/modules/graph`）；`examples.case_study.drop_print.{nvm,fvm,jit,prl}`（`feat/examples`，`expected exit 0, got exit 1` + `output contains '[Error]'`） |

**观测 vs 假设（标签必须分开写）**
- **观测**：`order_edge_on_a_pure_node_is_refused` 在基线上红；`editview.sec2_log` 红；例子的 `drop_print` 四后端红，退出码 1 且有 `[Error]`。
- **假设（未证实，待原文）**：编码器（`graph_encode.cpp:320-325`）把自由排布节点上的顺序边丢掉 ⇒ 草案产不出坏图 ⇒ 该负例只能"直接写进冻结记录"；它今天没拒绝，说明拒绝路径本身有问题。**两条都要下一轮取 fail 原文才算读数**：`fail/examples/examples.case_study.drop_print.nvm/output.txt`、`fail/opt.edges/order_edge_on_a_pure_node_is_refused/output.txt`（`-FromThisRun`）。

## 6 落地顺序（开枝后）

0. 先取 §5 两条 fail 原文（判据核心：从红到绿）。
1. `facts` 层加 `orderBearingNode` + 两个 adapter（不加例外条件、不加旁路表）。
2. 三处改点同时改（`graph_edges.cpp:647`、`builder_helpers.h:747`、`graph_encode.cpp:322`）。
3. 跑 §3 验收读数：`feat/opt/edges`、`feat/linear/order_edges*`、`feat/modules/graph_edits`、`probe/editview`；逐名比对 §5，**不许写"全绿"**。
4. 例子改块写法（§4），再跑 `feat/examples` + `feat/modules/graph`；`drop_print` 从红到绿、`break_rate` 仍被拒（金标不改）。
5. 旧机制（表里 #2、#3）的删除是 `blocks-as-scopes` 的第 3 步，**不在本切片里**，但本切片不许往它上面加东西。