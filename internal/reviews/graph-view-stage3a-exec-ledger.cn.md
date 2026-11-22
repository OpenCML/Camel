# graph-view 阶段 3a：执行侧五个目录与 src/tools 的直连账

**这一路的账：573 → 497。**（nvm 28→26、fvm 104→96、prl 149→129、cpp 127→94、exec 97→94、tools 68→58。）

七十六处离开，四百九十七处留着。留的原因按类写在第 2 节，逐行落在第 7 节的附录。

## 0 怎么数

```bash
git grep -n 'GCGraph\|GraphDraft' -- 'src/stdlib/std/nvm/**' 'src/stdlib/std/fvm/**' \
  'src/stdlib/std/prl/**' 'src/stdlib/std/cpp/**' 'src/stdlib/std/exec/**' 'src/tools/**'
```

按行数。四条口径写死，好让后面每一笔同向可比：

1. **这一片里 `GraphDraft` 是 0 处**：执行侧的五个目录与 `src/tools` 不建可编辑图，所以数出来全是 `GCGraph`。两个旧类型的计数这一片只动了一半。
2. **子串与整词都记**：按整词是 579 → 503，按子串是 589 → 513；两者的差是 `GCGraphSyncEdge`、`GCGraphOrderEdge` 这 9 处边记录名。三个数同向同幅（都 −76），口径不影响结论；本页与提交说明用任务给的那条命令（按行）。
3. **注释也数在内**：本片有 9 行注释点这个类型名。
4. **数的是"点名的处数"，不是"读它的处数"**：一行只要写了这个类型名就计入；读它但没写名的行（`graph->name()`、`graph->dataIndex`）不计。这一条决定了第 2 节的形状——四百九十七处里，绝大多数是**声明**，不是读。

## 1 表

| 提交 | 目录 | 这一笔 | 本目录 | 六目录合计 |
|---|---|---|---|---|
| 基点 `d4b597106`（`origin/develop`） | — | — | 28 / 104 / 149 / 127 / 97 / 68 | 573 |
| `3c168c424` | nvm | 循环区域的判断改问视图，`apply` 里借来的根图改 `auto *` | 28 → 26 | 571 |
| `581ab33fe` | fvm | 八处借来的图改 `auto *`（字节码转储、FastVM 两个入口） | 104 → 96 | 563 |
| `1a9eb9a80` | prl | 二十处借来的图改 `auto *` | 149 → 129 | 543 |
| `4b08496cc` | cpp | 三十三处：借来的图改 `auto *`（两处顺带删掉因此无用的 `using`），`nodeOriginNear` 改收 `Graph *` 并问视图 | 127 → 94 | 510 |
| `940391d32` | exec | 三处借来的图改 `auto *` | 97 → 94 | 507 |
| `8fca97a5e` | tools | 十处借来的图改 `auto *` | 68 → 58 | 497 |

## 2 分类：四百九十七处按"这个名字在这里做什么"分

| 目录 | 声明/身份链 | 容器与键 | 载荷 | 空指针初始化 | 注释 | 名字引入 | const_cast | lambda 参数 |
|---|---|---|---|---|---|---|---|---|
| nvm | 24 | 0 | 1 | 0 | 0 | 1 | 0 | 0 |
| fvm | 74 | 16 | 1 | 2 | 1 | 0 | 0 | 2 |
| prl | 106 | 14 | 1 | 6 | 0 | 2 | 0 | 0 |
| cpp | 50 | 32 | 0 | 8 | 0 | 2 | 1 | 1 |
| exec | 59 | 8 | 20 | 2 | 3 | 1 | 1 | 0 |
| tools | 32 | 19 | 0 | 2 | 5 | 0 | 0 | 0 |

各类的留法，一句话一条：

- **声明/身份链（345 处）**：这些是形参、返回值、成员。它们不是"读图"的地方，是"把正在执行的这张图传下去"的地方；传它的上游是 `Program::root()`、`Program::calleeOf`、`Programs::graphOf`、`RunScope`、观察者回调，这些接口都写着 `GCGraph *`。改这一条链是 3c 的事（引用一律按值、`Graph` 只剩一个实现），不是 3a 的事；现在把形参换成 `GraphView &` 会立刻撞上载荷读（同一行下面就读 `dataIndex`），换不过去。
- **容器与键（89 处）**：`unordered_map<GCGraph *, …>`、`std::vector<GCGraph *>`、`std::span<GCGraph *const>`。图指针在这里是**身份**：执行计划的表按它做键、可达图的列表按它去重、宿主镜像按它记入口 pc。视图是"读一张图的面"，不是身份；这些要等身份也从 `GCGraph *` 换成 `Graph *`，那是 3c 的引用模型。
- **载荷（23 处）**：这一行既点了名又读了存储里的东西——`GCGraphSyncEdge`/`GCGraphOrderEdge` 的边记录、`->node(`、`->region(`、`execPlan()`、`SyncEdges()`。视图按设计不交载荷字节，也不交 `dataIndex`（图视图的注释：槽位归执行计划）。这一类要等载荷进算子记录、槽位进计划。
- **空指针初始化（20 处）**：`GCGraph *x = nullptr;`。`auto *x = nullptr` 推不出类型，名字必须写出来；这是 C++ 的规则，不是选择。
- **注释（9 处）**：句子的主语就是这个类型自己的行为（`GCGraph::nodeOrigin()` 之类），换成别的名字句子就不真了。
- **名字引入（6 处）**：`using camel::runtime::GCGraph;`。同一份文件里还有上一类的地方，删掉这行只是让那些地方写全名。
- **`const_cast`（2 处）与 lambda 参数（3 处）**：`const_cast<GCGraph *>(…)` 与 `[](const GCGraph *a, const GCGraph *b)` 都必须写类型；后者可以写成 `const auto *`，但那是把一个具名 lambda 变成泛型 lambda，属于另一件事。

## 3 七十六处是怎么走的

- **nvm（2）**：`hasLoopRegions` 改收 `const GraphView &`，问 `regionsOwnedBy`——一个 LOOP 拥有条件区与循环体区就是区域形，拥有不了就是旧的图形式；区域表不再由这个函数自己扫。`apply` 里 `program->root()` 的那个指针改 `auto *`：NodeVM 借它，不拥有它。
- **fvm（8）**：`bcdump.cpp`（2）、`fastvm.cpp`（2）、`jit/dump/{asmdump,bindump,mirdump}.cpp`（4）里 `GCGraph *x = …` 改 `auto *`。它们接下来仍读 `execPlan()` 与载荷，只是不再宣告所有权。
- **prl（20）**：`plan.graph`、`graphs[i]`、`work.back()`、`calleeOf` 的答案改 `auto *`。`taskflow.cpp` 里 `targetGraph = nullptr` 那一处没动（见第 2 节）。
- **cpp（33）**：`cpp_export.cpp` 的根图改 `auto *` 并删掉随之无用的 `using`（−2）；`function_emitter.cpp`（19）与 `program_emitter.cpp`（10）里借来的图改 `auto *`；`nodeOriginNear`（`fault_sites.{h,cpp}`）改收 `const camel::graph::Graph *`，问 `originOf` 与 `inputs(ref, Context/Normal)`——原点是形状、两个端口组是形状，视图两面都答，调用点照样传 `GCGraph *`（向上转成 `Graph *`）。
- **exec（3）**：`exec_plan.cpp`、`loop_frame.h`、`operator_call.h` 里 `frame.graph()` 的答案改 `const auto *`。
- **tools（10）**：`debug_core.cpp` 的 `runtimeGraph()` 与 `program_index.cpp` 的九处（队列、`calleeOf`、`outerOf`、五个 range-for）改 `auto *`。

## 4 作者点名的两条，在这里是什么样

- **`fillBodyOf`/`accsBodyOf` 必须按 kind 判**：本片里按 kind 取 body 的地方（`cpp` 发射器、`exec` 计划、`prl` 的 `graphIsHeavy`）都在**读载荷**，没有一处是"问完算子再取 fill 体"的错法。stage2-exec2 已经把"按 kind 决策"的那 8 处改成问算子；本片没有再出现决策点，所以这一笔没有一处是"决策"。
- **白名单**：本片没有"四种之一"式的白名单（那在 `src/stdlib/transforms` 一侧）。`GCNodeKind` 的 switch 在本片有 3 处（`prl/graphIsHeavy`、`cpp` 发射器两处），都是按 kind 取载荷的分派，不是白名单。

## 5 按编号枚举的循环：这一片要不要看见 JOIN

视图里 JOIN 与墓碑不占位置，所以"按 0..nodeCount 枚举 / 按编号走"的代码整批搬到视图上不会报错，只会少看见节点——这是"绿"的形状，红看不见它。这一片的答案是：**一个循环也没有搬**，而且这一片只有一处按编号枚举的循环，它留在原处有自己的理由。

先数一遍（六个目录，`for (… < nodeCount()/nodes().size()/nodeSlotCount())`）与"按引用取节点"：

- **按编号枚举：1 处**，`src/stdlib/std/exec/graph_runtime_support.cpp:874`。
- **按引用取节点**（`graph->node(ref)`、`->node(user)`）：多处，是"手里已经有这个引用，读它的类型与载荷"，不是枚举；引用若指向 JOIN，`containsNodeRef` 已经把它挡在外面，没有"少看见"的问题。
- **按引用遍历输入/用户/前驱**：`nvm` 的 `regionsOwnedBy`（在区域表里按 owner 反查，与旧代码读同一张表，所以 JOIN 与墓碑都不会多也不会少）、`cpp` 的 `inputs(ref, Context/Normal)`——都不是编号枚举。

那唯一一处按编号的循环：`for (gc_node_ref_t n = 0; n < graph->nodeCount(); ++n)`，给 BRCH 的 arm 建一张按位置下标的成员表（`member[n] = regions.within(regions.controlOf(n), armRegion)`）。它**要**看见 JOIN 吗？不用——它本来就用 `containsNodeRef(n)` 跳过空位。它不该搬的理由是另一条：`n` 是**计划里的位置**，数组按 `nodeCount()` 开路，这是执行计划的地址（`dataIndex` 那一类），视图按设计不答。所以它留在 `GCGraph` 上是对的，理由与 JOIN 无关。

这一片也**没有**踩另一种形状：五个执行目录里没有 `for (auto &n : graph->nodes())` 这种整表遍历（`src/tools` 里的 `context->nodes()` 是语法树，与图无关）。以后若有整表遍历要搬，第一个要回答的就是"这个数里包不包括 JOIN 与墓碑"。
## 6 闸门读数

六笔都在 `D:\Projects\Camel-wt\sm1` 上由 `internal/tools/camel-remote.ps1 -Plan … -Jobs 8 -NoGpuWatch` 在 Linux 主机上编译并跑计划，构建全部 `BUILD_EXIT=0`。

| 计划 | 读数 | 红的是什么 |
|---|---|---|
| `smoke` | `total: 10, pass: 10, fail: 0` | — |
| `smoke + feat/para`（prl 笔，同时编入 cpp/exec/tools 的改动） | `total: 114, pass: 114, fail: 0`，exit 0 | — |
| `smoke + feat/trans`（cpp 笔） | `total: 122, pass: 119, fail: 3` | `trans.pass_dump`、`trans.effect_trace`、`trans.cml` |
| `smoke + feat/tools`（fvm 笔与 exec/tools 笔） | 两次同为 `total: 81, pass: 64, fail: 6, skip: 11` | `grammar.tables`、`grammar.files`、`load_class.tree`、`tools.member_init.sweep`、`portability.std_includes`、`compile_bench.smoke` |

两组红都不是这一片打出来的：

- **`trans` 的三红是既有红，且有别的车道在基线上量过**：`internal/lanes.md` 里 `lane/canonical-order-independent` 那一行写着"三红（`trans.pass_dump`、`trans.effect_trace`、`trans.cml`）把 src 暂存掉后在基线 `9680035a4` 上逐项同红"。原因也查得到：`effect_trace` 找 `REPO_ROOT/exemplars/05_effect_order`，而 `exemplars` 早已搬到 `internal/exemplars`（`3bd5e1e7f`）；`print_then_run` 的 8 个 case 里两个指向 `test/cases/src/**`，这条分支没有这个目录（`ROUNDTRIP … differs=2` 正是这两个 `unmeasurable`）；`pass_dump` 要 `tools/graph-diff/graph_diff.py`，同样不在分支里。
- **`tools` 的六红**：`grammar.tables`、`grammar.files`（宿主没有 `editors/vscode/scripts`）、`load_class.tree`（同一原因的级联：它数的 `tools.grammar:*` 用例不存在）、`tools.member_init.sweep`（发现点在 `src/kernel/facts/include/camel/facts/facts.h`、`src/parse/include/camel/parse/ast/data.h`）、`portability.std_includes`（发现点在 `src/kernel/graph/consistency.cpp`、`draft_view.cpp`）、`compile_bench.smoke`（`benchmarks/compile/bench.py`）。这些文件本片一个都没有碰；fvm 笔与 exec/tools 笔两次读数逐项相同。

全量 `feat/**` 的验收见最后一笔提交说明；没有合 `develop`，基线是 `d4b597106`。

## 7 附录：四百九十七处逐行

每行一处：`文件:行 类`。类的意思见第 2 节。

```
src/stdlib/std/cpp/fault_sites.cpp:23  using
src/stdlib/std/cpp/fault_sites.cpp:42  sig
src/stdlib/std/cpp/fault_sites.cpp:82  sig
src/stdlib/std/cpp/fault_sites.cpp:97  sig
src/stdlib/std/cpp/fault_sites.cpp:194  sig
src/stdlib/std/cpp/fault_sites.h:42  sig
src/stdlib/std/cpp/fault_sites.h:58  sig
src/stdlib/std/cpp/fault_sites.h:66  sig
src/stdlib/std/cpp/fault_sites.h:72  sig
src/stdlib/std/cpp/function_emitter.cpp:40  sig
src/stdlib/std/cpp/function_emitter.cpp:55  sig
src/stdlib/std/cpp/function_emitter.cpp:84  sig
src/stdlib/std/cpp/function_emitter.cpp:120  sig
src/stdlib/std/cpp/function_emitter.cpp:143  sig
src/stdlib/std/cpp/function_emitter.cpp:150  sig
src/stdlib/std/cpp/function_emitter.cpp:154  sig
src/stdlib/std/cpp/function_emitter.cpp:161  container
src/stdlib/std/cpp/function_emitter.cpp:235  sig
src/stdlib/std/cpp/function_emitter.cpp:274  sig
src/stdlib/std/cpp/function_emitter.cpp:289  sig
src/stdlib/std/cpp/function_emitter.cpp:334  sig
src/stdlib/std/cpp/function_emitter.cpp:406  sig
src/stdlib/std/cpp/function_emitter.cpp:455  sig
src/stdlib/std/cpp/function_emitter.cpp:1522  sig
src/stdlib/std/cpp/function_emitter.cpp:1563  sig
src/stdlib/std/cpp/function_emitter.cpp:1575  container
src/stdlib/std/cpp/function_emitter.cpp:1989  sig
src/stdlib/std/cpp/function_emitter.h:46  using
src/stdlib/std/cpp/function_emitter.h:51  local-null
src/stdlib/std/cpp/function_emitter.h:66  container
src/stdlib/std/cpp/function_emitter.h:74  local-null
src/stdlib/std/cpp/function_emitter.h:76  local-null
src/stdlib/std/cpp/function_emitter.h:79  local-null
src/stdlib/std/cpp/function_emitter.h:90  sig
src/stdlib/std/cpp/function_emitter.h:94  sig
src/stdlib/std/cpp/function_emitter.h:98  sig
src/stdlib/std/cpp/function_emitter.h:106  container
src/stdlib/std/cpp/function_emitter.h:114  container
src/stdlib/std/cpp/function_emitter.h:117  sig
src/stdlib/std/cpp/function_emitter.h:127  local-null
src/stdlib/std/cpp/function_emitter.h:142  local-null
src/stdlib/std/cpp/function_emitter.h:148  container
src/stdlib/std/cpp/function_emitter.h:176  sig
src/stdlib/std/cpp/function_emitter.h:203  sig
src/stdlib/std/cpp/function_emitter.h:209  container
src/stdlib/std/cpp/function_emitter.h:210  sig
src/stdlib/std/cpp/function_emitter.h:214  sig
src/stdlib/std/cpp/function_emitter.h:224  sig
src/stdlib/std/cpp/function_emitter.h:228  sig
src/stdlib/std/cpp/function_emitter.h:233  sig
src/stdlib/std/cpp/function_emitter.h:235  sig
src/stdlib/std/cpp/function_emitter.h:241  container
src/stdlib/std/cpp/function_emitter.h:245  container
src/stdlib/std/cpp/function_emitter.h:246  container
src/stdlib/std/cpp/function_emitter.h:247  container
src/stdlib/std/cpp/program_emitter.cpp:128  sig
src/stdlib/std/cpp/program_emitter.cpp:183  sig
src/stdlib/std/cpp/program_emitter.cpp:240  container
src/stdlib/std/cpp/program_emitter.cpp:241  container
src/stdlib/std/cpp/program_emitter.cpp:242  container
src/stdlib/std/cpp/program_emitter.cpp:243  container
src/stdlib/std/cpp/program_emitter.cpp:246  sig
src/stdlib/std/cpp/program_emitter.cpp:286  container
src/stdlib/std/cpp/program_emitter.cpp:287  container
src/stdlib/std/cpp/program_emitter.cpp:290  container
src/stdlib/std/cpp/program_emitter.cpp:291  container
src/stdlib/std/cpp/program_emitter.cpp:296  container
src/stdlib/std/cpp/program_emitter.cpp:301  sig
src/stdlib/std/cpp/program_emitter.cpp:302  local-cast
src/stdlib/std/cpp/program_emitter.cpp:308  container
src/stdlib/std/cpp/program_emitter.cpp:309  container
src/stdlib/std/cpp/program_emitter.cpp:310  container
src/stdlib/std/cpp/program_emitter.cpp:311  container
src/stdlib/std/cpp/program_emitter.cpp:313  sig
src/stdlib/std/cpp/program_emitter.cpp:326  container
src/stdlib/std/cpp/program_emitter.cpp:327  local-null
src/stdlib/std/cpp/program_emitter.cpp:344  container
src/stdlib/std/cpp/program_emitter.cpp:348  container
src/stdlib/std/cpp/program_emitter.cpp:350  sig
src/stdlib/std/cpp/program_emitter.cpp:355  sig
src/stdlib/std/cpp/program_emitter.cpp:372  sig
src/stdlib/std/cpp/program_emitter.cpp:567  sig
src/stdlib/std/cpp/program_emitter.cpp:580  sig
src/stdlib/std/cpp/program_emitter.cpp:588  container
src/stdlib/std/cpp/program_emitter.cpp:623  local-lambda
src/stdlib/std/cpp/program_emitter.cpp:627  container
src/stdlib/std/cpp/program_emitter.cpp:633  container
src/stdlib/std/cpp/program_emitter.h:38  sig
src/stdlib/std/cpp/program_emitter.h:43  local-null
src/stdlib/std/cpp/program_emitter.h:48  sig
src/stdlib/std/cpp/program_emitter.h:50  container
src/stdlib/std/cpp/program_emitter.h:53  container
src/stdlib/std/cpp/program_emitter.h:54  sig
src/stdlib/std/cpp/program_emitter.h:55  sig
src/stdlib/std/exec/exec_plan.cpp:32  payload
src/stdlib/std/exec/exec_plan.cpp:46  sig
src/stdlib/std/exec/exec_plan.cpp:65  comment
src/stdlib/std/exec/exec_plan.cpp:68  sig
src/stdlib/std/exec/exec_plan.cpp:121  container
src/stdlib/std/exec/exec_plan.cpp:156  payload
src/stdlib/std/exec/exec_plan.cpp:243  payload
src/stdlib/std/exec/exec_plan.cpp:469  container
src/stdlib/std/exec/exec_plan.cpp:571  sig
src/stdlib/std/exec/exec_plan.cpp:582  sig
src/stdlib/std/exec/exec_plan.cpp:601  sig
src/stdlib/std/exec/exec_plan.cpp:627  container
src/stdlib/std/exec/exec_plan.cpp:655  payload
src/stdlib/std/exec/exec_plan.cpp:656  payload
src/stdlib/std/exec/exec_plan.cpp:686  sig
src/stdlib/std/exec/exec_plan.cpp:801  payload
src/stdlib/std/exec/exec_plan.cpp:843  payload
src/stdlib/std/exec/exec_plan.cpp:848  payload
src/stdlib/std/exec/exec_plan.cpp:1046  sig
src/stdlib/std/exec/exec_plan.cpp:1109  sig
src/stdlib/std/exec/exec_plan.cpp:1162  sig
src/stdlib/std/exec/exec_plan.cpp:1207  container
src/stdlib/std/exec/exec_plan.cpp:1243  payload
src/stdlib/std/exec/exec_plan.cpp:1475  sig
src/stdlib/std/exec/exec_plan.cpp:1510  sig
src/stdlib/std/exec/exec_plan.cpp:1526  payload
src/stdlib/std/exec/exec_plan.cpp:2043  payload
src/stdlib/std/exec/exec_plan.cpp:2045  local-cast
src/stdlib/std/exec/exec_plan.cpp:2167  payload
src/stdlib/std/exec/exec_plan.cpp:2182  sig
src/stdlib/std/exec/exec_plan.cpp:2196  payload
src/stdlib/std/exec/fusion_group.cpp:101  payload
src/stdlib/std/exec/fusion_group.cpp:129  sig
src/stdlib/std/exec/graph_runtime_support.cpp:47  sig
src/stdlib/std/exec/graph_runtime_support.cpp:124  sig
src/stdlib/std/exec/graph_runtime_support.cpp:153  sig
src/stdlib/std/exec/graph_runtime_support.cpp:172  payload
src/stdlib/std/exec/graph_runtime_support.cpp:176  payload
src/stdlib/std/exec/graph_runtime_support.cpp:182  container
src/stdlib/std/exec/graph_runtime_support.cpp:231  payload
src/stdlib/std/exec/graph_runtime_support.cpp:468  payload
src/stdlib/std/exec/graph_runtime_support.cpp:538  payload
src/stdlib/std/exec/graph_runtime_support.cpp:552  payload
src/stdlib/std/exec/graph_runtime_support.cpp:663  sig
src/stdlib/std/exec/graph_runtime_support.cpp:675  sig
src/stdlib/std/exec/graph_runtime_support.cpp:728  sig
src/stdlib/std/exec/graph_runtime_support.cpp:740  sig
src/stdlib/std/exec/graph_runtime_support.cpp:802  sig
src/stdlib/std/exec/graph_runtime_support.cpp:839  container
src/stdlib/std/exec/graph_runtime_support.cpp:853  sig
src/stdlib/std/exec/graph_runtime_support.cpp:911  container
src/stdlib/std/exec/graph_runtime_support.cpp:1017  sig
src/stdlib/std/exec/graph_runtime_support.cpp:1039  sig
src/stdlib/std/exec/graph_runtime_support.cpp:1128  sig
src/stdlib/std/exec/graph_runtime_support.cpp:1193  sig
src/stdlib/std/exec/graph_runtime_support.cpp:1292  sig
src/stdlib/std/exec/include/camel/execute/fusion_group.h:66  sig
src/stdlib/std/exec/include/camel/execute/fusion_group.h:71  sig
src/stdlib/std/exec/include/camel/execute/fusion_group.h:108  sig
src/stdlib/std/exec/include/camel/execute/fusion_group.h:152  sig
src/stdlib/std/exec/include/camel/execute/graph_runtime_support.h:60  sig
src/stdlib/std/exec/include/camel/execute/graph_runtime_support.h:71  sig
src/stdlib/std/exec/include/camel/execute/graph_runtime_support.h:106  comment
src/stdlib/std/exec/include/camel/execute/graph_runtime_support.h:178  sig
src/stdlib/std/exec/include/camel/execute/graph_runtime_support.h:183  sig
src/stdlib/std/exec/include/camel/execute/graph_runtime_support.h:185  sig
src/stdlib/std/exec/include/camel/execute/graph_runtime_support.h:186  sig
src/stdlib/std/exec/include/camel/execute/graph_runtime_support.h:188  sig
src/stdlib/std/exec/include/camel/execute/graph_runtime_support.h:196  sig
src/stdlib/std/exec/include/camel/execute/graph_runtime_support.h:202  sig
src/stdlib/std/exec/include/camel/execute/graph_runtime_support.h:204  container
src/stdlib/std/exec/include/camel/execute/graph_runtime_support.h:209  sig
src/stdlib/std/exec/include/camel/execute/graph_runtime_support.h:215  sig
src/stdlib/std/exec/include/camel/execute/graph_runtime_support.h:232  sig
src/stdlib/std/exec/include/camel/execute/graph_runtime_support.h:238  sig
src/stdlib/std/exec/include/camel/execute/graph_runtime_support.h:252  sig
src/stdlib/std/exec/include/camel/execute/graph_runtime_support.h:257  sig
src/stdlib/std/exec/include/camel/execute/graph_runtime_support.h:261  sig
src/stdlib/std/exec/include/camel/execute/loop_driver.h:69  local-null
src/stdlib/std/exec/include/camel/execute/loop_driver.h:70  local-null
src/stdlib/std/exec/include/camel/execute/loop_driver.h:102  sig
src/stdlib/std/exec/include/camel/execute/loop_driver.h:260  comment
src/stdlib/std/exec/include/camel/execute/shaped_operators.h:112  sig
src/stdlib/std/exec/include/camel/execute/shaped_operators.h:132  sig
src/stdlib/std/exec/include/camel/execute/shaped_operators.h:173  sig
src/stdlib/std/exec/include/camel/execute/shaped_operators.h:179  sig
src/stdlib/std/exec/shaped_operators.cpp:35  using
src/stdlib/std/exec/shaped_operators.cpp:41  sig
src/stdlib/std/exec/shaped_operators.cpp:51  sig
src/stdlib/std/exec/shaped_operators.cpp:65  sig
src/stdlib/std/exec/shaped_operators.cpp:85  sig
src/stdlib/std/exec/shaped_operators.cpp:109  sig
src/stdlib/std/exec/shaped_operators.cpp:136  sig
src/stdlib/std/exec/shaped_operators.cpp:153  sig
src/stdlib/std/fvm/bcdump.cpp:13  container
src/stdlib/std/fvm/bytecode.h:20  sig
src/stdlib/std/fvm/bytecode.h:419  sig
src/stdlib/std/fvm/bytecode.h:443  sig
src/stdlib/std/fvm/bytecode.h:446  sig
src/stdlib/std/fvm/compile.cpp:36  payload
src/stdlib/std/fvm/compile.cpp:42  container
src/stdlib/std/fvm/compile.cpp:125  sig
src/stdlib/std/fvm/compile.cpp:744  sig
src/stdlib/std/fvm/compile.cpp:751  sig
src/stdlib/std/fvm/compile.cpp:758  local-lambda
src/stdlib/std/fvm/compile.cpp:763  sig
src/stdlib/std/fvm/compile.cpp:766  container
src/stdlib/std/fvm/compile.cpp:781  container
src/stdlib/std/fvm/compile.cpp:785  container
src/stdlib/std/fvm/compile.h:27  sig
src/stdlib/std/fvm/compile.h:33  container
src/stdlib/std/fvm/compile.h:38  sig
src/stdlib/std/fvm/compile.h:43  sig
src/stdlib/std/fvm/compile.h:47  sig
src/stdlib/std/fvm/compile.h:57  sig
src/stdlib/std/fvm/compile.h:68  container
src/stdlib/std/fvm/fastvm_stats.cpp:104  sig
src/stdlib/std/fvm/fastvm.cpp:52  sig
src/stdlib/std/fvm/fastvm.cpp:61  sig
src/stdlib/std/fvm/fastvm.cpp:66  sig
src/stdlib/std/fvm/fastvm.cpp:75  sig
src/stdlib/std/fvm/fastvm.cpp:81  sig
src/stdlib/std/fvm/fastvm.cpp:85  sig
src/stdlib/std/fvm/fastvm.cpp:91  sig
src/stdlib/std/fvm/fastvm.cpp:95  sig
src/stdlib/std/fvm/fastvm.cpp:99  sig
src/stdlib/std/fvm/fastvm.cpp:107  sig
src/stdlib/std/fvm/fastvm.cpp:111  sig
src/stdlib/std/fvm/fastvm.cpp:122  sig
src/stdlib/std/fvm/fastvm.cpp:126  sig
src/stdlib/std/fvm/fastvm.cpp:138  sig
src/stdlib/std/fvm/fastvm.cpp:143  sig
src/stdlib/std/fvm/fastvm.cpp:343  sig
src/stdlib/std/fvm/fastvm.cpp:360  sig
src/stdlib/std/fvm/fastvm.cpp:372  sig
src/stdlib/std/fvm/fastvm.cpp:964  sig
src/stdlib/std/fvm/fastvm.cpp:983  sig
src/stdlib/std/fvm/fastvm.cpp:1194  sig
src/stdlib/std/fvm/fastvm.cpp:1205  sig
src/stdlib/std/fvm/fastvm.cpp:1228  sig
src/stdlib/std/fvm/fastvm.cpp:1232  sig
src/stdlib/std/fvm/fastvm.cpp:1238  sig
src/stdlib/std/fvm/fastvm.cpp:1243  sig
src/stdlib/std/fvm/fastvm.cpp:1253  sig
src/stdlib/std/fvm/fastvm.cpp:1257  sig
src/stdlib/std/fvm/fastvm.cpp:1265  sig
src/stdlib/std/fvm/fastvm.cpp:1271  sig
src/stdlib/std/fvm/fastvm.cpp:1289  sig
src/stdlib/std/fvm/fastvm.cpp:1461  sig
src/stdlib/std/fvm/fastvm.cpp:1471  sig
src/stdlib/std/fvm/fastvm.cpp:1478  sig
src/stdlib/std/fvm/fastvm.h:73  local-null
src/stdlib/std/fvm/fastvm.h:220  sig
src/stdlib/std/fvm/fastvm.h:221  container
src/stdlib/std/fvm/fastvm.h:234  container
src/stdlib/std/fvm/fastvm.h:247  sig
src/stdlib/std/fvm/fastvm.h:250  sig
src/stdlib/std/fvm/fastvm.h:255  sig
src/stdlib/std/fvm/fastvm.h:260  sig
src/stdlib/std/fvm/fastvm.h:261  sig
src/stdlib/std/fvm/fastvm.h:262  sig
src/stdlib/std/fvm/fastvm.h:264  sig
src/stdlib/std/fvm/fastvm.h:265  sig
src/stdlib/std/fvm/fastvm.h:266  sig
src/stdlib/std/fvm/fastvm.h:282  sig
src/stdlib/std/fvm/fastvm.h:284  sig
src/stdlib/std/fvm/fastvm.h:322  sig
src/stdlib/std/fvm/fastvm.h:324  sig
src/stdlib/std/fvm/fastvm.h:406  sig
src/stdlib/std/fvm/fastvm.h:419  sig
src/stdlib/std/fvm/fastvm.h:420  sig
src/stdlib/std/fvm/fastvm.h:425  sig
src/stdlib/std/fvm/fastvm.h:428  sig
src/stdlib/std/fvm/fastvm.h:437  sig
src/stdlib/std/fvm/fastvm.h:454  sig
src/stdlib/std/fvm/fastvm.h:459  sig
src/stdlib/std/fvm/fastvm.h:461  sig
src/stdlib/std/fvm/fastvm.h:466  sig
src/stdlib/std/fvm/hosted.cpp:39  container
src/stdlib/std/fvm/hosted.cpp:49  local-lambda
src/stdlib/std/fvm/hosted.h:43  container
src/stdlib/std/fvm/hosted.h:46  container
src/stdlib/std/fvm/hosted.h:54  container
src/stdlib/std/fvm/jit/backend/backend.h:45  local-null
src/stdlib/std/fvm/jit/backend/x64/x64_backend.cpp:82  sig
src/stdlib/std/fvm/jit/dump/asmdump.cpp:51  container
src/stdlib/std/fvm/jit/dump/bindump.cpp:97  container
src/stdlib/std/fvm/jit/dump/mirdump.cpp:28  sig
src/stdlib/std/fvm/jit/dump/mirdump.cpp:51  container
src/stdlib/std/fvm/runtime_support.h:9  comment
src/stdlib/std/nvm/nodevm.cpp:48  using
src/stdlib/std/nvm/nodevm.cpp:156  payload
src/stdlib/std/nvm/nodevm.cpp:217  sig
src/stdlib/std/nvm/nodevm.cpp:280  sig
src/stdlib/std/nvm/nodevm.cpp:293  sig
src/stdlib/std/nvm/nodevm.cpp:828  sig
src/stdlib/std/nvm/nodevm.cpp:905  sig
src/stdlib/std/nvm/nodevm.cpp:936  sig
src/stdlib/std/nvm/nodevm.cpp:964  sig
src/stdlib/std/nvm/nodevm.cpp:991  sig
src/stdlib/std/nvm/nodevm.cpp:1024  sig
src/stdlib/std/nvm/nodevm.cpp:1055  sig
src/stdlib/std/nvm/nodevm.cpp:1080  sig
src/stdlib/std/nvm/nodevm.cpp:1128  sig
src/stdlib/std/nvm/nodevm.cpp:1196  sig
src/stdlib/std/nvm/nodevm.h:37  sig
src/stdlib/std/nvm/nodevm.h:44  sig
src/stdlib/std/nvm/nodevm.h:47  sig
src/stdlib/std/nvm/nodevm.h:80  sig
src/stdlib/std/nvm/nodevm.h:86  sig
src/stdlib/std/nvm/nodevm.h:93  sig
src/stdlib/std/nvm/nodevm.h:96  sig
src/stdlib/std/nvm/nodevm.h:99  sig
src/stdlib/std/nvm/nodevm.h:102  sig
src/stdlib/std/nvm/nodevm.h:105  sig
src/stdlib/std/nvm/nodevm.h:108  sig
src/stdlib/std/prl/handoff.cpp:22  using
src/stdlib/std/prl/handoff.cpp:42  container
src/stdlib/std/prl/handoff.cpp:152  sig
src/stdlib/std/prl/handoff.cpp:176  container
src/stdlib/std/prl/handoff.cpp:178  container
src/stdlib/std/prl/handoff.cpp:221  container
src/stdlib/std/prl/handoff.h:46  container
src/stdlib/std/prl/handoff.h:48  container
src/stdlib/std/prl/handoff.h:77  sig
src/stdlib/std/prl/taskflow_frame_pool.cpp:93  sig
src/stdlib/std/prl/taskflow_frame_pool.cpp:127  sig
src/stdlib/std/prl/taskflow_frame_pool.cpp:155  sig
src/stdlib/std/prl/taskflow_frame_pool.cpp:214  sig
src/stdlib/std/prl/taskflow_frame_pool.cpp:259  sig
src/stdlib/std/prl/taskflow.cpp:61  using
src/stdlib/std/prl/taskflow.cpp:75  local-null
src/stdlib/std/prl/taskflow.cpp:79  payload
src/stdlib/std/prl/taskflow.cpp:85  sig
src/stdlib/std/prl/taskflow.cpp:96  sig
src/stdlib/std/prl/taskflow.cpp:119  sig
src/stdlib/std/prl/taskflow.cpp:154  sig
src/stdlib/std/prl/taskflow.cpp:553  sig
src/stdlib/std/prl/taskflow.cpp:565  container
src/stdlib/std/prl/taskflow.cpp:566  container
src/stdlib/std/prl/taskflow.cpp:611  sig
src/stdlib/std/prl/taskflow.cpp:618  local-null
src/stdlib/std/prl/taskflow.cpp:657  container
src/stdlib/std/prl/taskflow.cpp:658  sig
src/stdlib/std/prl/taskflow.cpp:687  sig
src/stdlib/std/prl/taskflow.cpp:1367  sig
src/stdlib/std/prl/taskflow.cpp:1374  sig
src/stdlib/std/prl/taskflow.cpp:1391  local-null
src/stdlib/std/prl/taskflow.cpp:1739  sig
src/stdlib/std/prl/taskflow.cpp:1763  sig
src/stdlib/std/prl/taskflow.cpp:1783  sig
src/stdlib/std/prl/taskflow.cpp:1788  local-null
src/stdlib/std/prl/taskflow.cpp:1813  sig
src/stdlib/std/prl/taskflow.cpp:1837  sig
src/stdlib/std/prl/taskflow.cpp:1869  sig
src/stdlib/std/prl/taskflow.cpp:1906  sig
src/stdlib/std/prl/taskflow.cpp:1922  sig
src/stdlib/std/prl/taskflow.cpp:1952  sig
src/stdlib/std/prl/taskflow.cpp:1972  sig
src/stdlib/std/prl/taskflow.cpp:1985  sig
src/stdlib/std/prl/taskflow.cpp:2072  sig
src/stdlib/std/prl/taskflow.cpp:2097  container
src/stdlib/std/prl/taskflow.cpp:2106  container
src/stdlib/std/prl/taskflow.cpp:2129  sig
src/stdlib/std/prl/taskflow.cpp:2274  sig
src/stdlib/std/prl/taskflow.cpp:2297  sig
src/stdlib/std/prl/taskflow.cpp:2317  sig
src/stdlib/std/prl/taskflow.cpp:2427  sig
src/stdlib/std/prl/taskflow.cpp:2438  sig
src/stdlib/std/prl/taskflow.cpp:2506  sig
src/stdlib/std/prl/taskflow.cpp:2546  sig
src/stdlib/std/prl/taskflow.cpp:2555  sig
src/stdlib/std/prl/taskflow.cpp:2564  sig
src/stdlib/std/prl/taskflow.cpp:2574  sig
src/stdlib/std/prl/taskflow.cpp:2583  sig
src/stdlib/std/prl/taskflow.cpp:2592  sig
src/stdlib/std/prl/taskflow.cpp:2601  sig
src/stdlib/std/prl/taskflow.cpp:2666  sig
src/stdlib/std/prl/taskflow.cpp:2720  sig
src/stdlib/std/prl/taskflow.cpp:2869  sig
src/stdlib/std/prl/taskflow.cpp:2908  sig
src/stdlib/std/prl/taskflow.cpp:2955  sig
src/stdlib/std/prl/taskflow.cpp:2993  sig
src/stdlib/std/prl/taskflow.cpp:3111  sig
src/stdlib/std/prl/taskflow.cpp:3144  sig
src/stdlib/std/prl/taskflow.cpp:3184  sig
src/stdlib/std/prl/taskflow.cpp:3247  sig
src/stdlib/std/prl/taskflow.h:48  sig
src/stdlib/std/prl/taskflow.h:50  sig
src/stdlib/std/prl/taskflow.h:54  sig
src/stdlib/std/prl/taskflow.h:78  sig
src/stdlib/std/prl/taskflow.h:79  sig
src/stdlib/std/prl/taskflow.h:84  sig
src/stdlib/std/prl/taskflow.h:328  sig
src/stdlib/std/prl/taskflow.h:353  sig
src/stdlib/std/prl/taskflow.h:434  local-null
src/stdlib/std/prl/taskflow.h:472  container
src/stdlib/std/prl/taskflow.h:507  sig
src/stdlib/std/prl/taskflow.h:512  sig
src/stdlib/std/prl/taskflow.h:537  container
src/stdlib/std/prl/taskflow.h:538  sig
src/stdlib/std/prl/taskflow.h:586  sig
src/stdlib/std/prl/taskflow.h:596  sig
src/stdlib/std/prl/taskflow.h:598  sig
src/stdlib/std/prl/taskflow.h:599  sig
src/stdlib/std/prl/taskflow.h:605  sig
src/stdlib/std/prl/taskflow.h:610  sig
src/stdlib/std/prl/taskflow.h:624  sig
src/stdlib/std/prl/taskflow.h:633  sig
src/stdlib/std/prl/taskflow.h:639  local-null
src/stdlib/std/prl/taskflow.h:656  sig
src/stdlib/std/prl/taskflow.h:665  sig
src/stdlib/std/prl/taskflow.h:667  sig
src/stdlib/std/prl/taskflow.h:670  sig
src/stdlib/std/prl/taskflow.h:675  sig
src/stdlib/std/prl/taskflow.h:681  sig
src/stdlib/std/prl/taskflow.h:683  sig
src/stdlib/std/prl/taskflow.h:686  sig
src/stdlib/std/prl/taskflow.h:689  container
src/stdlib/std/prl/taskflow.h:693  sig
src/stdlib/std/prl/taskflow.h:697  sig
src/stdlib/std/prl/taskflow.h:700  sig
src/stdlib/std/prl/taskflow.h:706  sig
src/stdlib/std/prl/taskflow.h:709  sig
src/stdlib/std/prl/taskflow.h:714  sig
src/stdlib/std/prl/taskflow.h:716  sig
src/stdlib/std/prl/taskflow.h:720  sig
src/stdlib/std/prl/taskflow.h:726  sig
src/stdlib/std/prl/taskflow.h:732  sig
src/stdlib/std/prl/taskflow.h:737  sig
src/stdlib/std/prl/taskflow.h:745  sig
src/stdlib/std/prl/taskflow.h:750  sig
src/stdlib/std/prl/taskflow.h:755  sig
src/stdlib/std/prl/taskflow.h:760  sig
src/stdlib/std/prl/taskflow.h:765  sig
src/stdlib/std/prl/taskflow.h:770  sig
src/stdlib/std/prl/taskflow.h:772  sig
src/stdlib/std/prl/taskflow.h:774  sig
src/stdlib/std/prl/taskflow.h:776  sig
src/stdlib/std/prl/taskflow.h:778  sig
src/stdlib/std/prl/taskflow.h:780  sig
src/stdlib/std/prl/taskflow.h:784  sig
src/stdlib/std/prl/tfdump.cpp:80  sig
src/stdlib/std/prl/tfdump.cpp:84  sig
src/stdlib/std/prl/tfdump.cpp:112  sig
src/tools/camel-cli/main.cpp:100  sig
src/tools/camel-cli/main.cpp:112  sig
src/tools/debugger/server.h:120  comment
src/tools/debugger/src/core/debug_core.cpp:161  sig
src/tools/debugger/src/core/debug_core.cpp:465  sig
src/tools/debugger/src/core/debug_core.cpp:475  sig
src/tools/debugger/src/core/debug_core.cpp:489  sig
src/tools/debugger/src/core/debug_core.cpp:507  sig
src/tools/debugger/src/core/debug_core.cpp:523  sig
src/tools/debugger/src/core/debug_core.cpp:542  sig
src/tools/debugger/src/core/debug_core.h:76  sig
src/tools/debugger/src/core/debug_core.h:85  sig
src/tools/debugger/src/core/debug_core.h:106  sig
src/tools/debugger/src/core/debug_core.h:112  sig
src/tools/debugger/src/core/debug_core.h:130  local-null
src/tools/debugger/src/core/debug_core.h:249  sig
src/tools/debugger/src/core/debug_core.h:251  sig
src/tools/debugger/src/core/debug_core.h:253  sig
src/tools/debugger/src/core/debug_core.h:254  sig
src/tools/debugger/src/core/frames.cpp:40  container
src/tools/debugger/src/core/frames.cpp:83  sig
src/tools/debugger/src/core/frames.cpp:102  sig
src/tools/debugger/src/core/pipeline.cpp:12  sig
src/tools/debugger/src/core/program_index.cpp:309  container
src/tools/debugger/src/core/program_index.cpp:316  container
src/tools/debugger/src/core/program_index.cpp:317  container
src/tools/debugger/src/core/program_index.cpp:343  sig
src/tools/debugger/src/core/program_index.cpp:351  sig
src/tools/debugger/src/core/program_index.cpp:378  sig
src/tools/debugger/src/core/program_index.cpp:397  sig
src/tools/debugger/src/core/program_index.cpp:405  comment
src/tools/debugger/src/core/program_index.cpp:408  container
src/tools/debugger/src/core/program_index.cpp:409  container
src/tools/debugger/src/core/program_index.cpp:488  container
src/tools/debugger/src/core/program_index.cpp:527  sig
src/tools/debugger/src/core/program_index.cpp:562  container
src/tools/debugger/src/core/program_index.cpp:563  container
src/tools/debugger/src/core/program_index.cpp:583  container
src/tools/debugger/src/core/program_index.cpp:769  sig
src/tools/debugger/src/core/program_index.h:111  local-null
src/tools/debugger/src/core/program_index.h:120  container
src/tools/debugger/src/core/program_index.h:214  sig
src/tools/debugger/src/core/program_index.h:216  sig
src/tools/debugger/src/core/program_index.h:231  sig
src/tools/debugger/src/core/program_index.h:233  container
src/tools/debugger/src/core/program_index.h:252  comment
src/tools/debugger/src/core/program_index.h:253  sig
src/tools/debugger/src/core/program_index.h:277  container
src/tools/debugger/src/core/program_index.h:281  container
src/tools/debugger/src/core/program_index.h:282  container
src/tools/debugger/src/core/program_index.h:290  sig
src/tools/debugger/src/core/program_index.h:309  container
src/tools/debugger/src/core/program_index.h:318  container
src/tools/debugger/src/core/program_index.h:319  container
src/tools/debugger/src/core/program_index.h:331  container
src/tools/debugger/src/run.cpp:45  comment
src/tools/debugger/src/run.cpp:53  sig
src/tools/debugger/src/state.h:81  comment
```
