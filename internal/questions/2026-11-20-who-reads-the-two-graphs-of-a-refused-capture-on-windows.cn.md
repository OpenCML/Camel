# Windows 上那次「捕获被拒」：三个图的名字，只有一台能构建 Windows 的机器读得到

**日期**：2026-11-20
**提出者**：车道 S1-6（`lane-win-s1-6`，基线 `origin/develop` = `c75611862`，工作树 `D:\Projects\Camel-wt\wins1`）
**状态**：未修。读数已从「6 次里 2 次」变成 N 次里 M 次（下表），但**判它要的那条读数（三个图叫什么名字）本车道拿不到**，
因为本机禁止构建、远端主机只构建 Linux，而这条缺陷在 Linux 上 60 次里 0 次。
**触发**：`internal/reviews/2026-11-19-the-0.2.45-blocker-list-by-severity.cn.md:22` 的 S1-6。

## 1. 问题（一句）

`src/compile/gir/captures.cpp:63` 的 `graph->outerGraph() != nullptr` 在 Windows 上大约每三次响一次，
而它响的时候**是哪两张图**——被捕获的节点所在的图、以及捕获方所在的图——只有一条 Windows 读数能给出；
本车道用哪条通道换这条读数？

## 2. 让它是问句的读数（全部是本机逐次跑出来的，不是推断）

复现命令（作者原话，同一棵树的一份 11-12 的 Windows Release 构建，`out\latest\bin\camel.exe`，
`CAMEL_HOME=out\latest`，源文件取 `c75611862` 的 `test/cases/compile/stream/generators_shapes_consumed.cml`）：

```
camel.exe test\cases\compile\stream\generators_shapes_consumed.cml std::nvm
```

| 读数 | 失败 | 逐次退出码 |
| --- | --- | --- |
| `std::nvm`，空载，第 1 批 20 次 | **6** | `3,0,3,0,3,0,0,0,3,0,0,0,0,3,0,0,0,3,0,0` |
| `std::nvm`，空载，第 2 批 20 次 | **3** | `0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,0,3,3,0,0` |
| `std::nvm`，空载，第 3 批 20 次（负载撤掉之后） | **4** | `0,0,0,3,3,0,0,0,0,3,0,0,0,3,0,0,0,0,0,0` |
| **`std::nvm` 空载合计** | **13/60 = 22%** | |
| `std::fvm`，空载 20 次 | **5** | `3,3,0,3,0,0,0,3,0,0,0,3,0,0,0,0,0,0,0,0` |
| `std::jit`，空载 20 次 | **2** | `0,0,0,0,0,0,3,0,0,0,0,0,0,0,0,3,0,0,0,0` |
| `std::prl`，空载 20 次 | **6** | `0,3,3,0,0,0,0,3,0,0,3,0,3,0,0,0,0,3,0,0` |
| **四后端空载合计** | **16/80 = 20%** | |
| `std::nvm`，**满载**（28 核上 26 个占满，`LoadPercentage` = 100%）20 次 | **7/20 = 35%** | `3,3,3,0,0,0,0,3,3,0,3,0,0,3,0,0,0,0,0,0` |

**与负载有关**：满载 35% > 空载 22%，同一份二进制、同一条命令。作者在 CI 上读到的「四个后端全红」，
按空载 20% 算概率只有 0.16%，按满载算也只有 1.5% —— **⇒ CI 那一轮的实际速率比本机满载还高**，
所以「1 次没复现」在这个缺陷上没有任何意义，反过来「四后端全红」也不能当成四条缺陷。

失败时的完整 stderr（`exit 3`，stdout 为空；只有最上面六行是话，其余是 Release 下认不出符号的栈）：

```
Camel Invariant Violation:
    Location   : D:/Projects/Camel/src/compile/gir/captures.cpp(63)
    Function   : node_handle_t __cdecl camel::compile::gir::Builder::captureInto(const compile_graph_ptr_t &, node_handle_t, const std::string &)
    Guarantee  : graph->outerGraph() != nullptr
    Reading    : A captured value must belong to a graph that encloses the capturing one.

Stack Trace:
<1> libcamel!camel::compile::gct::Tree::print+0x200D4A
<2> libcamel!camel::compile::gct::Tree::print+0x20003F
<3> libcamel!camel::compile::gct::Tree::print+0x20003F
<4> libcamel!camel::compile::gct::Tree::print+0x20003F
<5> libcamel!camel::compile::gct::Tree::print+0x9F183
...（<63><87> 起是 camel::compile::gir::FrozenGraphRead::hasPayload+0xF589，<90> 是
    camel::core::module::UserDefinedModule::compile+0x1127，其后是 camel.exe 自己的三个偏移）
```

栈里 `<2><3><4>` 是同一个偏移（`+0x20003F`）——**`captureInto` 自己递归了三层**，
即那条向上走的链在被判死之前已经爬过至少三层，最上面那层所在的图 `outerGraph()` 是空。

**Linux 对照（同一棵树，`c75611862`，主机 `Release` 构建，`camel-remote.ps1`）**：
把上面那条命令在计划里重复 20 次，另把下面 §2 的两个形状各重复 20 次，一轮跑完：

```
[SUMMARY] total: 60, pass: 60, fail: 0
```

⇒ **Linux 上 60 次 0 次**。这不是「Windows 更容易撞上」，是这条路径在 Linux 构建上根本没走到。

### 2.1 把「哪个程序形状」也二分掉了（同一份 Windows 二进制，`std::nvm`，各 20 次）

| 程序 | 失败 |
| --- | --- |
| 只留 `three`（三个携带值 `i,s,t`） | 0/20 |
| 只留 `nested`（两层嵌套循环） | 0/20 |
| 只留 `fib`（携带一对） | 0/20 |
| 最简一个携带值 | 0/20 |
| `three` + `nested` | **7/20** |
| `three` + `fib` | **5/20** |
| `nested` + `fib`（两个生成器，都不是三携带值的形状） | 0/20 |
| 两个**同签名**的三携带值生成器（`three`/`three2`） | **8/20** |
| 三携带值 + 同状态形状但界是字面量（不捕获任何外层名字） | **7/20** |
| 原用例（三个生成器） | 6/20、3/20、4/20、2/20 |

**形状不是结构条件，只是分配格局**：`nested`+`fib` 也是两个生成器，0/20；
「三携带值 + 另一个生成器」无论另一个是否捕获外层名字、签名是否相同，都是 35—40%。
一个三元素的携带状态把某个分配推到另一个尺寸档，之后就是地址落在哪里的事。
这与「满载比空载高一半」是同一条线索：**决定成败的是一个地址，不是程序的结构**。

## 3. 按代码能确定的那一半（这一半不需要 Windows 读数）

**① `graph->outerGraph()` 什么时候是空——逐字：**
`outerGraph()` 返回 `DraftGraphBuilder::outerGraph_`（`include/camel/compile/gir/draft_graph_builder.h:241`，
`std::shared_ptr<DraftGraphBuilder>`，类成员默认构造即为空）。**全树只有两个写者**：

- `createCompileGraph`（`src/compile/gir/builder_helpers.h:403-406`），且**只在 `outer` 非空时写**：
  `if (outer) { graph->setOuterGraph(outer); outer->addSubGraph(graph); }`；
- `src/compile/gir/builder_generics.cpp:2598`：`scratch->setOuterGraph(currGraph_)`，紧接在
  `createCompileGraph(..., nullptr, "__typeof__")`（`:2593`）之后一行。

而 `createCompileGraph` 的调用点只有五处（`builder.cpp:47`、`:332`、`:357`、`builder_generics.cpp:2471`、`:2593`），
除 `builder.cpp:47` 与 `:2593` 两处外，`outer` 实参都是 `currGraph_`；`builder.cpp:47` 那处是**字面量 `nullptr`**，
它造的就是 `__root__`，`:2593` 那处也是 `nullptr` 但下一行就补上了父图。
⇒ **`outerGraph()` 为空 ⟺ 这张图被造成时没有外层图 ⟺ 它只能是 `__root__`，或 `__typeof__` 那张草稿图在 `:2598` 之前的那一瞬。**
`captureInto` 判死时 `graph` 已经参与过 `nodeGraphOf(origin) != graph` 与 `capturedValues_` 两次读，
不在那一瞬里，所以**判死的那张图就是 `__root__`**。

**② 所以这句断言在说什么：** 它站在 `__root__` 上，而这条向上走的链已经从捕获方的图爬到这里，
一路上没有任何一张图是 `nodeGraphOf(origin)`。逐字：**被捕获的节点所在的图，既不是捕获方那张图，
也不在它到 `__root__` 的祖先链上**——它在捕获方那张图的**下面**（捕获方所包住的某张图里），或在另一条分支上。
向**上**走的这趟永远走不到它，于是走到 `__root__` 没得走，守卫响。

**③ 是位置错还是真违反：真违反，不是位置错。** 守卫问的那件事（「被捕获的值必须属于包住捕获方的那张图」）
正好是 `captureInto` 唯一有权做的事：它在每一层造一个捕获口，而口只能从外层拿值。
`origin` 只能从两张表来（`capturedValues_`、`captureOriginOf_`/`machinePortOrigin_`），两张表存的是
`node_handle_t`（`{draft, epoch, id}`），而 `DraftGraphBuilder::fromRef` 是按 **地址 + epoch** 判活的
（`draft_graph_builder.cpp:65-80`，`draftOwners()` 键是 `&draft()`，析构里 `erase`），
所以**它答出来的图就是那个句柄真的所属的图**，不是悬垂读出来的。
⇒ 判死说明「某张表把一张图里的节点，当成另一张图能捕获的值交了出去」，错在**记录那一步**，不在守卫。
（`internal/reports/2026-11-15-is-the-stream-family-load-dependent.cn.md` §5 把这一处列成
`nodeIsCurrent`/`nodeDraftOf` 那一族的「同一形状」——**那一族的推理在这里不成立**：`graph` 是 `shared_ptr`，
图与图之间还有 `outerGraph_`/`subGraphs_` 的双向 `shared_ptr`，所以建图器不可能先死；上面 ① 已经逐字排除。）

**④ 顺手排掉的三条（都是逐字查过的，免得下一轮再走一遍）：**

- **不是平台条件编译**：`src/compile/**` 与 `include/camel/compile/**`、`include/camel/runtime/{draft,graph_txn}.h`
  里 `_WIN32` / `_MSC_VER` / `__GNUC__` **零命中**（全树 79 处命中全在建图之后的执行侧：`render`、`poison`、
  `dynamic`、`limits`、`x64_*`、`mir_encode`、`fastvm`、`gotovm`）。⇒ 两边的开关注释掉的是同一段前端代码。
- **不是「同一张草稿被两个建图器登记」**：`draftOwners()` 的键是 `&draft()`，登记值带 epoch
  （`draft_graph_builder.cpp:39-80`）。`GraphTransaction::borrow` 确实让两个事务指一张草稿
  （`src/runtime/draft_session.cpp:858` 的注释自己写着），但**没有任何 `DraftGraphBuilder` 建在借来的事务上**：
  `make_shared<DraftGraphBuilder>` 全树两处（`builder_helpers.h:397`、`src/runtime/draft_node_owner.cpp:45`），
  两处都用 `GraphTransaction::open`。⇒ 一个草稿只有一个登记，`nodeGraphOf` 不会答错到另一张图。
- **不是草稿会搬家**：`GraphTransaction::graph_` 是 `std::unique_ptr<GraphDraft>`（`graph_txn.h:52-55`），
  地址在事务活着期间不变 ⇒ 上面那个键不会失配。
- **不是「epoch 在地址复用时没递增」**（发版侧 2026-11-20 提的那条假设，逐字推翻）：epoch **不是节点级的，是草稿级的**，
  而且是在**造草稿的时候**取的：`draft.h:1275` `const uint64_t epoch_ = nextDraftEpoch();`（`const`，此后从不改），
  `nextDraftEpoch()` 是**进程级原子自增**（`src/runtime/draft.cpp:36-42`：`static std::atomic<uint64_t> counter{0};
  return counter.fetch_add(1, …) + 1;`，注释自己写着「0 留给不指名草稿的句柄」）。
  ⇒ **同一进程里两张草稿不可能同 epoch**，所以「地址被复用 + epoch 相同」这个局面造不出来；
  草稿内节点 id 也不复用（`draft.h:1282-1283`「ids are not reused」）。
  ⇒ 发版侧那句「它没说谎，是你问错了对象」**完全正确，但错的对象不是死句柄**：
  被问的句柄是**活的、epoch 对得上、`nodeGraphOf` 答的就是它真正的图** —— 也就是说，
  **记录的那一刻就已经在问一张捕获方包不住的图**，不是事后地址复用造成的。

⇒ 留下的是**记录那一步**：某张以图地址或节点句柄为键的表，把一张图里的节点当成另一张图能捕获的值交了出去。
十几张表都以 `DraftGraphBuilder*` 为键（`builder.h:205/220/255/561/605/661/665/667/765/769/1344/1345/1381/1388/1405/1409` 一带），
其中 `__typeof__` 那张草稿图析构时**只清了 `capturedValues_` 与 `valueUsedGraphs_` 两张**
（`builder_generics.cpp:2649-2652` 的注释说「以图地址为键的那些条目随图一起走」，而实际只走两张），
其余十几张仍在用同一个地址——**这是「最像」的候选，不是判定的结论**；判它要的就是 §1 那条读数。

**⑤ 22% 这个比率是哪来的（发版侧问的，逐条查过）：**

- **不是线程竞争**：`src/compile`、`src/core/module`、`src/core/sem`、`include/camel/{compile,core/module,core/sem}`
  里 `std::thread` / `std::async` / `ThreadPool` / `parallel_for` / `jthread` **零命中** ⇒ 编译期是单线程的，
  22% 不是竞态窗口。**但满载（35%）比空载（22%）高**——只能是堆布局被外部扰动，不是调度。
- **不是 `capturedUsesOf` 的迭代顺序**：它读的 `captures_[callee].variables` 是 `std::vector<node_handle_t>`
  （`builder.h` 的 `CaptureSummary`），按捕获发生的先后 push_back，**不是哈希容器** ⇒ 那条路按记录序，与地址无关。
- **但地址确实是这套表的身份**：`std::hash<DraftNodeRef>` 逐字是
  `hash<const void*>(ref.draft) * 31u + ref.id`（`include/camel/runtime/node_ref.h:60-63`），
  `operator<=>` 也先比 `draft` 地址（`:46-55`）；`node_set_t` 就是 `std::unordered_set<node_handle_t>`（`types.h:54`）。
  ⇒ **凡是 `unordered_{map,set}<node_handle_t>` 的遍历，次序都随这张草稿这一次落在哪个地址而变**，
  而这张草稿的地址每次进程都不同（ASLR + 堆格局，满载时被别的进程扰动）——**这就给 22% 提供了唯一的来源**。
  只是**还没找到哪一处遍历用这个次序做了会改变图结构的决定**（`capturePath` 上被遍历的几处：
  `builder_control.cpp:572/1884/2140` 读的是 `touchedOrder`，那是 `std::vector`、按触碰序；
  `builder_control.cpp:2080/3325/3763` 的 `capturedWrites` 只喂诊断文本；`builder_resources.cpp:927` 累加进有序集）：
  ⇒ 这一条仍开着，等三个图名把它指出来。

## 4. 三条路与代价

| 路 | 代价 |
| --- | --- |
| **A. 作者在一台能构建 Windows 的机器上构建 head，跑一次那条命令（或几次直到响），把六行贴回来** | 这条消息 `8b0a96a70`（11-19）已经改过了：它现在打印 `node {} of graph '{}'`、`currGraph_`、和「走到的图」。**一次响就够**——三个名字直接指出是哪张表记错了。代价是作者那台机器要构建一次；本车道不能做（本机构建会蓝屏，远端主机只构建 Linux） |
| **B. `workflow_dispatch` 在 `lane-win-s1-6` 上跑一次 CI 的 win-x64 腿** | 不需要任何本机构建，也不需要新授权：CI 的 `ci.yml` 只由 `release` / tag / 手动 dispatch 触发，**推车道分支不会触发**。代价是一轮 win 腿（CI 上这个缺陷的速率比本机满载高，几乎一次就响）；读数是 CI 日志，同样带那三个名字 |
| **C. 我按「按图地址做键的表」猜一个修法** | 能在 Linux 上证明「期望值一行都没动」（七项计划），**但不能证明守卫不响**——而本缺陷的全部读数都在 Windows 上。把一张按地址做键的表改成按稳定 id，是本缺陷最像的候选（`capturedValues_`/`valueUsedGraphs_`/`captures_`/`graphGctOf_` 等十几张表都以 `DraftGraphBuilder*` 为键，其中 `__typeof__` 那张草稿图析构时只清了前两张），**但「最像」不是「是」**，而这个仓的规矩是「没有读数的改法不许合」 |

## 5. 默认建议

**走 B**：作者对 `lane-win-s1-6` 手动 dispatch 一次 CI 的 win-x64 腿，把 `generators_shapes_consumed` 那四行日志贴回来。
一次响就给出三个图名，下一轮就能把修法落到唯一一处记录点，并且可以用「四后端各 20 次」验收（那是 Windows 读数，
只能在 Windows 上取）。在拿到那三个名字之前不动 `captures.cpp` 的守卫，也不动上面那十几张表里的任何一张：
**守卫是代码的保证，它响说明记录那一步错了；把守卫改宽只会把这条缺陷从一个响声变成一次错误编译。**