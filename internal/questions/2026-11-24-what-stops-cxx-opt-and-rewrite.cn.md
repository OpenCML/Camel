# 视图答不出什么：`cxx/opt` 与 `rewrite` 停下来的地方，按能力归类

2026-11-24 ｜ 车道 `lane/3a-rewrite`（阶段 3a 的余量）｜ 对象：`kernel/graph/graph_view.h`

## 0. 这一页是什么，读数是哪两个

这一页记的是 **`GraphView` 的缺口**，不是 `cxx/opt`、`rewrite` 欠的活。派工的话：「读不了视图的不硬改，把『读不了什么、需要视图或会话补什么』写下来。」

**路径先核，因为这条派工给的路径有一半不存在：**

```
git ls-tree --name-only origin/develop src/stdlib/std/ src/kernel/
```

- `src/stdlib/std/cxx/opt` —— **存在**（`src/stdlib/std` 下有 `cxx`，`cxx` 下有 `opt`）。
- `src/kernel/rewrite` —— **不存在**。按原路径跑 `git grep` 是 `exit 1`、**零行输出**，那个零不是「已清零」，是路径没对上。真实路径是 **`src/stdlib/rewrite`**（`src/kernel/` 下只有 `abi core diagnostics execute facts graph include ops profile runtime utils`）。

判据命令（**路径是真的那两个**）：

```
git grep -cE '\bGCGraph\b|\bGraphDraft\b' <rev> -- src/stdlib/std/cxx/opt
git grep -cE '\bGCGraph\b|\bGraphDraft\b' <rev> -- src/stdlib/rewrite
```

| 读数 | `src/stdlib/std/cxx/opt` | `src/stdlib/rewrite` | 读在哪棵树上 |
|---|---|---|---|
| `origin/develop`（`e3c6e02933`） | **88** | **24** | `e3c6e02933` |
| 第一轮改后（`a03836c6cf`） | **66** | **23** | `a03836c6cf` |
| **第二轮改后** | **49** | **23** | 本条车道的第二笔提交，见 §6 |
| 差 | **−39** | **−1** | |

`src/stdlib/rewrite` 的 24 与 2026-11-23 那页记的 31 不是矛盾：`module.cpp` 那 26 已降到 19，是那页之后别的车道做的，本轮开工时的读数就是 24。

逐文件（第一轮的读数，树是 `a03836c6cf`；第二轮的 −17 全在 `inline/engine.cpp`，见 §6）：

| 文件 | 前 | 后 |
|---|---|---|
| `cxx/opt/inline/engine.cpp` | 57 | 39 |
| `cxx/opt/generic/rewrite.h` | 9 | 9 |
| `cxx/opt/generic/screen.cpp` | 8 | 8 |
| `cxx/opt/generic/rewrite.cpp` | 6 | 6 |
| `cxx/opt/defun/defunctionalize.cpp` | 5 | 3 |
| `cxx/opt/generic/generic.cpp` | 2 | **0**（文件已不在清单里） |
| `cxx/opt/generic/algebra.cpp` | 1 | 1 |
| `rewrite/fact.cpp` | 4 | 4 |
| `rewrite/module.cpp` | 19 | 18 |
| `rewrite/proof.h` | 1 | 1 |

## 1. 本轮降的 23 行，逐条是哪一类

三类，判据是「这一行写这个名字，是在**做什么**」，与 exec 账 §0 的四条口径同源（按行、整词、注释也数、数的是点名的处数）。

### 1.1 「读」（3 行）—— 这一行原来读图，现在读视图

| 位置 | 原来读什么 | 现在问视图的什么 | 为什么等价 |
|---|---|---|---|
| `cxx/opt/inline/engine.cpp:92` `originKeyOf` | `graph->nodeOrigin(ref)` | `view.originOf(ref)` | 同一个存储字段，视图的映射表逐条写着 `nodeOrigin → originOf`（`gc_graph_view.h` §表）。源码位置**不是载荷**，是形状的一面。 |
| `cxx/opt/inline/engine.cpp:272` `returnsClosure` | `graph->funcType()` | `view.funcType()` | 同表：`GCGraph::funcType` 是图自己的类型，视图答它。函数体只有这一处读，所以整条签名改成收 `const camel::graph::Graph *`（**不是** `GraphView &`），调用点一字未改——它们传的 `GCGraph *` 向上转。 |
| `rewrite/module.cpp:464` `isRuleSide` | `graph.name()`（`const std::string &`） | `view.view().name()`（`std::string_view`） | 同表：图的名字是形状。类型跟着从 `const std::string &` 改成 `std::string_view`，`ends_with` 照旧。3 个调用点 `isRuleSide(*graph)` 一字未改。 |

**为什么这三行是「读」而不是「写」**：它们只取值，不落到任何一张图上，不产生草稿、不提交、不开会话。改前改后**同一个输入得同一个答案**：`originOf` 与 `nodeOrigin` 都读 `chunk.origins[i]`；`funcType` 两边读同一个字段；`name()` 两边读草稿/图的名字字段。

### 1.2 「身份」（19 行）—— 图在这里是*指针*，不是*面*

这些行不读图的内容，只把「哪一张图」传下去或当键。它们改的是**用什么类型写下这个指针**，不是读法。两种：

**(a) 借来的指针改 `auto *`**（4 行，`cxx/opt`）

| 位置 | 改前 → 改后 |
|---|---|
| `engine.cpp:289` | `GCGraph *callee = draft.calleeGraphOf(id)` → `auto *callee` |
| `engine.cpp:296` | `GCGraph *callee = session.work().calleeOf(...)` → `auto *callee` |
| `engine.cpp:1061` | `GCGraph *const calleeGraph = draft.calleeGraphOf(id)` → `auto *const calleeGraph` |
| `defunctionalize.cpp:965`、`:1050` | `const GCGraph *callee = program->calleeOf(...)` → `const auto *callee` |
| `generic.cpp:1998` | `for (camel::runtime::GCGraph *graph : session.collectReachableRuntimeGraphs())` → `for (auto *graph : ...)` |

`auto` 推出的是同一个类型，语义零变化；它只是不再**宣告**所有权与表示。这与 exec 账里 76 处的主干是同一个动作（`cpp` 的 33 处、`prl` 的 20 处都是这一步）。

**(b) 只当身份用的指针，换成目标身份 `const camel::graph::Graph *`**（14 行）

`camel::graph::Graph` 是 3a 已经落地的身份类型（`kernel/graph/include/camel/graph/graph.h`：一个虚析构、一个 `view()`、没有别的），`GCGraph` 与 `GraphDraft` 都从它派生。所以「只看指针值、从不读内容」的地方可以写这个类型，**调用点不用改**（`GCGraph *` 向上转），计数下降 —— 这正是 exec 账 §3 里 `nodeOriginNear` 那一步（改收 `const camel::graph::Graph *`）。

`inline/engine.cpp` 里 `CallBook` 是一个**纯粹的账本**：`index_` 按图指针做键，`calls`/`counted`/`stampOf`/`examined`/`slotOf`/`forget` 全部只查表、只比较指针，没有一处读图的内容。所以整本账连它的成员一起换身份类型：

`:700 calls`、`:705 counted`、`:714 forget`、`:730 stampOf`、`:736 examined`、`:743 unchangedExamination`、`:768 slotOf`、`:784 replaceHeld`、`:810 index_`（9 行）。

同一条还带出 `MacroOperandGraphs`（`:362` 的集合、`:369` 的 `contains`，2 行）与 `directCallees` 的返回类型和局部（`:279`、`:281`，2 行）。

**这 14 行是「身份」不是「读」，也不属于「写」**：`unordered_map<const Graph *, …>` 与 `unordered_map<const GCGraph *, …>` 存放的指针值逐位相同，hash 同一个指针，去重同一个集合；没有任何一条答案会变。

**没有做的事**：`recount`（`:718`）与 `rewritten`（`:728`）**留着** `const GCGraph *`，因为它们调 `directCallees(session_, graph)`，而那个函数真的读图（`graph->nodes()`、`session.work().calleeOf(*graph, …)`）。把它们的参数也换掉，就得给 `directCallees` 塞一个向下转，那是把签名写成假话。

### 1.3 「名字引入」（1 行）—— 顺带删掉的、因此无用的 `using`

`generic/generic.cpp:53` `using camel::runtime::GCGraph;`。同一文件里唯一的另一处（`:1998`）改 `auto *` 之后，这一行再无所指，所以删掉。exec 账里 `cpp` 的 −2 也是这个动作。

## 2. 停下来的地方，按能力

「位置」是**一处要该能力的地方**（一个函数记一处，即使它有若干个调用点）。A–H 沿用 `onnx`/`tiles` 那一页的编号，**I、J、K 是这一页新提的三条**。

| 类 | 缺的能力 | `cxx/opt` | `rewrite` | 本轮能迁吗 | 谁在补 |
|---|---|---|---|---|---|
| **A** | 节点与区域的属性（`attrsOf`、区域 tag set、权限） | 1（`mayMerge` 的 `tag::permissionsOf`） | 0 | 不能 | `lane/view-gaps` |
| **B** | 载荷字节 / `hasNodePayload` | 4（`constantValue`、`isPureOperNode`、`mayMerge`、`mayRemoveDead`） | 1（`axisPortsOf` 的 `uri()`） | 不能 | `lane/view-gaps` |
| **C** | 控制边 `ctrlInputsOf` / `predecessors(Wait)` | 2（`mayRemoveDead`、`mayReduceOrder`） | 0 | 不能 | `lane/view-gaps` |
| **D** | 静态区的**槽**与整片读取（`staticSlots`/`staticArea`/`staticDataType`） | 3（`constantValue`、`staticFunctionOf`、`functionConstantGraph`） | 2（`compilePattern`、`declRefsOf`） | 不能 | **没人** |
| **E** | `Program` 侧收 `const Graph *`（`calleeOf`/`graphs()`/`collectReachableRuntimeGraphs`/`tryDraft`） | 4（`directCallees`、`defunctionalize` 两处、`generic` 的闭包循环） | 0 | 不能 | **没人**（视图侧无解） |
| **F** | 写侧：`GraphDraft` / `GraphTransaction` 的编辑入口 | 3（`writeWrapperGraph`、`inSplicedRegion`、`leftCallOrigin` 的 draft 形参） | 5（`module.cpp` 的 4 处 `const_cast` + `subject`） | 不能 | 3b |
| **G** | 反向 `uses()`、全表 `syncEdges()` | 0 | 0 | —— | 本轮不涉及 |
| **H** | 原始 16 种 kind、`GCNode::flags`/`typeId` | 5（`screen.cpp` 五个 screen 的整表枚举） | 3（`stepsOf`、`declaredUlpsOf`、`compilePattern`） | 不能 | 3b |
| **I** | **`GraphId`**（`Graph::id()`） | 1（`variantOf`） | 1（`leftCallOrigin` 的 `callee->id()`） | 不能 | **没人**（本页新提） |
| **J** | **区域是不是内联拼进来的**（`GCRegionKind::Inline`） | 1（`nodeCameFromInlineSplice`） | 0 | 不能 | **没人**（本页新提） |
| **K** | **含 JOIN 的整表枚举**（`nodes()` 跳过 JOIN/DREF/Dead） | 2（`inlinedNodeCount`、`inlinedComputeNodeCount`） | 1（`mayMerge` 第一趟显式跳 JOIN） | 不能 | **没人**（本页新提） |

**一句话答**：两个数里 `cxx/opt` 的主干落在 **H 与 K**（`screen.cpp` 整本、`engine.cpp` 的六个尺寸/枚举函数），`rewrite` 的主干落在 **F 与 H**（写侧 5 处、按编号遍历 operator 记录 3 处）。A/B/C 补完之后这两个数**不会**同时归零：D/E/F 各挡一段，而 E、I 视图本身答不了。

### 2.1 I — `GraphId`：视图明说自己不答

`variantOf(TypePlan &plan, const GCGraph &target)`（`defunctionalize.cpp:281`）除了读 `name()`、`closureNodes()`、`node(capture)->dataType()` —— 这三样视图都答 —— 还要 `target.id()` 与 `plan.variants` 的键做匹配。图的 id 正是视图声明不答的那一样（`graph_view.h` 的「四样读法离开这张面」一节点名讲了 `GraphId` 与 `debugMap().graphOrigin`）。`leftCallOrigin`（`engine.cpp:679`）同样。

要迁它，要么视图答 `GraphId`（那等于承认图有一个编号身份，与 2.3「图的身份是值」直接冲突），要么 `TypePlan::variants` 改用 `const Graph *` 做键 —— 后者是 3c 的引用模型，不是 3a。

### 2.2 J — 「这块区域是内联拼进来的」视图答不了

`nodeCameFromInlineSplice`（`engine.cpp:76`）问的是 `graph->region(id)->kind == GCRegionKind::Inline`。视图的 `RegionKind` **故意没有 `Inline`**：`graph_view.h` 写着「an inlined body is an ordinary region of this graph」。这不是视图漏了，是它按设计不答——内联的来处是编译器的记账，不是图的形状。

所以这个函数停在这里，而它下面的 `inlinedComputeNodeCount` 也因此整条不能迁。要迁它得先有人裁定「内联出处」算不算图的一面（草稿侧已经有 `draft.inlinedCopyOf(id, callee)`，冻结侧没有对应物）。

### 2.3 K — `nodes()` 的集合比 `nodeCount()` 少，所以整表计数不能从视图读

这一条是**与 `onnx` 那页 E1 同一类、但后果更硬**的一条，值得单独说，因为它是**静默差一个数**：

- 旧 `GCGraph::nodes()` 枚举整张表，只跳 `Dead`；**JOIN 有节点记录、会被枚举到**。
- 视图 `nodes()` 枚举 `contains()` 为真的槽，而 `contains` 对 **JOIN、DREF、Dead 都答 false**。

于是「数一数这张图有多少个节点」这个问题，从视图读会**少掉每一个 JOIN**，而且没有任何断言会响。本轮因此**没有**迁这两处：

- `inlinedNodeCount`（`engine.cpp:189`）—— 它数「除 DATA 与 PORT 之外的一切」，JOIN 会算进去，视图读法不算。它喂增长预算与 owner 上限（`growthSpent + copied > growthBudget`），少一个数就是预算多一份。
- `inlinedComputeNodeCount`（`engine.cpp:129`）—— 它自己有 `switch (node->kind)` 跳过 DATA/PORT/GATE/DEAD，JOIN 走 `default` 被计一次；视图读法同样会少算。

`mayMerge`（`screen.cpp:170-174`）是这个差的**活证据**：它的第一趟显式写着 `if ((*it)->kind == GCNodeKind::Join) continue;` —— 只有 JOIN 真在枚举里，这一行才有意义。谁要把这几个 screen 迁到视图，得先回答「JOIN 在不在你要数的集合里」。

## 3. 第一轮之后剩下的 89 行，逐处停在哪

**本节读的树是 `a03836c6cf`**（第一轮之后、第二轮之前）：那时的读数是 66 + 23。第二轮的 17 行从 `engine.cpp` 里又移走了 17 行（39 → 22），见 §6；本节列的 39 行里，§6 点名的那 17 行已经不在树上了。

按函数归组（同一函数的形参、声明、调用点算同一处的多条），每条给类。

### `cxx/opt/inline/engine.cpp` —— 剩 39

| 行 | 是什么 | 类 |
|---|---|---|
| `:5` | 文件头注释，主语是「GCGraph-native 的 pass」 | 注释 |
| `:47` | `using`（下面 38 行都要它） | 名字引入 |
| `:76` | `nodeCameFromInlineSplice` 形参 | J |
| `:129` `:180` | `inlinedComputeNodeCount` 与它的调用方 | K + B |
| `:189` | `inlinedNodeCount` 形参 | K |
| `:204` `:205` `:250` `:251` | `bodyNeedsOrder` 的形参、`seen` 集合、对外契约 | B + E + 容器 |
| `:285` | `directCallees` 形参（`tryDraft`/`calleeOf` 收表示） | E |
| `:381` | `functionConstantGraph`，读 `staticArea()` 并**返回**一张图 | D + 身份 |
| `:400` `:492` | 两个函数的 `closure` 形参（来自 `collectReachableRuntimeGraphs()`） | E |
| `:403` | `std::vector<const GCGraph *> work;` 无初值 | 容器 |
| `:435` `:595` `:880` | lambda 形参。写成 `auto *` 会把具名 lambda 变成泛型 lambda，是另一件事（exec 账 §2） | lambda 形参 |
| `:482`–`:488` `:494` | SCC 的六张表与 `indexOf`；值喂给读载荷的判定 | 容器 + 键 |
| `:626` | `shouldSkipRecursiveInline` 的两个形参 | 见 §3.1 |
| `:658` `:659` | `DeclinedCall::caller`/`callee`；换类型会撞进公共头 `camel/passes/opt/inline.h` | 声明 |
| `:678` | `leftCallOrigin` 的 `GraphDraft` 形参 | F |
| `:679` | 同一函数的 `callee`，读 `callee->id()` | I |
| `:718` `:728` | `recount`/`rewritten`（调 `directCallees`，真读） | E |
| `:826` | `inSplicedRegion(const GraphDraft &)` | F |
| `:851` | `const GCGraph **lastChangedOut`，出参必须写出类型 | 声明 |
| `:864` `:882` | `PointerSet<GCGraph>` | 容器 |
| `:865` `:872` | 闭包向量，逐个要读 | 容器 + E |
| `:1467` | `const GCGraph *lastChangedGraph = nullptr;`，`auto` 推不出 | 空指针初始化 |

`canonical`：`defun/defunctionalize.cpp` 剩 3（`:179` D+H、`:281` I、`:652` F）。

### `cxx/opt/generic/screen.cpp` —— 剩 8

五个 screen（`mayFold` `:89`、`mayMerge` `:158`、`mayRemoveDead` `:213`、`mayReduceOrder` `:288`）加三个助手（`constantValue` `:46`、`isConstantLike` `:74`、`isPureOperNode` `:81`）与 `:26` 的 `using`。停的原因合起来是 **A+B+C+D+H+K**，没有一条只是「没人去改」。

**`isPureOperNode` 是这里唯一有视图对应物的一处，也是唯一一处我判它「先不改」**：

- 旧：`node->kind == GCNodeKind::Oper && node->hasPayload() && OperatorTable::mayBeRecomputed(graph.nodeBodyAs<GCOperBody>(ref)->oper)`
- 视图能给的：`isOperRecord(ref) && mayBeRecomputed(operatorOf(ref))`，而 `isOperRecord` = `kindOf == Oper && operatorOf != kNoOper`

两者只在一个输入上分岔：**一个存储的 OPER 节点，`GCOperBody.oper` 恰好是 `kNoOper`**。这时旧的会问 `mayBeRecomputed(kNoOper)`，新的直接答 false。`mayRecomputed(kNoOper)` 是不是 true 没有断言钉住；而一个 OPER 节点带 `kNoOper` 在今天的构建里要么不发生、要么是真缺陷。**这一处要一行：谁确认「存储的 OPER 的 oper 永不为 kNoOper」，这一行就能迁。**

**顺带核过、没有缺陷**：我原以为 `mayMerge` 在 `:193` 对每个节点调 `isPureOperNode`，会让 CAST/COPY/FILL/ACCS 的载荷被当成 `GCOperBody` 读（`graph_view.h` 记着这是一条真的误读）。**不成立**：那四种在存储里是**自己的 kind**（`GCNodeKind::Fill` 等），`node->kind == GCNodeKind::Oper` 先短路了。折叠成 `Oper` 是**视图**做的事，不是存储。所以 `isPureOperNode` 在旧面上没有误读。

### `cxx/opt/generic/rewrite.{h,cpp}` —— 剩 15

`contentHash`（`rewrite.cpp:654`）是整条链的锚：它 hash 的是**存储本身的字节** —— `nodePayload()->chunks[]`、`chunk.origins`/`permissions`/`attrs`/`regions`/`edges`/`payload`、`regionTable`、`writings`、`normPorts`、`withPorts`、`closure`，外加 `staticSlots()` 与四个类型的 canonical id。这不是「读得不够视图」，是**它要的东西按定义就不在视图上**：视图交的是形状，memo 要的是「同样的字节」，两者是不同的问句。`leftAlone`/`remember`/`graphContentHash` 只是把它传下去（`:734` `:740` `:746` 与 `.h:296` `:297` `:311`）。

另外 9 行是五个 screen 的**声明**（`.h:319`–`:323`）与函数指针 `bool (*mayRewrite)(const camel::runtime::GCGraph &)`（`rewrite.cpp:752`、`.h:338`）。**这个指针类型是本目录最大的一处卡口**：它一改，五个 screen 必须一起改；而四个 screen 的体内读 A/B/C/D，迁不动；`mayAlgebra`（`algebra.cpp:269`）的形参甚至无名、函数体直接 `return true`（它唯一的障碍就是必须匹配这个指针类型）。所以那 1 行也留着。

### `src/stdlib/rewrite` —— 剩 23

| 位置 | 是什么 | 类 |
|---|---|---|
| `fact.cpp:75` | `using`（下面 3 行要它） | 名字引入 |
| `fact.cpp:175` | `Source::graph` 成员，`= nullptr` | 空指针初始化 |
| `fact.cpp:567` | `compilePattern` 形参：读 `GCNodeKind`、`dataIndex`、`staticSlots()`、`kernelArgumentsOf` | D + H |
| `fact.cpp:693` | `const_cast<GCGraph *>` 交给 `resolveRuntimeTailValueRef`（VM 按值槽写） | F |
| `module.cpp:251` | `WhenCall::graph` 成员，`= nullptr` | 空指针初始化 |
| `module.cpp:419` | 条件图交 node VM，`const_cast` | F |
| `module.cpp:1036` `:1038` 区 | `applyRewrite` 的 `subject` 与 `session.edit(subject)` | F（**写入口已在**） |
| `module.cpp:1605` `:1669` `:4180` `:4181` | 四处 `const_cast<GCGraph *>(programNow()->graph(id))`，紧接着 `session.edit(...)` | F（**写入口已在**） |
| `module.cpp:2647` `:2921` | `RefEvaluator` 的构造形参与成员：整条求值路径读 `node()`/`normInputsOf`/`nodeBodyAs` | F + B |
| `module.cpp:3016` `:3022` `:3042` `:3627` `:3680` `:3681` | 六个比较/证明函数的形参，全是 `RefEvaluator` 的载体 | D + B |
| `module.cpp:3830` `:3839` | `stepsOf`/`declaredUlpsOf`，按编号遍历 operator 记录 | H |
| `module.cpp:3888` | `axisPortsOf` | H + B |
| `proof.h:53` | 注释，主语是 `GraphDraft::decode` 的节点 id | 注释 |

**写图的入口这一条，答案是好消息**：`session.edit(...)` 就是那个入口，它已经到位；上面 5 处 `const_cast` 不是「入口缺失」，是**入口收 `GCGraph *` 而不是 `const Graph *`**，所以把一个 `const` 的图交进去必须 cast。要让这 5 行消失，改的是 `RuntimeGraphDraftSession::edit` 的形参 —— 属 3b 的编辑原语，本轮不动。

## 4. 谁认领

| 缺的 | 谁 |
|---|---|
| **A**（属性）、**B**（载荷）、**C**（控制边） | `lane/view-gaps`，在补 |
| **D**（静态区的槽与整片读取） | **没人**（与 `onnx` 页同一条） |
| **E**（`Program` 收 `const Graph *`） | **没人**；视图侧无解 |
| **F**（写侧编辑入口收 `const Graph *`） | 3b |
| **H**（原始 kind、`flags`、`typeId`；整表枚举里的 JOIN） | 3b |
| **I**（`GraphId`） | **没人**；本页新提，且与 2.3「身份是值」冲突，要先裁 |
| **J**（区域的内联出处） | **没人**；本页新提 |
| **K**（视图的 `nodes()` 比 `nodeCount()` 少 JOIN） | **没人**；本页新提。**顺序上它该在 H 之前**：任何按整表数数的读者都会静默少数 |

**默认建议**：这一侧希望先做 **K**，其次 **D**。K 一条挡 `cxx/opt` 的 2 处、`rewrite` 的 1 处，更要紧的是它**不会报错**——少数的那个数会一路进增长预算。D 挡 3 处且是 `constantValue` 与 `staticFunctionOf` 共同的门槛，`constantValue` 又是 `mayFold`/`mayMerge` 的下游；`onnx` 页已经把 D 记成「本该关掉、其实没关」的一条。

## 5. 没读到的，明说

- **`src/kernel/rewrite` 从来没有读过**——它不存在。本页所有 `rewrite` 的话都是关于 `src/stdlib/rewrite`。
- **`shouldSkipRecursiveInline` 第二轮已迁**（第一轮留着没做）。它连同 `RuntimeCallGraphSccInfo` 的六张表、`analyzeRuntimeCallGraphScc` 的 `indexOf`、`:595` 的 lambda 一起换成了 `const camel::graph::Graph *`，一共 9 行，见 §6。它确实是本页说的「最干净的一笔」。
- `proof.h:53` 那条注释只核了它说的是 `GraphDraft::decode` 的节点 id，没有追 `decode` 本身。
- 本轮**没有**读 `generic/algebra.cpp` 里 `mayAlgebra` 之外的部分，也没有读 `defunctionalize.cpp` 的 `:652` `writeWrapperGraph` 的函数体（只按返回值判它是写）。

## 6. 第二轮：17 行，全是身份（2026-11-24）

第一轮之后 `cxx/opt` 的 66 行里，`inline/engine.cpp` 独占 39。第二轮从这一个文件里移走 17 行，两个目录的读数变成 **49** 和 **23**（命令与 §0 相同，`--rev` 指本车道的第二笔提交）。

**这 17 行的类**：17 行全是**身份**，没有一行是「读」的内容迁移，也没有「写」和「注释」——`cxx/opt` 剩下的读侧门槛（A/B/C/D/H/K）一条都没动，因为它们不在本车道。三组：

1. **`RuntimeCallGraphSccInfo` 与建表（9 行）**。六张表（`componentOf`、`components`、`recursiveGraphs`、`componentEntryGraphs`、`externallyCalledGraphs`、`reachesRecursion`）、`analyzeRuntimeCallGraphScc` 里的 `indexOf`、`:595` 的 lambda 形参、`shouldSkipRecursiveInline` 的两个形参。它们的每一次使用都只是 `insert`/`contains`/`find`/`size()` 与指针比较，没有一处读图的内容；`GCGraph *` 向上转成 `const Graph *` 之后存放的指针值与 hash 都还是那一个（`GCGraph : public Object, public camel::graph::Graph`，单次派生到基类的地址是确定的）。
2. **`DeclinedCall::caller`/`callee`（2 行）**。这两个成员在记录做出来时只取**名字**，所以类型换成身份之后，`applyRuntimeOptimizeRewrite` 末尾那两行就从 `call.caller->name()` 变成 `call.caller->view().name()`，名字改由视图答（`graph_view.h` 的映射表：`GCGraph::name() → name()`，答案是 `std::string_view`，进 `InlineDeclined` 的 `std::string` 要显式构造）。
3. **闭包集合与「最后改过的那张图」（6 行）**。`closureSet`、`members`（`PointerSet`）、`previousClosure`、`:880` 的 lambda 形参、`applyRuntimeOptimizeRewrite` 的 `lastChangedOut` 与 `lastChangedGraph`。前四个只用来 `contains`/`size` 与 `forget`；后两个只在「没收敛」那条报告里被读名字，四处 `->name()` 一并改成 `->view().name()`。`previousClosure = closure` 随之写成 `assign(closure.begin(), closure.end())`，因为两个 `std::vector` 的元素类型不再是同一个。

**为什么不更远**：`rewrite` 的 23 行一行没动，且这一轮核过、确认它们没有一行属于身份类——`fact.cpp` 的 `Source::graph` 与 `module.cpp` 的 `WhenCall::graph` 都既被当身份用（比较、进 `Anchor`）**又**被读内容（`graph->node(...)`、`staticSlots()`、经 `const_cast` 交给 node VM），把成员类型换掉只会在读侧塞进向下转；剩下的是 D/B/H 与 3b 的写入口。

**这一轮的读数是干净的**：`test/plans/feat/opt` 全目录在 `a03836c6cf` 上是 `total: 1636, pass: 1495, fail: 141`。带这一笔改动重跑（`feat/opt` 全目录加 `smoke`）是 `total: 1646, pass: 1505, fail: 141`；`smoke` 那 10 条全过，所以 `feat/opt` 那部分仍是 1636 / 1495 / 141，红的 141 条逐 plan 分布也逐条相同（`gate_value` 60、`splice_place` 29、`camel_passes` 4、`pass_params` 4、`algebra` 2、`writings` 2、`commit_refusal` 1、`effects_kept` 1、`opt_second_pass` 1、`rounds_warning` 1）。这 141 条全部是这棵落后 `origin/develop` 二十一笔的树自己的红：`stdlib/random.cml` 的 `to_array` 未解析、`stdlib/perf.cml` 的 `<:mark/reduce_arr>` 无匹配、`test/cases/opt/writings` 找不到 stdlib、golden 里的节点号差一。没有一条落在这一笔改动能触及的地方。