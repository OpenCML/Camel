# graph-store 第 2 步 b：直连账（逐步填）

**这一路的账：535 → 534 → 533 → 532 → 530 → 521 → 515 → 517**（files 66 → 62，decode 6 → 5 → 4）。它**不是一条一直往下的线**：`534`（`fact.cpp` 的两处退路）、`530`（转储在 decode 副本上开事务）、`515`（`read.cpp` 不再提这个类型）各掉一格；中间三笔**一格没掉**——`calleeKernel` 把 `|| handle->draft()` 收成只问事务、`read.cpp` 三处把 `->draft()` 归零（退路本就是 `auto`，没有类型名可删）、删探针（探针不碰草稿）；**最后 515 → 517 是向上的一格，它是 C 裁定的代价**：`writings.h` 必须留一处"收草稿"的入口，而入口必须命名它收的类型。**"改一处就该掉一格不是必然"与"下降也不是一直向下"两句要一起读。**

b 的落点是"可编辑图归图仓的事务所有、前端一开始就在里面建图"，最后一笔删掉 `GraphDraft`。
本页只做一件事：把"还有多少处直接连到 `GraphDraft`"记成三个数，**每一笔提交填一行**。叙述可以说"删净了"，这三个数不会。

## 0. 三个数怎么数

两条口径，都写死：

1. **事务自己的两份文件不计**——`include/camel/runtime/graph_txn.h`、`src/runtime/graph_txn.cpp`
   引用 `GraphDraft` 是事务的墙，不是旧住户。
2. **按整词数，不按子串**——`git grep -w`。这一条是被自己的账逼出来的：按子串数会把
   `RuntimeGraphDraftSession` 里的六个字母组合也算成一次"直连"，基点因此虚报成 885（真实 712），
   而且会话那个类名每多写一次账就涨一次，涨的是噪声不是住户。

```bash
# 1. 事务外引用：文件数 / 引用数
git grep -lw GraphDraft -- src include modules | grep -v -e graph_txn.h -e graph_txn.cpp | wc -l
git grep -ow GraphDraft -- src include modules | grep -v -e graph_txn.h -e graph_txn.cpp | wc -l
# 2. 构造点：新建一张可编辑图的每一处
git grep -o 'make_unique<[^>]*GraphDraft>' -- src include modules | grep -v -e graph_txn.h -e graph_txn.cpp | wc -l
# 3. decode 点：`GraphDraft::decode` 的出现处（含 `src/runtime/draft.cpp` 的定义一句、
#    `modules/rewrite/proof.h` 注释里的一句）
git grep -o 'GraphDraft::decode' -- src include modules | grep -v -e graph_txn.h -e graph_txn.cpp | wc -l
```

**三个数只能单调降，b6 必须全 0。涨了就是那一笔在新建直连，停下解释。**
`decode` 取"出现处"（16 起）而不是"调用点"（14 起），把定义与注释也数进去，省得口径随人变。

**这条规则当场抓到了自己人**：b2.1b 的地址用例第一版在 `edges.cpp` 里写了三个 `GraphDraft *`，
数立刻从 709 涨到 714；改成 `auto *`（测试要的是"会话递出来的那一个"，不是它的名字）才归位。
规则不是摆设。

**这三个数现在是一条快层检查，不再靠自觉。** 用例
`test/cases/runtime/graph_draft_ledger/check.mjs`，计划
`test/plans/feat/runtime/graph_draft_ledger.plan.toml`（`tier = "fast"`，所以每一次闸门都跑它）。
它按本节的两条口径原样数（事务自己那两份文件不计、整词不按子串），判据是**当前值 ≤ 基线值**
而不是相等——这一步的目标就是让数往下走，所以降是绿、涨是红，红了就打印"哪个数从几升到几"和
新出现的位置（`文件#符号`，符号取该处所在函数）。基线在
`test/cases/runtime/graph_draft_ledger/baseline.json`，降了以后用 `check.mjs --record` 把新数记下来，
好让收益不能被后面一笔等量涨回去；`--record` 遇到涨的数会拒绝，除非 `--force`，那时 diff 会自己说话。

**这一笔落地时构造点数是 3，表里 b3a 起的旧行写的是 1**：本节第 2 条的 `make_unique<[^>]*GraphDraft>`
把 `std::make_unique<rt::GraphDraft>` 也算进去，旧行只数了不带限定的那一句。`region_extract.cpp`
和 `test/resources.cpp` 那两处是真在直接造一张可编辑图，按本节的口径就是构造点，所以记 3。

## 1. 表

| 提交 | 这一步做了什么 | 事务外引用（文件 / 引用） | 构造点 | `decode` 点 |
|---|---|---|---|---|
| `3d8cee417`（基点 `origin/develop`） | — | 90 / 712 | 9 | 16 |
| `2cdd65681` | 前端改由 `GraphTransaction` 拥图：`createCompileGraph` 开事务并在此取号，`DraftGraphBuilder` 不再 `make_unique<GraphDraft>`、`draft()` 答自 `transaction_->edit()` | 90 / 710 | 8 | 16 |
| `4f6fc592f` | b2.1 会话改由事务拥图：`RuntimeGraphDraftSession::DraftEntry` 持一份 `GraphTransaction`，`tryDraft`/`ensureDraft`/`edit`/`rootDraft` 从 `transaction->edit()`、`transaction->graph()` 回答，**30+ 调用点一个字没动**；`GraphTransaction` 增加 `decode`；`open` 不再替前端决定区域继承，改回 `DraftGraphBuilder` 自己说 | 90 / 709 | 8 | 15 |
| `fa2c94f68` | b2.1b 给这根生死线加两道锁（见 2 节）：`edit()` 返回类型必须是引用的静态断言 + `test::edges_ok` 的地址稳定性用例（计划要求该行输出，Release 与全优化链下都跑）；**不动任何消费者，三个数原地不动**——第一版曾涨 5，因为测试里点了 `GraphDraft` 的名，改 `auto *` 才归位 | 90 / 709 | 8 | 15 |

b1 的 −2 是从哪掉的：`src/compile/gir/draft_graph_builder.cpp` 的 `make_unique<runtime::GraphDraft>`
与 `include/camel/compile/gir/draft_graph_builder.h` 的 `std::unique_ptr<runtime::GraphDraft> draft_`
各掉 1 处。原始口径（含事务两份文件、按子串）从 885 变成 888——多的 3 就是墙本身那几处。

`decode` 点的历史：**基点和 b1 都是 16**，b1 没动它（口头报过的 15 是漏数了 `src/runtime/draft.cpp`
里那句定义）。b2.1 从 16 降到 15，是会话那一处 `GraphDraft::decode` 搬进了事务自己的
`graph_txn.cpp`——按口径不计，所以是真正的下降，不是口径漂移。

| b2.2 | `edit()`/`rootDraft()`/`ensureDraft` 返回 `GraphTransaction&`；全树 69 个调用点改绑：命名形式 `GraphDraft &d = session.edit(g)` → `auto &d = session.edit(g).edit()`（**不再点类型名**），chained 形式 `x.edit(g).foo()` → `x.edit(g).edit().foo()`；`&session.edit(g)`（地址用例要的事务本身）保持不加 `.edit()` | 80 / 661 | 8 | 15 |

| b2.3a | `bindFunctionCall` 收 `GraphTransaction&`（体首一行 `auto &draft = transaction.edit();`，函数体一字未改）；调用点 `session.rootDraft().edit()` → `session.rootDraft()`。**内联引擎那四个留到 b2.3b**：它们要先在 `engine.cpp:901` 把事务与图分开绑一次，`devirtualizeStaticCallInDraft` 的唯一调用点在 `pullback.cpp`，归 b3 | 80 / 659 | 8 | 15 |

| b2.3b | `specializeDirectFuncInDraft` 收 `GraphTransaction&`；`engine.cpp:901` 拆成 `auto &transaction = session.edit(g); auto &draft = transaction.edit();` 一次（内联引擎从此手里有事务）；**`inlineCallableInDraft`／`inlineCallInOrder`／`expandDefinition` 没能进来**：它们的另一个调用点在 `modules/autodiff/pullback.cpp:1421/1426`，那里只有 `draft_`、拿不到事务，所以整条链随 **b3** 一起换——硬塞就得在 b3 之前给自动微分造事务，那正是 b3 的题目 | 80 / 657 | 8 | 15 |

| b2.4a（测试体） | `passes/test/control.cpp` 的 15 处 `GraphDraft draft;` 换成真事务：`auto owner = GraphTransaction::open(static_cast<GraphId>(1)); auto &draft = owner->edit();`（既有 public 入口，无新构造路径，事务活过整个测试函数）。**它能单独编译通过，所以不挂在 b2.3e 上**；剩下的 `node(GraphDraft &draft, …)` 这个局部助手随 b5 | 80 / 642 | 8 | 15 |

| b2.4a（建图器签名） | 十个读图的助手从收 `const GraphDraft&` 改成收 `const GraphTransaction&`，体首一行 `const auto &draft = transaction.graph();`：`builder.h` 的六个声明（`moduleVariableOf`、`numberBodyPorts`、`readCalleeSignature`、`reachOfOper`、`reachOfFunc`、`reachOfCall`）＋ `builder_resources.cpp` 的定义，加 `builder.cpp` 的两个顺序边助手与 `builder_linearity.cpp` 的 `controlRegionsAround`、`readsNode`。**每个调用点单独定来源，不是实参改名**：`footprintOf` 绑一次 `graph->transaction()` 走到底；`reachOfOper`／`reachOfFunc` 保留建图器（它们要 `operTarget`／`funcTarget`）另收事务；`rehomeOrderEdges`、`controlRegionsAround`、`readsNode` 手里只有建图器，传 `graph->transaction()`。`builder.h` 从此不再点 `GraphDraft` 的名 | 79 / 625 | 8 | 15 |

| b2.4b（事实表按属主） | 两张表改存属主：`builder_linearity.cpp` 的 `GraphFacts::draft`（`const GraphDraft *`）与 `builder_diagnostics.cpp` 的 `unordered_map<GraphDraft *, bool>` 都改成 `GraphTransaction *`，读图处写 `facts[graph].transaction->graph()`；新增与 `nodeDraftOf` 对称的 `nodeTransactionOf(node)`（`builder_support.h`），于是"这个值是不是本图"从比存储地址改成比属主（`nodeTransactionOf(value) != known->second.transaction`）——b6 没有 `GraphDraft` 之后这个比较仍成立。剩下 31 处 `= X->draft()` 局部绑定（32 处里 `footprintOf` 那处随 b2.4a 已走）改成 `= X->transaction().edit()`／`= X->transaction().graph()`（只读的取 `.graph()`），不再点类型名；`builder_control.cpp`、`builder_helpers.h`、`builder_operators.cpp`、`builder_static.cpp`、`tree.cpp`、`reachable.cpp`、`debug_json.cpp` 里的这个名字一起消失 | 75 / 594 | 8 | 15 |

| b3a（自动微分的图开在事务里） | `GraphDraftGroup::Member` 改持 `std::unique_ptr<GraphTransaction>`：`reserve` 用分配到的 id `open` 一份事务、`draft(id)` 回答 `transaction->edit()`、新增对称的 `transaction(id)`、`define` 收 `unique_ptr<GraphTransaction>`（定稿时把 id 与 program 盖上）、`encode` 从 `transaction->graph()` 取图。**这一处是从 b4 提前的**：`gradient()` 的最后一件事就是把图交给 group（`group_.define(key, …)`），`Member` 若留到 b4，b3 就只能在"交给 group"这道边界上退成收 `GraphDraft&`——正是这一步要消掉的第二入口，所以连带提前。自动微分自己造图的四处（`gradient` 的图、分支臂的 pullback、循环体的 pullback、循环的逆闭包）改成 `GraphTransaction::decode`／`GraphTransaction::open`，`Pending::source` 与 `planForDraft` 一并改事务；`buildReverseClosure` 从返回草稿改成在调用方开的事务里建（`open(closureKey)` 再 `define`）。**只读图的助手一个没动**，仍收事务发下来的 draft。臂的 pullback 在 reserve 之前就要开图，用 `open(kInvalidGraphId)`、id 由 `define` 盖上——`make_unique<GraphDraft>()` 今天本来就是这个状态 | 75 / 582 | 1 | 13 |

| b3b（内联链收事务） | `ForwardRewrite` 手里从此有事务：新增 `rt::GraphTransaction &transaction_`，构造函数收事务、`draft_` 用 `transaction_.edit()` 初始化（成员版的"体首一行"），于是 `pullback.cpp` 里那两处内联调用（`devirtualizeStaticCallInDraft`、`inlineCallInOrder`）能把事务传下去——这是 b2.3b 记下的那个卡口。四个编辑别人图的助手改收事务、体首一行 `auto &draft = transaction.edit();`、函数体一字未改：`inlineCallableInDraft`、`devirtualizeStaticCallInDraft`（`draft_inline.h/.cpp`）、`inlineCallInOrder`（`opt/inline.h` ＋ `engine.cpp:1150`）、`expandDefinition`（`runtime/definitions.h/.cpp`，头文件补 `graph_txn.h`）。调用点逐个定来源：`engine.cpp:950/1084` 传 `:901` 那份事务、`:1238` 传本函数的、`definitions.cpp:575` 传本函数的、`passes/test/definitions.cpp:163` 从 `session.edit(g)` 取事务（`draft.draftIdOfSourceRef` → `transaction.edit().draftIdOfSourceRef`）。循环体的磁带计划也收事务：`loopBodyPlanFor`／`buildLoopBody` 改参数，缓存键从 `const GraphDraft *` 改成 `const GraphTransaction *`（按属主做键，与 b2.4b 同一理由） | 73 / 567 | 1 | 12 |

**b3 的这一串数写在一行**：594→582→567、构造点 7→1、decode 16→13→12；语料两次——b3a 3358/3359 逐字节相同（差的 `test/cases/linear/byref/var_capture_reference.cml` 是 1 节记过的行序抖动，473 行多重集相同、退出码 0），b3b **3359/3359 全同**。

| b3c-1（成员不再持草稿） | `ForwardRewrite` 的 `GraphDraft &draft_` 换成 `rt::GraphTransaction &transaction_` ＋ 访问器 `auto &draft() const { return transaction_.edit(); }`，体里 246 处 `draft_` 改叫 `draft()`：不点类型名，也不用在五十个方法的体首各插一行（访问器是 `const`，`member()` 这类只读方法照样能问它要图）。`Backward` 按定下的设计收**两个事务**：`rt::GraphTransaction &forward`（它读的图）与 `&target`（它写的图），两个成员同样换成事务 ＋ `forward()`／`target()` 访问器；`inPlace_(&forward == &target)` 一字未改——同图就是同一个事务，`gradient` 里传 `*transaction, *transaction` 两次同一个地址，"现在允许复用吗"这条规则从此写在调用点上，不在签名里。四个起点逐个定：臂 `(transaction_, *pullbackOwner)`、`Engine::build` `(*pending.source, *pullbackOwner)`、循环体 `(transaction, *pullbackOwner)`、`gradient` `(*transaction, *transaction)`。**剩下的读侧**（33 个自由助手的签名与它们的调用点、`regionOwners_` 的 `const GraphDraft *` 键、`using rt::GraphDraft;`）是 b3c-2。b3a 顺手加的那个 `GraphDraftGroup::transaction(id)` 访问器在这里删掉：**没有调用者**，就是提前造的入口，b4 搬居民时自然会有第一个，那时再加签名才准 | 73 / 562 | 1 | 12 |

| b5a（自己 decode 输入的遍改从事务取图） | 三处自己给输入 decode 一张可编辑图的遍，改问事务要图，构造点一处未动：`gir_canonical` 的 `orderingOf` 用 `GraphTransaction::decode`、`transaction->edit()` 读并化简顺序边；`test::graph_release` 在事务里造 20000 个空节点再 `encode`；`test::writings` 的四处 `GraphDraft::decode` 全部改事务，两个只读助手 `portsOf`／`firstOperWith` 改收 `const GraphTransaction&`、体首一行 `const auto &draft = transaction.graph();`，五个调用点先绑一次 `auto &transaction = session.edit(g)` 再 `transaction.edit()`。`writings.cpp` 只留 `GraphDraft::WritingScope` 一处——那是存储自己的 RAII 护栏，不是这里的住户。`gir_canonical.cpp` 与 `graph_release.cpp` 从此不再出现这个名字 | 71 / 553 | 3 | 6 |

`b5a` 的 `check.mjs --record`：`[LEDGER] recorded files 71 references 553 constructions 3 decode 6`。命名按层走：这一笔先拿"读的最薄、调用者一两个、不依赖谁"的那片，b4（运行时会话与通用改写）没动。

| b5b（遍自己的局部助手收事务） | 两个遍的局部助手不再收 `GraphDraft&`：`tensor::fuse` 的七个（`operOf`、`isPrivateIntermediate`、`setSymbol`、`kernelChoice`、`markKernels`、`markGroup`、`markGroups`）与 `test::loop_collect` 的三个建图小助手（`addAccs`、`addOper`、`addInt`）改收 `GraphTransaction&`，只读的体首一行 `const auto &draft = transaction.graph();`、要写的 `auto &draft = transaction.edit();`，函数体其余一字未改；调用点把 `session.edit(g)` 那一份事务直接传下去，不再先 `.edit()` 再传。两个文件从此不再出现这个名字 | 69 / 541 | 3 | 6 |

`b5b` 的 `check.mjs --record`：`[LEDGER] recorded files 69 references 541 constructions 3 decode 6`。

| b5c（类型重建收事务） | `retypeDraft` 与它内部的 `inputCountOf` 改收 `GraphTransaction&`（要写的体首一行 `auto &draft = transaction.edit();`，只读的那一句用 `transaction.graph()`）；`GraphRetypeOptions::nodeType` 这个钩子的第一个参数也从 `const GraphDraft&` 换成 `const GraphTransaction&`——它今天没有实现者，签名按"读者拿事务"写，将来第一个实现者不必再改一次。三个调用点把 `session.edit(g)` 那一份直接传下去：`retypeGraph`、`retypeProgram`、以及 `defun_value` 的整程序重建（先绑 `auto &transaction = session.edit(graph)` 再 `transaction.edit()` 给它后续的读）。头文件的说明同步改成"重建一个事务拥有的图" | 67 / 537 | 3 | 6 |

`b5c` 的 `check.mjs --record`：`[LEDGER] recorded files 67 references 537 constructions 3 decode 6`。

| 并入 develop（`6ed427f5e`） | 只并基线，三个数一笔未动：develop 这九笔带来的是"去函数化只剩一个遍"、`transforms` 模块及其接线、和 effect chain place 丢失的账。与这三层相交的只有 `defun_value.cpp` 里那个 `retypeDraft` 调用点，按新行号自动落位，无冲突；并后重数仍是 67 / 537 / 3 / 6，所以这一行没有新的 `--record` | 67 / 537 | 3 | 6 |

| b5d（宏改写桥收事务） | `execute/macro/rewrite.cpp` 的 `bridgeMacroRewriteControl` 改收 `GraphTransaction&`（体首一行 `auto &draft = transaction.edit();`，函数体其余未动）；唯一调用者 `applyMacroRewrite` 先绑事务、再 `.edit()` 给后续的读。文件头那句"materialized into the owner GraphDraft"随之改成"into the graph one transaction owns"，因为这个函数的墙确实换了。这个文件从此不再出现这个名字 | 66 / 535 | 3 | 6 |

`b5d` 的 `check.mjs --record`：`[LEDGER] recorded files 66 references 535 constructions 3 decode 6`。

命名说明：上面那行 `b2.4a（测试体）` 是本页早先写的（`control.cpp` 那 15 处），协调者后来把这一步拆成"建图器签名"和"事实表"两笔并沿用 b2.4a／b2.4b 两个字母，所以本页有两处 b2.4a；括号里的名字才是区分。

### 一处行序抖动：`test/cases/linear/byref/var_capture_reference.cml`

b2.4a／b2.4b 两笔扫下来是 3359 个 `.cml` 里 3358 个逐字节相同，差的那一个**变的是打印顺序，
不是内容**。证据（量过，写在这里给下一个读到"3358/3359"的人）：同一个二进制连跑 10 次打出 3 种顺序；
把它放到另一个与本次改动无关的 checkout（`Camel` 树，停在 `abbc9c229`）上跑 8 次打出 2 种，
其中一种**正是** b2.4b 扫出来的那种。两份清单里该文件的行多重集完全相同，其余 393,408 条行摘要全等，
3359 个文件的退出码 0 处变化。所以这是这条用例自己的后继边打印序在抖，不是这一步改坏的。


b3 的两笔是同一个目的的两半：前半给自动微分造事务（不然 `ForwardRewrite` 手里没有东西可传），
后半把事务传进内联链（不然事务造了也只是躺着）。顺序不能反。

## 2. 那一处生死线：`edit()` 只能给引用

b2 起消费者不再点类型名，写的是 `auto &draft = session.edit(g).edit();`。这只有在
`session.edit(g)` 给的是**会话里那个已经存在、活得够长的事务**时才成立；若它按值返回事务，
`.edit()` 给的就是临时对象的图的引用，语句一结束就悬垂，而**调用点上没有任何编译错误**。
两道锁：

1. `include/camel/runtime/draft_session.h` 类后的 `static_assert`：`edit()` 的返回类型必须是引用。
   谁改成按值，编译就过不去。
2. `src/passes/test/edges.cpp` 的 `test::edges_ok`（`test/plans/feat/opt/edges.plan.toml`）：
   同一张图连取两次 `edit()`、中间插一次编辑、再取一次，三个地址必须相同，打印
   `EDGES transaction stable`，而计划里这一行是必须出现的（`expect_stdout_contains`），
   Release 构建下每次闸门都跑；`ASSERT` 在 `NDEBUG` 下是空的，所以这里用输出而不是断言。
   手动再叠一遍全链：`camel cases/opt/generic/generic_passes.cml test::edges_ok std::opt std::gir`
   打出的仍是这两行，说明优化打开后它没有骗人。

## 3. 还没填的行

b2 起逐笔往下填：b2.2 起按 `src/passes/test` → `opt/inline` ＋ `opt/generic` →
`modules/{graph,rewrite,tiles,tensor,onnx}` → `src/execute` 四批搬，每批一行、每批三个数。
**b6 那一行必须是 `0 / 0`、0、0**；不是 0 就说明 `GraphDraft` 还有住户，不能删名。
## 4. 这笔为什么不是四笔（API 翻面是原子的）

b2.2 原计划按 `src/passes/test` → `opt/*` → `modules/*` → `src/execute` 四批搬。
实际做成一笔，理由只有一条：`edit()`/`rootDraft()` 一改返回类型，**全树 69 个调用点在同一瞬间不成立**，
中间态编译不过；而"每一笔都要绿"是硬约束，所以不能按目录切。往后"按目录分批"对**剩余的类型名**仍成立
（它们的签名彼此独立），但对"换 `edit()` 的返回类型"这件事不成立。将来按"按目录分批"的规矩回头看，
看到这一笔合了四批，是这条原子性，不是漏做。

## 5. skip 2 → 7 的读数不可比

b2.2 那一跑的闸门是 `total 7802, pass 7793, fail 0, skip 7`，基线是 `pass 7798, fail 0, skip 2`。
差的 5 条全是 `skip_unless = ["node", "test/tools/have-cuda-device.mjs", "0"]` 的用例
（`device_startup.available`、`device_startup.one_device_named_missing`、`gpu_preference.named_device`、
`gpu_scope.placement_with_a_device`、`gpu_scope.host_effect_synchronises`）。原因不在代码：
`tiles::available()` 的实现就是"`backend()` 能否构造"（`modules/tiles/src/cuda.cpp:609`），
而那一跑时 GPU 0 已用 **23,956 / 24,576 MiB**——四个 `VLLM::EngineCore` 进程各占 20 GiB——探针连跑三次都答 false，
20 秒内显存数字不动。**所以这一行的 `pass 7793` 与基线的 `pass 7798` 不可比**。

**复测（在 `991b37d98` 这个 tip 上重跑）**：仍是 `total 7802, pass 7793, fail 0, skip 7`——
测试主机 `88zjwei` 的 device 0 此刻是 **24,110 / 24,576 MiB**，四个 `VLLM::EngineCore` 还在，探针照旧答 false。
判据是那台机器上的探针，不是别处空着的显存；所以这一格仍是"不可比"，等它空出来再跑一次、
把 `skip` 收回 2、`pass` 回到 7798 才能改口。

**另一条账**：这一轮我并行做了一件不该做的事——闸门在跑的时候又推了一次构建，第二次构建替换了同一个
`out/latest`，于是那一跑报 `fail 12`；串行重跑是 `fail 0`。**同一台主机上构建与闸门必须串行**，
这条写在这里，免得下次拿一次假红当真红。

## 6. 剩下这 661 归谁

每个格子都要能指到具体一笔；b6 之前一格不能没人认。

| 区域 | 事务外引用 | 归哪一笔 | 说明 |
|---|---|---|---|
| `src/runtime/draft*.cpp`、`draft.h`、`draft_*.h`（存储自身） | 129+39+20+20+13+7+3+1 ≈ 232 | **b6** | 存储的名字随类型一起消失，没有更早的笔能动它 |
| `modules/autodiff/pullback.cpp` ＋ `draft_clone` | 60+13+4 = 77 | **b3** | 自动微分自己造草稿、自己 decode，GraphDraftGroup 一起换 |
| `draft_inline.*`、`writings.*`、`opt/generic/*`、`opt/inline/engine` | 32+6+15+5+23+17+11+4+4 ≈ 117 | **b2.3** | 内联与改写引擎的签名，这一批的正题 |
| `draft_session.*`、`control`、`loop`、`retype`、`graph.*`、`graph_build`、`definitions` | 22+9+9+6+2+4+11+5+4+2+2+1 ≈ 77 | **b4** | 运行时会话与通用改写 |
| `src/compile/gir/builder_*`、`draft_graph_builder.*`、`graph_encode`、`nodes.h`、`graph.h` | 12+11+8+7+6+4+5+4+1+3+3+1+1 ≈ 66 | **b2.4** | 前端已经把图建在事务里了（b1），这里剩下的只是签名里的**拼写**；**单独一笔，不并进 b6**——b6 只做删除（删类型、构造私有化），diff 应当几乎全是删除行，那本身就是它做对了的证据 |
| `passes/test/*`（control 16、writings 7、resources 4、loop_collect 4、fault 2、graph_release 1、anchor 2） | ≈ 36 | **b5** | 测试遍 |
| `modules/graph`、`modules/tiles`、`modules/rewrite`、`modules/onnx`、`tensor/fuse` | 6+5+1+3+3+2+2+1+3+1+8 ≈ 35 | **b5** | 模块与后端生产者 |
| `core/tags`、`execute/macro`、`gir_canonical`、`node_ref.h`、`operator_instance`、`loop.h`、`func.h`、`proof.h` 等零散 | ≈ 27 | **b5** | 零散消费者，一处一改 |

（数字随 b2.3 起每笔更新；这张表和三个数是同一份账。）

## 7. 估量要给两个数：拼写数 + 调用者数

b2.3e 我先报的是"60 处"（`rewrite.h` 17、`generic.cpp` 23、`rewrite.cpp` 11、`algebra.cpp` 4、`gates.cpp` 1、`txn.cpp` 3、`exporter.cpp` 1），开工后发现**外部调用点远不止 7 行**：`passes/test/control.cpp` 7、`modules/onnx/exporter.cpp` 9、`modules/graph/txn.cpp` 5、`compile/gir/builder_control.cpp` 4、`builder_helpers.h` 3、`passes/trans/cml/cml.cpp` 3、`runtime/resources.cpp` 3 等，**11 个文件、约 41 行**。
**教训**：只数了 `GraphDraft` 这个拼写，没数"谁会把这些助手当参数调"。**以后凡估量一次迁移，两个数一起给：拼写数、调用者数**；只给一个数就是没量。

## 8. b2.3e 的依赖顺序：它在 b2.4／b3／b4 的下游

`opt/generic` 那 15 个助手一改签名，**每一处调用它们的函数都必须手里有事务**。实际一试，拿不到事务的持有者有三类，而且都不是 b2.3e 的题目：

| 拿不到事务的持有者 | 它调哪些已改签名的助手 | 归哪一笔 |
|---|---|---|
| `passes/opt/inline/engine.cpp` 的 `inlineCallInOrder`（另一个调用点在 `pullback.cpp`） | `orderedSources`、`operIdOf` | **b3** |
| `compile/gir/builder_*`（前端建图时只握 draft） | `operIdOf`、`isPureOper`、`reaches` 等 | **b2.4** |
| `runtime/resources.cpp`、`definitions.cpp`、`draft_session.cpp`、`passes/trans/cml/cml.cpp`（会话内部的 draft） | 同上 | **b4／b5** |

所以"先把干净的那片拿下"这个判断**对 `opt/generic` 内部成立、对整个助手 API 不成立**：它像 b2.2 一样是原子的，而且**原子范围跨过 b2.3e 自己的边界**。**结论：b2.3e 要排在 b2.4（builder）、b3（autodiff）之后做，或者并进 b6 那次大翻面**——先做它，等于提前替 b2.4／b3 造事务。
这一笔的代码尝试已按备份分支 `b2.3e-backup` 完整退回，树在 `9940cf5d3` 上是绿的。


## 9. 顺序改了：b2.4 → b3 → b2.3e

原计划 b2.3e（`opt/generic` 那 15 个助手）在 b2.4 之前，实测后改到它俩之后，理由是依赖，不是偏好：

- **`opt/generic` 的助手签名一换，每一处调用它们的函数都必须手里有事务**；而拿不到事务的三类持有者恰好是 b3（`inlineCallInOrder`／`pullback`）、b2.4（`compile/gir/builder_*`）、b4／b5（`runtime/resources.cpp`、`definitions.cpp`、`draft_session.cpp`、`passes/trans/cml/cml.cpp`）。**b2.3e 的真正位置在它们下游。**
- **不并进 b6**：b6 的规矩是"只做删除、diff 应几乎全是删除行"，那是它能被看懂的证据；把 15 个签名 + 77 个调用点塞进去，b6 就同时是"改名"和"删名"，证据没了。

## 10. 判据：改完能删代码的数量（三处评估后不做）

三个数只说明"还剩多少拼写"，不说明"这一笔值不值"。补一个判据，**改完能删掉几行代码／几个局部变量**；为 0 就是净负（调用点变长、函数体多一行绑定），不做。三处照这个判据量过，都不做：

| 层 | 拼写 / 调用点 | 能删的代码 | 结论 |
|---|---|---|---|
| `passes/test/control.cpp`（b5d 评估） | 1 处签名 / 46 个调用点 | **0 个局部变量** | **不做** |
| `runtime/graph.cpp` 的 5 个 read-only 助手 | 5 处签名 / 12 个调用点 | 0 | **不做，整片留 b4** |
| `modules/tiles` | 10 处 / 6 文件 | 0 行 | **不做，留 b4** |

**`control.cpp` 为什么是 0**：那 46 个调用点改成 `node(*owner)` 之后，每个测试函数的 `auto &draft = owner->edit();` 一行也删不掉——这个文件里每个测试自己都要拿草稿调被测的 API（`reduceControlDependencies(draft, …)`、`draft.ctrlInputsOf(…)`、`draft.addNode(…)`），那行绑定是测试的题目，不是直连的残渣。**换掉 1 处拼写、46 个调用点变长，净负。**代码尝试已退回，树回到 `ef6dcac85`。

**`graph.cpp` 为什么整片留 b4**：那 5 个助手（`draftRuntimeNodeRefs`、`nodeIsFreelySchedulable`、`isUnanchoredPure`、`carriesOrdering`、`describeDraftPayload`）的 12 个调用点里，9 个在拿不到事务的持有者手里（`opt/generic/rewrite.cpp`、`opt/generic/generic.cpp`、`writings.cpp`、`draft_inline.cpp`）。**b5 的边界不是"还能改哪儿"，而是"改完不会把 b4 拽进来"。** 账本 §6 把 `graph.*` 归 b4、`draft.h`（存储自身）归 b6，这里以它为准。

**`modules/tiles` 为什么是 0**：这 10 处里真正的住户有三个——`region_extract.cpp` 的 `std::make_unique<rt::GraphDraft>()`（**三个构造点之一**）、`regions.h` 的 `std::unique_ptr<runtime::GraphDraft> graph` 成员、`region_test_pass.cpp` 的 by-value 局部 `runtime::GraphDraft draft;`。改签名要先把这三个住户换成事务，那是 b4 的规模；只改签名则两处定义各多一行 `auto &draft = transaction.edit();`，删掉的行数是 0（`packGraphAggregate`／`packGraphTuple` 的 5 个调用点、`transferAggregate` 的 3 个调用点一个都不因此消失）。

**判据的第三条（最容易违反的一条）**：**改签名而调用点不减，等于把成本从一处搬到 N 处——那不是重构，是扩散。** 判断时把两个数列出来：改前 `拼写数 / 调用点数`，改后预期 `拼写数 / 调用点数`；后者只少了拼写、调用点一个不少，且没有一行代码被删掉，就是扩散。

## 11. b5 剩下的格子：三个数一次量完，全是 0

按 §10 的判据把 b5 剩下的几处一次量完（处数 / 调用点数 / **改完能删的行数**），按能删行数降序——全为 0，所以**一处都不做**：

| 区域 | 处数（文件） | 调用点 | 能删的行数 | 真正的住户是谁 |
|---|---|---|---|---|
| `modules/graph` | 15（4：`handle.h` 5、`handle.cpp` 1、`read.cpp` 6、`txn.cpp` 3） | `->draft()` 4、`GraphDraft::decode` 1，另有 handle 的构造与访问器 | **0** | handle 存的 `const GraphDraft*`；换成事务是尺寸中性的重拼 |
| `modules/rewrite` | 4（2：`fact.cpp` 3、`proof.h` 注释 1） | 1（`first->draft()`） | **0** | 它从 handle 取图，handle 不改它没得改 |
| `passes/test/resources.cpp` | 4 | 29 处调用（13 行） | **0** | 一个构造点 `make_unique<rt::GraphDraft>()` + 三个 `(GraphDraft&)` lambda；构造点按规矩不动 |
| `passes/test/fault.cpp` | 2 | 2（`breakFirstBranchOfMain` 的回调签名） | **0** | 回调类型由测试辅助定，改它要动共享设施 |
| `modules/onnx/exporter.cpp` | 1 | 1（`bindCaptures(session.rootDraft().edit(), …)`） | **0** | 只多传一个事务、少一次 `.edit()`，行数不变 |
| `passes/test/writings.cpp` | 1 | 1（`GraphDraft::WritingScope`） | **0** | 存储自己的 RAII 守卫，归 b6 |
| `modules/tensor`（`fuse.cpp`） | 0 | 0 | 0 | b5b 已做完 |
| `passes/test/anchor.cpp` | 0 | 0 | 0 | 已是 0 |

**结论**：b5 到这里只剩"住户"——构造点、handle 的存图、存储的 RAII 守卫、编码器的回调签名；它们的名字要随类型一起消失，那是 b6（handle 那条还牵着 b4）。**b5 不再有正收益的层可做**；再往下该开 b4／b6，而不是在 b5 里继续找。

**b5 收尾（本页到此为止，不再往下推）**：b5 剩余八处，能删行数全为 0；剩下的住户是构造点、handle 的存图、`WritingScope`、编码器回调——归 b6（handle 还牵 b4）。**b5 到此为止。** 加一句给后来人：**别再把 b5 的边界往后推——能删行数为 0 的层就是边界。**

## 12. b4 的入口：handle 一片，四个数 + 构造点一分为二

b4 只有一个人口：`modules/graph/handle.h` 里那个 `const GraphDraft *draft_`。b4 的难点不是签名，是"**有人要先拿到图才能拿到 handle**"，量的时候把它单列。

**四个数**：
1. **handle 走图的处数**：**15 处 / 4 文件**（`handle.h` 5、`handle.cpp` 1、`read.cpp` 6、`txn.cpp` 3）。
2. **调用点数**：**7**——`read.cpp` 的 `handle->draft()` 3（136、164、446）、`*node.draft` 1（637）、`GraphDraft::decode` 1（672）、`fact.cpp:825` 的 `first->draft()` 1、`handle.cpp:271` 的 `createInDraft(*draft_, …)`（clone）1。
3. **若把 handle 换成事务能删几行**：**0 行**。`draft_` 换成 `transaction_` 是同数目的成员，`draft()` 访问器改成 `graph()` 也是同 1 行；**但它顺手消掉一个 decode 点（6 → 5）和一条"草稿活得比事务久"的路**——所以它的价值不在行数，在把 b6 的前提挪到位。
4. **换完谁还会去拿 handle**：**13 个构造点是"从 program 拿图"造出来的**（`read.cpp` 8、`txn.cpp` 3、`fact.cpp:424` 1、`module.cpp:1544` 1），这些调用者手里只有 `Program`／`Function`、没有事务；**这批人就是 b4 的第二片**（Camel 侧的 `graph::` 函数与 rewrite 模块的 `whenArg`）。

**构造点一分为二（决定 b4 是一笔还是三笔）**：**4 个在事务里造**（`createInDraft`：`read.cpp:165`、`fact.cpp:422`、`module.cpp:332`、`module.cpp:360`），**13 个在事务外造**（`Handle::create`，从 `program` 造）。**四六开、而且"事务外"那侧全是同一个形状（`program` → `Handle::create`）**，所以 b4 更像"一片（把 4 个在事务里的先收）+ 一片（13 个从 program 造的，要先给他们事务）"，而不是三笔。

**一条结构事实，写在这里免得下一个人以为这是改名**：`handle.cpp:281` 的 `updateRefs` 只 trace `program_`，`draft_` 是**借来的裸指针、不参与 GC 追踪**；`equals()` 又拿 `draft_` 指针当身份。换成事务要同时回答"谁持有它、它参不参与 `updateRefs`、身份怎么比"——**这是所有权问题，不是拼写问题**，也正因为如此，它必须排在 b6 之前、且不能并进 b5。

## 13. b4 写成两片（每片当天能绿，规则一条一笔）

作者的规矩是"一条规则一笔、每笔当天合入"，所以 b4 不写成一大笔，写成**两片**：第一片收"已经在事务里造"的那 4 个构造点，第二片才是"从 program 造"的那 13 个。

### 第一片：handle 存事务（4 个事务内构造点 + 7 个调用点）

- **动哪些文件**：`modules/graph/handle.h`、`modules/graph/handle.cpp`、`modules/graph/read.cpp`（165 的 `createInDraft`、637 的 `*node.draft`、672 的 decode 一条线）、`modules/rewrite/fact.cpp`（422）、`modules/rewrite/module.cpp`（332、360）。
- **哪些调用点**：7 个——`read.cpp:136/164/446` 的 `handle->draft()`、`read.cpp:637` 的 `*node.draft`、`read.cpp:672` 的 `GraphDraft::decode`、`fact.cpp:825` 的 `first->draft()`、`handle.cpp:271` 的 `createInDraft(*draft_, …)`（clone）。
- **验收怎么量**：① **decode 6 → 5**（`git grep -nw GraphDraft` 里那处 decode 变成 `GraphTransaction::decode`）；② **`handle->draft()` 3 处消失**（`git grep -n '\->draft()' modules/graph` 应为 0）；③ `GraphDraft` 拼写 15 → **11**（4 个 `createInDraft` 的签名与 1 个成员改名）；④ `never_rises` 绿、闸门 `fail 0`；⑤ `equals()` 与 `updateRefs` 的注释改成说事务，不再说草稿。
- **做完之后还有谁去拿 handle**：**13 个"从 program 造"的构造点**（`read.cpp` 8、`txn.cpp` 3、`fact.cpp:424`、`module.cpp:1544`）。它们是**第二片的对象**；这一片之后，"草稿活得比事务久"的路只剩 `GraphTransaction::decode` 与 b6 的存储自身。

### 第二片：从 program 造 handle 的那 13 处

- **动哪些文件**：`modules/graph/read.cpp`（8 处构造 + 它们的 `program` 来源）、`modules/graph/txn.cpp`（3）、`modules/rewrite/fact.cpp:424`、`modules/rewrite/module.cpp:1544`。
- **哪些调用点**：这 13 处构造；以及它们上游"先拿到图才能拿到 handle"的那批（Camel 侧 `graph::` 函数、rewrite 模块的 `whenArg`）。
- **验收怎么量**：① `modules/graph` 的 `GraphDraft` 15 → **0**；② 每个 `Handle` 都指得到一个所有者（**事务**，或**已发布程序**），没有第三种；③ 闸门 `fail 0`、`never_rises` 绿。
- **做完之后还有谁去拿 handle**：**没有第三种持有者**。这一片可能得出"program-backed handle 保留 program 就够、不必造事务"的结论——**那也是它的结论**，写进账本，别硬造事务。

**两片的顺序不能反**：第一片是"把已经在事务里的那 4 个收进屋"，尺寸小、风险低；第二片要回答"读已发布的程序要不要事务"，那是设计问题，得等第一片把 handle 的形状定下来。

## 14. 判据的第二版：收益有三种，不能只记"删了几行"

§10 的判据只记了第一种收益（删行数），会被 b4 第一片判成净负——因为它删 0 行。补齐：

| 收益类型 | 怎么量 | 例子 |
|---|---|---|
| ① 删掉的行／局部变量 | 行数 | b2.4、b5a–b5e（拼写与绑定一起消失） |
| ② 消掉一个显式点 | 计数点（decode、构造点、`->draft()`） | b4 第一片：decode **6 → 5**、`handle->draft()` **3 → 0** |
| ③ 消掉一条生存期漏洞 | "有没有东西活得比它的所有者久" | b4 第一片：草稿不再能脱离事务被 handle 持有 |

**判据**：一笔要么删行，要么消点，要么堵漏；**三样都没有才是净负**。提交信息里必须写出它是哪一种——否则按"删行数"看，b4 第一片是 0 行，会被误判成扩散。

## 15. b4 第一片：停在哪（可构建的中间态）

**停在哪**：`modules/graph/handle.h`、`modules/graph/handle.cpp` 两个文件改完并**构建绿**——
`Handle` 现在同时能指名**事务**（`transaction_` + `transaction()` + `createInTransaction` + `clone` 走事务分支）和**草稿**（旧的 `draft_`、`createInDraft` 原封不动）。
`equals()` 把 `transaction_` 并进身份比较；`updateRefs` 多了三行注释，写明**事务是借来的、不参与追踪、句柄活得比它久就什么也不指名**。

**为什么这样切**：这是一次纯新增的中间态——**没有任何调用者用 `createInTransaction`**，所以行为与上一笔逐字节相同，树随时能构建、能整闸。类型一换就成了"必须原子完成"的笔，那正是今晚不该做的。

**下一步第一件**：`modules/graph/read.cpp:165` 的 `nodeHandle(from, node)` —— 它现在从 `from->draft()` 取草稿，改成从 `from->transaction()` 取事务、用 `createInTransaction`；同一段里 `from->draft()` 的判空顺带改成 `from->transaction()` 判空。**这一步做完，`read.cpp` 的 `->draft()` 从 3 处降到 2 处。**

**树能不能构建**：**能**（`-Plan compile` 全绿：1845/1845、`fail 0`，build 16.2 s）。三个数仍是 66 / 535 / 3 / 6——这一笔只加能力、不删拼写，账要等调用点搬过去才动。

**从这一笔里出来的一条规矩：新代码里点名就算数，`auto` 是这一层的写法。**
起因很小：`createInTransaction` 里先写成 `const camel::runtime::GraphDraft &draft = transaction.graph();`，账本当场判红（535 → 536），改成 `const auto &draft = …` 才绿。但它说明的不是"台账要省一格"，而是**这一层的类型名是借来的，写类型名等于宣告所有权**——写下 `GraphDraft` 的人，等于说"这份草稿归我管"。
所以 b4／b6 从此按这一条判断：**谁在代码里写出 `GraphDraft`，谁就是在宣告自己持有它**；只想借来用一次的，写 `auto`，让所有者留在它该在的地方（事务、会话、存储自身）。

## 16. b4 第一片停在这里：它前面还有一层 `Writings`（§12 的"4 个在事务里"要改）

原计划下一件是 `read.cpp:165`：把 `nodeHandle(from, node)` 从 `from->draft()` 改成 `from->transaction()`。**试读之后不能做**，证据两条：

1. **读端先改，会让所有草稿 handle 变成"看不见"**：`viewOf` 与 `nodeHandle` 判"这是不是草稿"用的就是 `handle->draft()`；先换成 `transaction()`，那些由 `createInDraft` 造出的 handle（`transaction_` 为 null）会被当成已发布的图，落到 `nodeOfFrozen` 上，而它们的 `program_` 是 null。
2. **写端也换不了**：`module.cpp:259` 是 `auto &draft = match.writings.draft();`，而 `Writings` 存的是 **`GraphDraft &draft_`（`writings.h:414`／`542`），它手里没有事务**；`fact.cpp` 的 `Source.draft` 又是从 handle 借的（`fact.cpp:859`）。

**所以 §12 那句"4 个在事务里造"是错的：这 4 个不是"在事务里"，是"在存储里"**——草稿来自 `Writings`。**17 个构造点里，0 个手里有事务。**

**两条路，都是别人那一层的题：**
- **A（推荐）**：先让 `Writings` 通过事务持有草稿（它的构造本来就从会话拿草稿，`writings.h:405`），handle 随后跟上。这**不是 b4 第一片，是 b6 的第一小步**。
- **B**：`draft_` 留在 handle 里，等 b6 随类型一起消失（第一片不做）。

**状态**：这一轮没有改代码（树干净、构建绿）；已落地的是纯加能力的 `e467242e3`（`transaction_`／`createInTransaction`，无调用者）。协调者选了 **A**，见 §17。

**一条由此得来的规矩**：**判"是不是草稿"的每一处，都是搬家的前置条件，不是搬家的对象。** 谁把 `handle->draft()` 改成 `handle->transaction()`，就必须先让被问的那个东西有事务；先改读端，等于把"有草稿"这个判据抽掉。

## 17. b6 的第一小步：`DraftWritings` 记下它的事务（A 的后半）

**做了什么**：`DraftWritings` 新增一个收 `GraphTransaction &` 的构造（旧的收 `GraphDraft &` 的构造**原封不动**），成员多了 `transaction_` 与访问器 `transaction()`；重写模块里 5 个构造点从"传草稿"改成"传 `session.edit(graph)`"。**handle 侧一行没改。**

**为什么它是 b6 的第一小步**：`DraftWritings` 是重写引擎里"草稿"的**存储侧所有者**——`Match::writings.draft()` 就是 `module.cpp:259` 取草稿的地方。它先拿到事务，"草稿有个不来自 handle 的所有者"才成立；下一步才轮得到 `fact.cpp#Source.draft` 不再从 handle 借。

**验收（照 §14 的三种收益）**：① 删行 0；② 消点 0；③ **消掉一条生存期漏洞的一半**——草稿从此能指认它的所有者（事务），只是还没有读端用它。**三个数一个没涨：66 / 535 / 3 / 6**；`read.cpp` 的 `->draft()` 仍是 3；行为逐字节相同（`transaction_` 没有被任何代码读）。构建：`-Plan compile` 1860/1860、`fail 0`。

**下一步第一件**：`fact.cpp#Source.draft` 改从 `DraftWritings` 取（不再从 handle 借）。

**撞号的正面产出**：§18 那条注记不是"另一条线的插曲"，它正好印证 §15 的规矩在 **b4 之外**第一次应验——`defunctionalize.cpp` 的 helper 只借一次草稿，写成 `template <class Draft>` 而不是 `GraphDraft &`，三个数当场回到 66 / 535 / 3 / 6。**规矩立在这一笔，第一次被用到在另一条线，两边记在同一个号下（它改成 §18，内容一句没丢）。**

### 17.1 事实源的草稿改从事务取（§17 的下一步）

三处，一处一笔构建：

1. **`handle.cpp#createInTransaction` 同时设两个指针**：草稿从它所属的事务里拿到（`&transaction.graph()`，**不写类型名**）。这样"删掉 `draft_`"就变成**只改读端**，而不是一次改掉每一个指名草稿的 handle。
2. **`module.cpp` 的条件参数**新增 `nodeHandleOf(writings, ref)`：writings 有事务就 `createInTransaction`，没有（测试自建草稿）才 `createInDraft`；两处调用点（普通参数、`const` 的隐藏节点参数）都走它。
3. **`fact.cpp:825`**：`const auto *draft = transaction != nullptr ? &transaction->graph() : first->draft();`——**字节是同一份，改的只是"说谁拥有它"**。

**账本**：references **535 → 534**（`fact.cpp` 那行不再写 `GraphDraft`），已 `--record` 锁进基线；files 66 / constructions 3 / decode 6 未变。**这是这一层第一次出现"消点"而不是"涨点"之外的动作：数字往下走了一格。**

## 18. 这条规矩在 b4 之外的第一次应验（另一条线记，协调者留在账本里）

`src/passes/opt/defun/defunctionalize.cpp` 新增了一处写图的 helper：一个遍第一次自己造一张图（id 取 `Context::graphIds`、一个 `PORT`、直接调用、`exit`、`encode`／`adoptGraph`）。它先写成 `GraphDraft &draft = transaction->edit();`，账本当场红：files 66 → 67、references 535 → 536。

helper 只借一次草稿、并不持有它，所以改成收模板参数（`template <class Draft>`，调用点传 `transaction->edit()`），三个数回到 **66 / 535 / 3 / 6**。

这不是"台账省一格"的老故事，是 §15 的规矩第一次在 b4 之外被用到：**点名就是宣告所有权**，借的一次写 `auto` 或写模板，所有者留在事务那里。数没动，所以不记新行，只记这条用例——下一笔若有人在遍里直接写 `GraphDraft`，先问它是不是真的持有。

## 19. 读端三处：每一处都改问事务（每处一笔、每处构建一次）

| 笔 | 改了什么 | references |
|---|---|---|
| `eb20adf78` | `read.cpp#viewOf`：先问 `handle->transaction()`，草稿从 `&transaction->graph()` 拿，`handle->draft()` 只作退路 | 534 → **533** |
| `997e712b6` | `read.cpp#nodeHandle(from, node)`：`from` 有事务就 `createInTransaction`，没有才 `createInDraft` | 533 → **532** |
| `092196630` | `read.cpp#calleeKernel`：判"这是不是草稿的节点"先问 `transaction()`，再退到 `draft()` | 532（不变） |

**掉的两格是哪两处**：都是 `const GraphDraft *draft = …` 那一行——`viewOf` 一处、`nodeHandle` 一处改成 `const auto *`，因为草稿的来源变成 `&transaction->graph()`，**类型名不再需要写**。第三处只把"问谁"的先后换了、没有可删的类型名，所以数不动：**"改一处就该掉一格"不是必然，问的顺序变了而类型名还在的，那格不算收益。**

**第三笔顺带关掉一条缝**：`calleeKernel` 原来只问 `draft()`，一旦某个 handle 只有事务、没有草稿指针，它会把草稿节点当成已发布的图交给 `nodeOfFrozen`；现在先问事务。

## 20. 第二条顺序规矩（可引用）

> **先让新的事实到达读端所读的那个字段，再改读端。**

两次实测：§16（生产者先把事务送进 handle，读端后改）与 §17.1（`createInTransaction` 同时设两个指针）都是这条的应用；反过来就是 §16 那颗地雷。配套的第一条（另一条线实测）是**新造图先 `setId` 再 `encode`**。两条都属于"顺序错了会静默错、不会编译错"，所以抄在同一个地方。

## 21. `read.cpp` 的 `->draft()` 归零：数没动，问题却浮出来了

`1cdeced1`（`read.cpp` 三处全改成只问 `transaction()`）。**references 仍是 532**——三处退路本来就是 `auto`，没有类型名可删，**改的只是"问谁"**。所以这一笔的收益不是"消点"，是**把一条隐含的依赖翻到了明面上**。

**翻出来的是什么**：一个**只指名草稿、不指名事务**的 handle，现在读端当它"在已发布的图里"，会拿 `program_`（空）去 `nodeOfFrozen`。**套件里没有任何路径会这样走到**（所以整闸绿：7927 / 7916 / 0 / 9 / 2），但 `createInDraft` 还在造这种 handle（`module.cpp:258` 的退路、`fact.cpp:422`、`handle.cpp:291` 的 clone）。

**这就是"删 `draft_` 之前要回答的那个具体问题"**：*测试自建草稿的那些构造点，改完之后它们从哪里拿到草稿？* **答案：拿不到。** 它们手里的 `DraftWritings` 没有事务（`src/passes/test/writings.cpp`、`saturate.cpp` 用收 `GraphDraft &` 的旧构造），而 `rules.cpp:72` 虽然从 `probe.edit(...)` 拿到的是事务里的草稿，**但也是走草稿构造进去的**，所以 `writings.transaction()` 为空，`nodeHandleOf` 只能退回 `createInDraft`。

**结论：还缺一个"无事务写入"的合法裁定**，三条路：
- **A**：承认它就是非法——把 `createInDraft` 与 `Handle::draft_` 一起删掉，测试改用事务（`rules.cpp` 已有事务可用，`writings.cpp`／`saturate.cpp` 需要给它们一个 `RuntimeGraphDraftSession`）；
- **B**：承认它合法——读端必须**同时问事务与指针，任一为真即是草稿**（协调者那条规矩），此时 `->draft()` 会回到 3；
- **C**：让 `Writings` 在没有事务时**自己开一个**（拥有型），`createInDraft` 随之消失，测试不用改。

**我停在这里，等裁定。** 在裁定之前不动 `handle.h` 的 `draft_`——按 §20，删字段是"删用法之后"的事，而用法的合法性还没有结论。

## 22. A 落地：`draft_` 与 `createInDraft` 一起消失

裁定是 **A**（C 被否：*"自己开一个事务"意味着有一张图永远不会被 commit，那正是 `draft_` 今天在做的事*）。

| 笔 | 做了什么 | 四个账本数 |
|---|---|---|
| `404c77db4` | 17 处收草稿的 `DraftWritings` 构造改收事务（测试 10／3／2 + `extractWritings` 1） | 66 / 532 / 3 / 6 |
| `11e83bec5` | `graphWritings` 转储：在 decode 副本上开事务、**永不 commit**，注释写明"这张图是副本，事务在这里只是写作面" | 66 / **530** / 3 / **5** |
| 本笔 | 删 `Handle::createInDraft`、`draft_`、`draft()`、构造第 3 个参数、`class GraphDraft` 前置声明；`DraftWritings` 只剩收事务的构造，`transaction()` 返回引用（永不为空）；`fact.cpp#Source` 由草稿指针改为事务指针 | **64 / 521 / 3 / 5** |

**数字怎么掉的**：`files 66 → 64` 是 `handle.h` 与 `handle.cpp` 里再没有 `GraphDraft` 这个词（`createInDraft` 的声明与定义各带走一处）；`references 530 → 521` 是 `draft()`、`draft_`、`fact.cpp#Source` 的八个分支一起走。**同一棵树上一笔 -9 references、-2 files，是今晚涨落最大的一笔。**

**"无事务写入"这种形态现在不存在了**：`DraftWritings` 只认事务、`Handle` 只认事务、`createInDraft` 没有调用者。§21 问的那个问题（*测试自建草稿的构造点改完从哪里拿草稿*）的答案是**它们不再需要草稿**——测试改成开事务，模拟的正是真实世界里写图的样子。

**§21 那条更正到此生效**：判"是不是草稿"**只问事务**。§21 里 B 的版本（同时问事务与指针）**只适用于两种形态并存的时期**；A 落地后它不再成立。

**"删得干净"的判据是这一句，不是"测试全绿"**：*`createInDraft` 零调用者，所以删得干净。* 套件全绿只能说明没人踩到，零调用者才说明这条路不存在了。

## 23. 收尾清扫：`read.cpp` 不再提这个类型，账本一格没白掉

**判据提前写在前面（这一笔的预期是"账本数大概不动"）**：这三处本来就拿着事务，只是写法还是老的——所以掉数不是这一笔的目的，**写法和事务一致**才是。

| 改了什么 | 为什么现在能改 |
|---|---|
| `NodeView` 从"借一个 `const GraphDraft *`"改成"借事务"，`draft()` 访问器返回 `transaction->graph()`（`auto` 推导） | 视图的唯一来源就是事务；**`read.cpp` 里 `GraphDraft` 的出现次数 4 → 0，整个文件从账本里消失** |
| `is_written_slot` 的缓存键值：`GraphDraft::decode` → `GraphTransaction::decode` + `.edit()`，注释写明"副本、永不 commit" | 和 `graphWritings` 同一种形态；decode 点 5 → 4 |
| `fact.cpp` 删掉已无用的 `using camel::runtime::GraphDraft;` | 上一笔把 `Source` 改成事务之后它就没人用 |
| `txn.cpp#staticValue` 收 `const Draft` 模板参数 | 它只是借读，不该在签名上宣称拥有 |
| `is_anchor` 里 `const GraphDraft &draft = *node.draft;` → `const auto &` | 借读用 `auto`（§15 的老规矩） |

**四项实测：62 / 515 / 3 / 4**（files 64 → 62，references 521 → 515，decode 5 → 4）。

**"还有没有可删的调用点"——在今晚动过的范围里是 0**：`handle.h`／`handle.cpp` 0、`read.cpp` 0、`fact.cpp` 0、`module.cpp` 0。**`modules/graph/txn.cpp` 还剩 2 处，都不是台账住户**：一处是 `using`，一处是事务 API 自己的返回类型 `std::pair<GraphDraft &, gc_node_ref_t>`——事务的墙（§0 第 1 条）本来就不在账里。剩下的 `GraphDraft` 全在 `draft*.cpp` 与各 pass 里，那是**这个类型本身该在的地方**，不是"直连"。

## 24. 裁定改口径：C 落地，账本只允许一次上涨

> **"终点那句话从此是可检查的，不是叙述"**——`check.mjs` 的 `ZERO_FILES` 直接查那五个文件，与四个总数无关。这是账本工具化最好的一次：叙述会漂，断言不会。
>
> **"`--force: recording an increase; the diff of baseline.json says so`——涨在 diff 里看得见，不是偷偷记的"**——这是"允许一次"与"默认放行"的分界线：前者要一条命令、一次 `--force`、一份可见的 diff，后者只需要谁都不看。

**作者 18:15 的裁定**：*"可编辑的图只存在于事务里。所以 B 不选，读端不允许'同时问事务和指针'。实现上取 C：`Writings` 没拿到外部事务时，就自己开一个并拥有它，`createInDraft` 随之删掉，测试一行不用改。"*

**落地**（`1e70b99ac` + `01ea89d6f`）：`GraphTransaction::borrow(GraphDraft&)` 开一个**借来的**事务（自己不拥有草稿，草稿本来就有主）；`DraftWritings(GraphDraft&)` 内部持有它，**生命周期到 `Writings` 析构**；那 17 处调用点**逐字还原**（`git revert 404c77db4`）。`graphWritings` 的 (a) 保留。

**这是唯一一次允许上涨：C 的入口必须命名它收的类型（`writings.h:411`、`writings.cpp:431` 两行）。上涨是裁定的代价，不是漏出去的洞。**

**终点改成可检查的形状（原先那句"三个数归零、删掉 `GraphDraft`"到此作废）**：

> **`handle`／`read`／`fact`／`module` 侧归零；`writings.h` 留一个入口，因为它必须命名收进来的类型。**

`check.mjs` 现在**直接查这件事**（`ZERO_FILES`）：`modules/graph/handle.h`、`handle.cpp`、`modules/graph/read.cpp`、`modules/rewrite/fact.cpp`、`modules/rewrite/module.cpp` 五处，任何一处出现这个类型名即红，与四个总数无关。

**A 那半边照旧成立，不因为改成 C 就说回去**：`createInDraft` 零调用者、`Handle::draft_` 已删、`nodeHandleOf` 单形态。

## 25. b4 的 A：13 处"从 program 造 handle"问完，答案是三层

问的是**"读一个已发布的程序，造 handle 要不要事务"**。13 行逐个答，答案不是两种而是三种：

| 类 | 处数 | 位置 | 要不要事务 |
|---|---|---|---|
| **① 已发布的程序** | **9** | `read.cpp:158/162/172/570/742/796`、`txn.cpp:393`、`fact.cpp:423`、`module.cpp:1555`（其中 1555 与 404/438 一类，见下） | **不要**：图不再变 |
| **② 根本无图** | **3** | `read.cpp:404`（Op）、`read.cpp:438`（TypeRef）、`module.cpp:1555`（Op） | **不需要**：没有图可谈 |
| **③ 草稿里的新节点** | **2** | `txn.cpp:350`（常量折叠）、`txn.cpp:440`（`add_constant`） | **要**：节点只存在于草稿里 |

**可检查的一句话**：*不是"不要事务"，是"两种都有"——9 行已发布、3 行根本无图、2 行未发布。*

**第 ③ 类这一笔就修了（`82c8be1ee`）**：两个内核手里本来就有 `state.session` 与 `session.edit(graph)`，所以不是加管道，是**把已有的东西接上**——返回值改 `createInTransaction`。

**然后加了一条可执行的自律，不然分类法只是自律（`a985ea51c`）**：`Handle::create` 在 `kind == Node && program != nullptr` 时要求 `(graph, node)` 真在 program 里（用现成的 `program->graph()` 与 `containsGraphNode`），**错了就在造 handle 的地方炸**，不等某个读者拿空 `program_` 去查。触发它的是一条只读自测 `graph::probe_handle`（`probe.cpp` + `probe_handle.cml` + 计划里的 `runtime.graph_draft_ledger.handle_invariant`），读数：
```
HANDLE_PROBE allowed a node that is there: yes
HANDLE_PROBE refused: InvalidArgument
```
**第②类是"无图"不是"未发布"，这一点写在这里**：Op／TypeRef handle 的 `program` 是 `nullptr`、图 id 是 `Invalid`，它们根本不命名图，所以在这个断言里必然放行——放行是对的。

**断言一开就抓到了第 14 个红（`4a7892476`）**：`graph.replace` 的守卫问替换节点要 `program`，而折叠刚写进草稿的节点现在用事务命名，于是 14 个折叠用例当场全红。守卫改成"两种命名法都认"（`to->program() == state.base` 或 `to->transaction()->id() == to->graph()`）。**这就是断言的价值：它抓的不是我改的两行，是我没想到的第三个地方。**

### 25.1 那条红立成一条规矩（B 的每一步都按它判）

**守卫不能靠 `program` 认图。** 一个节点在某张图里，凭据是它自己的 `(graph, node)`，不是"我手里这张 program 有没有它"。**用 `program` 认图，等于假设节点都已发布**——折叠写进草稿的节点一出现，这个假设就破了。

**证据就是上面那次的读数，原样一句话**：*它没抓我改的那两行，抓的是我以为无关的第三个地方。* 14 个用例、一个守卫、一个假设。

**于是 B 里凡是"这个位置手里只有 program"的，先问一句：它是在认图吗？**

| 判断 | 做法 | 计数 |
|---|---|---|
| **是认图** | 改成从节点自己的字段取（`handle->graph()`、`handle->transaction()`、草稿的 `containsNodeRef`） | **改掉的** |
| **确实需要 program** | 保留，并就地写一句为什么（要遍历已发布的图、要给读者一张冻结的快照） | **保留的** |

两类分开计数，是这一步的账。

## 26. B 的第一片：`autodiff/pullback.cpp`，从 37 处到 34 处

**先记一句上一轮的结论，它把分类法变成了判据**：**① 类 = 没有草稿参数的位置，③ 类 = 有草稿参数且不属于事务的位置**。所以"B 的第一片要删掉多余的草稿参数"在 ① 类里量出来是 **0 行、0 个局部**——那 9 行之所以是 ① 类，正是因为没有草稿能到那里。

**这一片动的不是参数，是"认谁"**：`regionOwners_` 这张表记的是"某个 region 的来路里带进了哪些函数"，**键原来是草稿的地址**。草稿只能从事务到达，**所以键改成事务**（`0ecc12501`）：

| 数 | 值 |
|---|---|
| 删掉的行数 | **0**（改的是三行：两个簿记函数的参数、一处表类型） |
| 删掉／改写的参数与局部 | **2 个参数**（`ownersOf`、`noteArmOwner` 的第一个参数）+ **1 处表键类型** + 2 处调用点 |
| 本来就需要草稿而保留的点 | **34 处**，全部是真实用法 |

**为什么是 0 行**：这个文件里没有一处"多余的草稿参数"——每一处都是**读一个节点、写一个节点、或取一个图 id**。真正的删法是另一件事：**那些只被 `draft()` 调用的自由函数，本来该是 `PullbackContext` 的成员**，转成成员后签名里那个 `const GraphDraft &draft` 就消失了（对象自己知道它的草稿）。改法不是"删参数"，是"换归属"——那是下一片的事，不是这一片。

**四个数：62 / 514 / 3 / 4**（`517 → 514`）。

## 27. B 第二片：那张表只放行 5 行，不是 18 行

**判据就是 §26 那句**：*只被 `draft()` 调用的自由函数，本来就该是成员的*。按它把 `pullback.cpp` 的 28 个收草稿的自由函数逐个过一遍，**只有 5 行满足**（`4eb159b5a`）：

| 函数 | 原参数 | 转成员后消失的参数 | 调用者数 | 全部传 `draft()`？ |
|---|---|---|---|---|
| `readsCallVersion` | `const GraphDraft &draft` | 这个 | 2 | **是** |
| `componentProjectionOf` | 同上 | 这个 | 1 | **是** |
| `orderingOf` | 同上 | 这个 | 2 | **是** |
| `takeSyncPlace` | `GraphDraft &draft` | 这个 | 3 | **是** |
| `evidentFunction` | `const GraphDraft &draft` | 这个 | 3 | **是** |

**为什么其余 23 行不转——它们不是"多传东西"，是"传的东西不一样"**：同一个 helper 被**三种草稿**调用——重写自己的 `draft()`、反向传播的 `forward()`（`Backward::forward`，1051）、反向传播的 `target()`。`calleeOf` 四传 `draft()`、两传 `forward()`；`valueNode`、`portsOf`、`externalInputsOf`、`staticValue`、`addPort`、`setResult`、`pruneUnreachable` 同样跨着好几个草稿。**转成某一个对象的成员，就等于替另一个对象认错了图**——这正是 §25.1 那条规矩的另一面。它们该去的地方是**事务**（谁拥有这张图），不是某一个重写器；那要动 `GraphTransaction` 的接口，属于下一层。

**三个数**：删掉的行数 **0**（每个函数是搬进类里，不是删掉）；消失的参数 **5 个**；保留 **23 个**（每个都是一句"它服务的不止一张图"）。

**四个数：62 / 509 / 3 / 4**（`514 → 509`）。

## 28. B 第三片：那 23 个的边界在"调用点上手里有没有事务"

**判据（这一层的那句话，写在这里）**：*同一个 helper 被三种草稿调用——重写自己的 `draft()`、反向传播的 `forward()`、`target()`。转成某一个对象的成员，就是替另一个对象认错了图。**它们该去的地方是**事务**（谁拥有这张图），不是某个重写器。***

**于是表问的是另一件事**：不是"谁是它的主人"，而是**"它的调用点上，手里有没有事务"**——有就换成收 `const rt::GraphTransaction &`，没有（只拿到一个 draft 参数的）就得先转那个自由函数。

| helper | 今天被哪些草稿调用 | 能从事务到达 | 本片 |
|---|---|---|---|
| `staticValue` | `draft()`、`forward()`、`target()` | **是** | **改** |
| `calleeOf` | `draft()`、`forward()` | **是** | **改** |
| `externalInputsOf` | `draft()`、`forward()` | **是** | **改** |
| `portsOf` | `forward()` | **是** | **改** |
| `addCall` | `target()` | **是** | **改** |
| `typeOf` | `draft`、`draft()`、`forward`、`forward()`、`liftedDraft`、`target()` | 否 | 留 |
| `kindOf` | `draft`、`draft()`、`forward()`、`target()` | 否 | 留 |
| `regionMember` | `draft`、`draft()` | 否 | 留 |
| `valueNode` | `draft`、`forward` | 否 | 留 |
| `hasOrdering` | `draft` | 否 | 留 |
| `addAccs`／`addField`／`addOper`／`addPort`／`addAggregate`／`addFill` | `draft`、`draft()`、`target()`、`pullback` | 否 | 留 |
| `setResult`／`pruneUnreachable` | `draft`、`forward`、`pullback` | 否 | 留 |
| `addLoopInt`／`stampLoopOrigin`／`addZeroTangent`／`addTangentSum`／`buildReverseCond` | `draft` | 否 | 留 |
| `addClosure` | `engine_.group()` 与 `draft()`／`forward`／`draft` | 否 | 留 |

**"否"的那 18 行是哪一种**：它们都经**自由函数的 `draft` 参数**到达——`buildReverseBody`、`buildReverseClosure`、`packState` 那一族。**它们的调用点上没有事务，只有草稿**；要转就得把那一族一起转，而那一族目前的调用者里还有人只传草稿。**这就是这次重构的边界，写在这张表里，不是留在脑子里。**

**三个数**：删掉的行数 **10**（净 `-10`）；**消失／换掉的参数 5 个**（五处 `const GraphDraft &draft` / `GraphDraft &draft` → 所有者）；**保留 18 个**。

**四个数：62 / 504 / 3 / 4**（`509 → 504`）。

## 29. `generic` 这一族：深度是 3，而且**入口的 `std::function` 类型是原子**

**判据沿用 §28 那条**（可检查）：**"能不能在**调用点上**拿到事务——不是'谁是它的主人'。"**

**这一族长什么样**：`generic` 里所有收草稿的函数，**调用点的第一个实参全都是同一个标识符 `draft`**——一个从入口一路传下来的名字。链条只有一条：

| 层 | 谁 | 这一层手里有没有事务 |
|---|---|---|
| **0 入口** | `rewriteReachableGraphs`（`rewrite.cpp:457`） | **有**：`session.edit(g)` 就在调用点上 |
| **1 回调** | `std::function<size_t(GraphDraft &)>`，六个 lambda（`generic.cpp:1465/1491/1503/1527/1543/1577`、`algebra.cpp:276`） | 没有，参数是草稿 |
| **2 pass 函数** | `algebraDraft`、`cseDraft`、`dceDraft`、`reduceControlDependencies`、`reduceOrderDependencies`、`reduceDocumentOrdering` | 没有，同上 |
| **3 叶子** | `operUriOf`、`operIdOf`、`isPureOper`、`isValueOnly`、`isDeadWhenUnread`、`isReplaceable`、`reaches`、`orderedSources`、`readsNoWrittenSlot`、`replaceNode`、`markSyncStatement`、`handOverSyncWait`、`gateInputsSurvive`、`staticValueOf`、`isFoldable`、`staticArgs`、`staticArmOf`、`branchArmNodes`、`joinBlockFragments`、`pruneBranch`、`foldStaticAccess`、`bypassValueGate`（22 个） | 没有，同上 |

**"有多深"的答案：一个叶子要变成收事务，得改三处签名（0→1→2→3）**；**但真正的原子是第 0–1 层**：`std::function<size_t(GraphDraft &)>` 这个类型不在一个函数里，它决定**六个 lambda 全部**的写法。**所以这一族不能按"每片 5–8 个函数"切**——切法只能是**按分支**，每个分支都要付一次"入口 + 回调类型"的共同成本。

| 分支 | 内容 | 叶子数 |
|---|---|---|
| **A. 代数** | `algebraDraft` + `operUriOf`、`isPureOper`、`replaceNode`、`markSyncStatement`、`handOverSyncWait`、`gateInputsSurvive` | 6 |
| **B. 公共子式与死代码** | `cseDraft`、`dceDraft`、`foldStaticAccess` + `isValueOnly`、`isDeadWhenUnread`、`isReplaceable`、`reaches`、`orderedSources`、`readsNoWrittenSlot`、`pruneBranch`、`joinBlockFragments` | 8 |
| **C. 顺序去重** | `reduceControlDependencies`、`reduceOrderDependencies`、`reduceDocumentOrdering`（在 `src/runtime/control.cpp`，被同样的 lambda 调用） | 0 |

**A 与 `pullback` 那 18 行是同一族**：形状一样（草稿作为自由函数的参数一路传下来，入口处本来就有事务），**只是两处实例**——`generic` 的 `draft` 与 `pullback` 的 `draft`/`forward`/`pullback`。**表能看出它们是同一族：判据一样、链条一样、原子一样。**

**三个数（这一片未动代码）：0 / 0 / 28（保留，等分批）**。**四个数：62 / 504 / 3 / 4（未变）。**

## 30. 那一族的原子换了形状（`71151fc20`）

**只改类型与签名，一条规则体都没动，一个叶子都没搬**：

| 位置 | 改前 | 改后 |
|---|---|---|
| `rewriteReachableGraphs` 的入口（`rewrite.cpp:457`） | `rewrite(session.edit(g).edit())` | `rewrite(session.edit(g))` |
| 回调类型（`rewrite.h:218`、`rewrite.cpp:459`） | `std::function<size_t(GraphDraft &)>` | `std::function<size_t(camel::runtime::GraphTransaction &)>` |
| 六个 lambda（`generic.cpp` 四处、`finish` 一处、`algebra.cpp:277`） | `(GraphDraft &draft)` | `(GraphTransaction &transaction)` + 首行 `auto &draft = transaction.edit();` |
| 第 2 层三个 pass 函数（`algebraDraft`、`cseDraft`、`dceDraft`） | `(GraphDraft &draft)` | `(GraphTransaction &transaction)` + 同一行 |

**用 `edit()` 不是 `graph()`**：`graph()` 是 `const GraphDraft &`，而这几个 pass 函数要改图——**这正是"所有权"在类型上的第一次显形**：写的人要拿 `edit()`，读的人拿 `graph()`。编译器的第一轮报错（`setNodeAttrs` 作用在 `const GraphDraft` 上）就是这个区分的证据。

**验收（可检查的一条）**：`git grep -n "std::function<size_t(GraphDraft &)>" -- src` → **无输出（exit 1）**。**"类型没了"比"能编过"更能说明这一笔真的改了形状。**

**三个数**：**消失的参数 13 个**（11 处签名／lambda 的 `GraphDraft` 参数 + 入口两处 `.edit()` 的二次解包）；**删掉的行数 0**（净 `+15`，都是 lambda 首行的绑定与折行）；**保留 28 个**（叶子一个没动）。

**四个数：62 / 491 / 3 / 4**（`504 → 491`）。

**这条数不只是「引用数」，还是「同一件事被拼写几次」**（2026-10-31 实测）：同一条谓词，如果写成
`bool nodeTakesNoOrder(const GraphDraft &draft, gc_node_ref_t id)` 这样的自由函数，声明加定义就是**两处**
新的类型拼写，账本立刻 `+2`；把它做成 `GraphDraft` 的成员（`bool GraphDraft::takesNoOrder(gc_node_ref_t) const`），
函数体里用 `this`、参数表里不再出现类型名，定义处仍有一处 `GraphDraft::` 限定，于是只 `+1`，从基线 515 起到 516
就是那一处。**所以这一层要降的不只是「谁还连着它」，还有「同一句话被写了几遍」**：一句判据只该有一个拼写，
这也是把谓词收成成员的第二个理由（第一个是写与判同义）。

## 31. A 分支：两个叶子转了，四个被 B 挡住——**这一片的结论是顺序，不是数量**

**转了的两个**（`33c185186`）：`operUriOf`、`gateInputsSurvive`。两个都是纯读，所以体内是
`const auto &draft = transaction.graph();`——**`graph()` 是读的那一半，`edit()` 是写的那一半，这一片第一次按读写分开取。**

**四个被挡住的，各自在声明处留了一句"被谁挡住"**：

| 叶子 | 挡住它的是谁 | 它为什么挡 |
|---|---|---|
| `isPureOper` | `foldDraft`（B） | 它把收到的草稿原样传下去 |
| `markSyncStatement` | `handOverSyncWait`、`replaceNode` | 两个调用方都收草稿 |
| `handOverSyncWait` | `replaceNode`、`bypassValueGate`（B） | 同上 |
| `replaceNode` | `replaceFoldResult`（B）、图模块的 `replace` | 事务在它们外面一层 |

**所以 A 不是"做完 6 个"，是"做完 2 个、量出 4 个的依赖"**：**这四个叶子的入口全在 B 的 pass 函数里**——
`foldDraft`、`replaceFoldResult`、`bypassValueGate`。**这说明族的真实顺序是 B → A 的剩余 → C**，
不是表上原先写的 A → B → C。**表又一次自己改了答案**：分支 A 不是最先能做的一层，**它是最先能"读"的那一层**。

**三个数**：**消失的参数 2 个**（`const GraphDraft &draft` ×2）；**删掉的行数 0**（净 `+7`：多出的三句挡路注释、两个 `const auto &draft` 绑定）；**保留 26 个**。

**四个数：62 / 488 / 3 / 4**（`491 → 488`）。

## 32. B 分支：`draftOf` 也交了——**一个函数的返回值，决定了四个叶子能不能转**（`c336ad4dc`）

**B 转了 11 处签名**：`bypassValueGate`、fold 一族的六个（`foldDraft`、`foldStaticAccess`、`foldStaticFill`、
`foldStaticCast`、`foldFromTypes`、`replaceFoldResult`）、以及 A 留下的四个叶子（`isPureOper`、
`markSyncStatement`、`handOverSyncWait`、`replaceNode`）。**外加 algebra 里被 `replaceNode` 与 `isPureOper`
牵动的两处（`simplify` 与那次 `replaceNode` 调用）——它们不在表上，是编译器点名的。**

**真正的发现不在 `generic` 里，在 `modules/graph/txn.cpp`**：`replaceNode` 的最后一个外部调用者
是 `graph.replace`／`graph.erase`，而它们手里只有一个**裸草稿**——因为 `draftOf` 的返回值就是
`std::pair<GraphDraft &, gc_node_ref_t>`。**一个辅助函数的返回类型，卡住了另一个模块的重构**：

| | 改前 | 改后 |
|---|---|---|
| `draftOf` | `std::pair<GraphDraft &, gc_node_ref_t>` | `std::pair<camel::runtime::GraphTransaction &, ...>` |
| 三个调用点 | `auto [draft, ref]` | `auto [transaction, ref]` + 一行 `auto &draft = transaction.edit();` |

**这不是顺手的清理，是判据的必然**：`draftOf` 的注释本来写着"返回草稿"，而 `Handle` 那边已经裁定
"草稿里的节点只能由事务命名"。**两句话对不上，编译器就把它们撞在一起了。**

**`test::control` 的门自检也一样**：它原来 `bypassValueGate(draft, ...)`，可它上面两行就是
`auto owner = GraphTransaction::open(...)`——**事务一直在手边，只是没交出去。**

**三个数**：**消失的参数 11 个**；**删掉的行数 12**（净 `+11`：十处 `auto &draft = transaction.edit();` 与一行 `const auto &draft = transaction.graph();`，抵掉旧签名折行的行数）；**保留 15 个**（`armRegions`、`staticValueOf`、`isFoldable`、`staticArgs`、`staticArmOf`、`branchArmNodes`、`joinBlockFragments`、`pruneBranch`、`operIdOf`、`isValueOnly`、`isDeadWhenUnread`、`isReplaceable`、`reaches`、`orderedSources`、`readsNoWrittenSlot`——都是被 B 的函数用草稿调用的读，**它们的调用点现在手里是事务，但转它们要连这些读的调用者一起看，留给下一片**）。

**四个数：61 / 472 / 3 / 4**（`488 → 472`，**files 首次降到 61**：`gates.cpp` 不再拼那个类型名）。

## 33. 那 15 个读：一片带走，而且**"读"和"写"在这一层第一次分家**（`6aa9ef309`）

**15 个全部转了**，写法按它们对图能做什么分两种：

| 一种 | 谁 | 体内第一行 |
|---|---|---|
| **只读**（13 个） | `operIdOf`、`isValueOnly`、`isDeadWhenUnread`、`isReplaceable`、`reaches`、`orderedSources`、`readsNoWrittenSlot`、`armRegions`、`staticValueOf`、`isFoldable`、`staticArgs`、`staticArmOf`、`branchArmNodes` | `const auto &draft = transaction.graph();` |
| **会写**（2 个） | `joinBlockFragments`、`pruneBranch` | `auto &draft = transaction.edit();` |

**这不是风格，是参数表在说它能做什么**：一个只读的 helper 收 `const GraphTransaction &` 就**拿不到可写草稿**，
编译器替"这个函数不该改图"做了担保。**今晚第一次，判据从注释搬进了类型。**

**又没有在新模块里踩坑**，因为 B 那一笔已经把它们都解决了：图模块的 `replace`／`erase` 手里是 `draftOf` 现在返回的事务，
`inline` 的 `inlineCallInOrder` 自己的参数表里就写着 `GraphTransaction &transaction`。**"先报再改"这一条没有触发——三个外部调用点全都是"事务本来就在手边"。**

**三个数**：**消失的参数 15 个**；**删掉的行数 15**（净 `0`：进 89 出 89，签名换行与绑定行正好抵平——**这一片是纯形状，行数不动**）；**保留 0 个**（`generic` 这一族到此清空，只剩 C（`runtime/control.cpp` 那三个）与 `rewrite.h` 里两个 inline 的边界）。

**四个数：59 / 450 / 3 / 4**（`472 → 450`，**files 61 → 59**：`algebra.cpp` 与 `gates.cpp` 一起不再拼那个类型名）。

## 34. C：两个模块，同一个问题，两个已经对上的答案（`2b8f372cf`）

**`control.cpp` 这半小时没动**（最后一次是 04:05 的 `5d4231797`，比动手时早一个多小时），所以按约定做了。

三个函数转了签名，**它们看什么边一点没改**：`edges` 的 ②a（"pass 该不该继续造控制边"）还没定，
所以在 `reduceOrderDependencies` 上留了一句：*"它仍然读控制等待：一个 pass 能不能干脆不再造它们这件事（edges 2a）还没定，
在那之前，一个忽略它们的归约会去回答一个关于"编译器并不构建的图"的问题。"*

**这一片的证据不在 `control.cpp`，在两个调用者身上**：

| 调用者 | 它手里原本有什么 | 改了什么 |
|---|---|---|
| `generic` 的两处 lambda | 事务（B 之后） | 传 `transaction` |
| `src/passes/trans/json/gir_canonical.cpp:237` | **两行之上刚 `GraphTransaction::decode(program_, graph)`** | 传 `*transaction` |
| `src/passes/test/control.cpp`（18 处） | 每个用例函数第二行就是 `auto owner = GraphTransaction::open(...)` | 传 `*owner` |

**没有一处需要新造事务**。**"调用点上能不能拿到事务"这条判据，到现在为止的答案是：能——只要那个模块已经按自己的方式回答过一次。**

**还剩几个（"该不该收尾"的数）**：

| 位置 | 剩下 | 是什么性质 |
|---|---|---|
| `src/passes/opt/generic/` | **3** | `rewrite.h:26` 的 `using`、`rewrite.h:34` 的 `isComputedAfter`（inline 读）、`algebra.cpp:154` 的 `constant`（模板读） |
| `src/runtime/control.cpp` + `control.h` | **10** | 3 个导出 API（`hasControlDependency`、`controlFrontier`、`appendControlDependency`）+ 内部 4 个 + 头文件里的声明 |

**这 13 处和前面 400 多处不是同一种**：它们全是**读谓词与内部实现**，而且**都在"已经收事务的函数"体内被调用**——
转它们是"再往里一层"，收益是拼写数，不是判据。**下一片做不做，由你按"还能不能给出一句新判据"决定。**

**三个数**：**消失的参数 3 个**；**删掉的行数 3**（净 `+5`：三行绑定 + 那段"仍然读控制边"的注释）；**保留 13 个**（上表）。

**四个数：59 / 444 / 3 / 4**（`450 → 444`）。

## 35. 收尾扫描：那 13 处不是"拼写数"，是**13 个删不掉的理由**（`46a30576e`）

**判据的对象换了**：这一族的目标不是少写字，是**最后能删掉 `GraphDraft` 本身**——所以
**"能删掉几行"在这里读成"能删掉几个 `GraphDraft &` 参数"**：**每一个还在收它的参数，都是一个删不掉的理由。**
这不是把标准放宽，是把标准的**对象**从行数换成了**参数**。

**13 处全转，0 处拿不到事务。** 而且有三处把同一个问题往外推了一跳，**每一跳都早就有人回答过**：

| 那一跳 | 它本来就有 |
|---|---|
| `builder_control.cpp` / `builder_operators.cpp` | **`nodeTransactionOf`**，就写在 `nodeDraftOf` 旁边，注释原话：*"the transaction the node's graph is built in … the identity two nodes of one graph agree on"* |
| `draft_inline.cpp` 的拼接例程 | `inlineCallableInDraft(session, GraphTransaction &transaction, …)` 的参数表 |
| `test/control.cpp` | 每个用例第二行的 `GraphTransaction::open(...)` |

**额外带出来的三个**（不在那 13 里，是编译器点名的下一跳）：`bindPortValueUses`、`keepCalleeOrdering`、
`devirtualizeClosureCallInDraft`——**它们都在 `draft_inline.cpp`，上游都是已经收事务的函数。**

**"没有一处需要新造事务"**——**这句话是整场迁移的总答案**：*"调用点上能不能拿到事务"这条判据，
到现在为止的答案是：能——只要那个模块已经按自己的方式回答过一次。*

**这一族的账**：`generic` 与 `control` 两个模块里**再没有一处参数写 `GraphDraft`**。

**三个数**：**消失的参数 16 个**（13 + 额外三跳）；**删掉的行数 16**；**保留 0 个**。

**四个数：57 / 432 / 3 / 4**（`444 → 432`，**files 59 → 57**：`control.cpp`、`control.h` 连拼写都没了）。

## 36. 族谱：b6 还差多远，用**还能收草稿的参数**报（不是 `references`）

**判据要换对象**：*b6 的进度不该用 `references` 这个数报，应该用**还能收草稿的参数数**报——因为目标是删掉类型，
而类型只能在没有函数收它的时候被删。*

**全仓 444 处拼写里，174 处在参数位置**（其余是 `using`、`#include`、`GraphDraft::` 成员定义、类型自身的声明）：

| 族 | 文件数 | 收草稿的参数 | 其中"类型自己的家／成员定义"（删不掉也不该删） |
|---|---|---|---|
| `runtime/draft*` + `graph_txn` + `writings`（墙） | 19 | 112 | **112**（全是：类型自身的成员与自由函数读它的内部） |
| `compile/gir`（builder 一族） | 3 | 3 | 0（**但是造草稿的地方，要单独一轮**） |
| `modules/autodiff`（`pullback`） | 1 | **23** | 0 |
| `passes/`（`opt` + `test`） | 7 | 12 | 0 |
| `modules/tiles` | 4 | 7 | 0 |
| `core/tags` | 2 | 10 | 0 |
| `modules/onnx` | 1 | 1 | 0 |

**所以"墙外"还剩 56 个参数**，分布在 18 个文件里。**`pullback` 的 23 个是最大的一块。**

**`pullback` 这一片今晚没成，原因是判据在这一族上第一次不够用**：那 23 个参数不是 23 个自由函数的入口，
而是**同一个类（`ForwardRewrite`／`Backward`）的成员方法里的 40 多处调用**，而这些成员同时握着三张草稿
（`draft()`、`forward()`、`target()`）——**"调用点上有没有事务"在这里退化成"这三张草稿各自属于哪个事务"，
而它们属于两个不同的事务（`forwardTransaction_` 与 `targetTransaction_`）。** 我改到一半树不能构建，
按"任何时刻停下树都要能构建"退回了（`git checkout --`，树已重新绿）。**这一片需要一个 A/B：先给每个成员方法标出它读写的是哪个事务，再改签名。**

**三个数（这一片）：0 / 0 / 56**（未动）。**四个数：57 / 432 / 3 / 4（未变）。**

## 37. `pullback` 的 A 表：**三张草稿，两个事务，一个映射**

**表头那句判据**：*一个方法同时握两个事务本身不是错——错的是它把一张草稿上的节点塞进另一张草稿。
"跨事务写"那一列非空的地方，就是要单独想的地方。*

**`ForwardRewrite`：15 个方法，一个事务。** 它只收一个 `rt::GraphTransaction &transaction_`，
`draft()` 就是 `transaction_.edit()`；**没有任何方法同时握两个**：

| 方法 | 用哪张草稿 | 事务 | 跨事务写 |
|---|---|---|---|
| `followFunctionValues`、`carriesFunctionValue`、`expandKernelBodies`、`run`、`retarget`、`redirectValue`、`redirectCallValue`、`redirectAnchors`、`split`、`dependsOnTangentPort`、`rewriteLoop`、`rewriteIndirectCall`、`rewriteClosureCall`、`rewriteCall`、`rewriteBranch`（15 个） | `draft()` | **`transaction_`（一个）** | **无** |

**`Backward`：35 个方法，两张草稿两个事务。**

| 结论 | 方法数 | 方法 |
|---|---|---|
| **只读前向**（`forward()` → `forwardTransaction_`） | **16** | `accumulate`、`gradient`、`userGradient`、`refuseWhenActive`、`run`、`reportRefusals`、`activeFunctionInput`、`chosenFunction`、`refusalReason`、`whyNotEvident`、`operUri`、`operId`、`checkVariableReads`、`visit`、`visitOper`、`visitAccs` |
| **只写目标**（`target()` → `targetTransaction_`） | **8** | `accumulateInto`、`producerOf`、`add`、`zero`、`materialize`、`visitCallSite`、`visitRuleSite`、`finishCaptures` |
| **两张都不用** | **7** | `primalOf`、`gradientOf`、`accumulateGradient`、`isActive`、`mergeInto`、`refuseActiveCollection`、`refuseActiveEnum` |
| **两张都用（交界）** | **4** | `ref`、`checkGradientType`、`visitFill`、`visitForward` |

**交界处 4 处，但只有 1 处是设计上的跨**：

* `ref(primal)`——**它就是那张映射表本身**，注释写着"Target-graph node holding the primal value"。**唯一一处把前向节点变成目标节点的地方。**
* `checkGradientType`、`visitFill`、`visitForward`——**同时读两个事务，但都不调用 `ref()`**。
  **B 那一轮的第一件事就是核实这三处**：它们若把前向节点直接塞进目标写，那就是这条族真正的风险点；
  若只是比较类型、读两边头部，那就没问题。**表里这一列非空，但还不知道是"跨"还是"看"。**

**B 的做法（随之定）**：`ForwardRewrite` 的方法继续收 `transaction_`；`Backward` 的方法**显式收它真正用的那一个，
需要两个的就显式收两个**（`forwardTransaction_`、`targetTransaction_`）——**不收一个"能拿到另一个"的接口，那是把判据藏起来。**

**三个数**：**0 / 0 / 56**（这一片只出表）。**四个数：57 / 432 / 3 / 4（未变）。**

## 38. 那 3 处核实完了：**都不跨**

| 方法 | 它读 | 它写 | 结论 |
|---|---|---|---|
| `checkGradientType` (`pullback.cpp:3115–3128`) | `typeOf(target(), gradient)`（3117）、`forward().nodeOrigin(where)`（3122） | **两个图都不写**（`failGradientType` 记的是拒绝，不是节点） | **不跨**——只从两边**读** |
| `visitFill` (`pullback.cpp:4186–4236`) | 前向 `payloadOf`／`typeOf`／`withInputsOf`（4187/4189/4199/4222） | 目标 `materializeStaticValue`（4210）、`addOper(target(), …)`（4214），**输入全是目标节点**（`moved.value`、新常量） | **不跨**——前向节点只当 `gradients_`／`active_` 的**键** |
| `visitForward` (`pullback.cpp:4238–4256`) | 前向 `normInputsOf`／`typeOf`（4239/4244） | 目标 `addCastNode`／`setNormInputs`（4251–4252），输入 `g` 是 `gradient()` 给的**目标节点** | **不跨**——`accumulate(source, g)`（4255）把前向当键、目标当值 |

**也没有"顺手拿到另一个事务"的那种**：三个都真的两边都要（读前向、写目标），**签名应当显式收两个。**

**这条族的跨事务写不是"没有"，是只有一个地方，而它被收在一个动作里**：`gradients_` 是**以前向节点为键、目标节点为值**的表，
所以每次 `accumulate(前向, 目标)` 都在"跨"——**但跨的是表的键，不是图的写。**

### `ref()`：为什么必须在这里跨

`Backward::ref` (`pullback.cpp:3020–3045`) 是**全类唯一一处把前向节点变成目标节点的地方**，理由在它的三条出路里：

* 常量：**复制**一份进目标（`materializeStaticValue`，3030）——目标图不能指向前向图的节点，常量也要自己的；
* 残差口（tape 模式）：在目标图里开一个 **Port**（3034–3036），并把它记进 `captures_`——**这个 Port 就是"把前向的一个值带进目标图"的唯一合法形式**；
* 闭包口：同上，开 Port 并挂成闭包节点（3038–3040）。

三种都被记进 `toTarget_`／`toPrimal_`（3042–3043）——**那张表就是两个图之间的翻译表。** **所以"能不能不收两个事务"的答案是：不能。
`ref` 是唯一一处两个事务必须同时在场的地方，因为它做的事就是把一个地址空间的名字翻译成另一个地址空间的节点。** 其余所有方法要么在一边
读，要么在一边写，**它们跨的时候都是经过 `ref()`，而不是自己造一个。**

**B 的顺序**：**3 处核实（本节，已完成，不改代码）→ 16 个只读前向的签名 → 8 个只写目标 → 最后 4 个交界。**

**三个数**：**0 / 0 / 56**（本轮只核实，未改代码）。**四个数：57 / 432 / 3 / 4（未变）。**

## 39. B 第一笔：`pullback` 22 个参数一次转完，**并说明为什么"读批／写批"分不成两笔**（`1ba2a53af`）

**计划与它为什么不成立**：原打算"先 16 个只读前向，再 8 个只写目标"。**做不到**——
`addOper`／`addAggregate` 这些**造节点**的函数，自己要先问 `typeOf`／`kindOf`／`regionMember`（它们能造什么取决于读到了什么），
所以**在一边点名事务，另一边同一句话里也得点名**。**读批与写批在调用图上是一个连通块，不是两层。**
（`ref`／`checkGradientType`／`visitFill`／`visitForward` 那 4 个交界仍可最后做——它们是被调用者，不是调用者。）

**真正需要一次一笔的是 `Backward::run`**：**它的 48 次 `forward()` 一次没动**——那是同一个方法里 48 个"这张图只读"的论据，
不是机械替换。**它留作单独一笔。**（这一笔只改了 `run` 里调用那些已转函数的**实参**，十几行。）

**转的时候必须区分的四个地址空间**（这就是 A 表那张图的用处）：

| 位置 | 事务从哪来 |
|---|---|
| `ForwardRewrite` 成员 | `transaction_` |
| `Backward` 成员读 | `forwardTransaction_` |
| `Backward` 成员写 | `targetTransaction_` |
| 自由函数 | 参数 `transaction` |
| `Engine::build` 里那几处 | `*pending.source`（前向）、`*pullbackOwner`（回传图） |

**三处不是这一族、仍然收草稿的东西**（转的时候被编译器点名，已还原）：
`ungated()`（别的模块的助手）、`LoopDraftView` 的构造（**它是一张草稿的视图**）、`rt::fallBackToCopyingAppend`（运行时入口）。

**结果**：**`modules/autodiff/pullback.cpp` 里 `GraphDraft` 的拼写归零**（`git grep -c -w GraphDraft` 退出码 1）。

**三个数**：**消失的参数 22 个**；**删掉的行数 217**；**保留 0 个**。

**四个数：56 / 408 / 3 / 4**（`432 → 408`，**files 57 → 56**：`modules/autodiff` 整族清零）。

## 40. `run` 那一笔：32 处读，没有一处不是"只读"（`55fe8ccd0`）

**A 表里那个 48 是转换前的数**：`1ba2a53af` 里已有 16 处变成"传给已转函数的实参"，**剩 32 处是 `run` 自己的直接读**，这一笔全收进一个 const 视图。

**逐处看过的结论**：32 处只有六种表达式——`payloadOf`、`normInputsOf`、`withInputsOf`、`armRegionOf`、`nodeOrigin`、`program()`；
另外 6 处 `LoopDraftView(*forward.program(), forward, node)` 是**把图当视图传**（构造一个只读视图），不是写。
**没有一处需要可写句柄。** **"48 个论据"里没有一个反例。**

**三个数（这一笔）**：**消失的参数 0 个**（`run` 本来就不收草稿——它收的是自己的成员）；**改动的行数 32**；**保留 0 个**。

### b6 的"永远不收"清单（三条，各一句为什么是边界而不是漏网）

| 名字 | 为什么它是边界 |
|---|---|
| `ungated()` | 它在**别的模块**里，收的是草稿；这里只是调用者。**改它属于那一族的工作，不属于 pyllback 的签名。** |
| `LoopDraftView` 的构造 | **它自己就是"一张草稿的视图"**——视图要的是草稿，不是事务；收事务的应该是造视图的人。 |
| `rt::fallBackToCopyingAppend` | **运行时入口**，调用方是 pass，它拿的是"要追加的那张图"，收事务会把运行时的边界掀开。 |

**记住它们不是妥协**：**这三处是"事务之外还允许存在草稿"的全部理由**，b6 要删掉 `GraphDraft` 时，得先回答这三条。

## 41. `passes/` + `core/tags` 那 22 处：转掉的、和"不该转"的（`3a128f94e`）

**先给清单（一行一处）**：

| # | 位置 | 那个函数收了什么 | 调用点上能不能拿事务 |
|---|---|---|---|
| 1 | `core/tags.h:55` | 前置声明 `class GraphDraft;` | —（声明） |
| 2 | `core/tags.h:249` / `tags.cpp:575` | `factsOf(const GraphDraft &, node)` | **不收**：见下方"边界" |
| 3 | `core/tags.h:256` / `tags.cpp:584` | `permissionsOf(...)` | **不收**：同上 |
| 4 | `core/tags.h:264` / `tags.cpp:594` | `effectivePermissions(...)` | **不收**：同上 |
| 5 | `core/tags.h:274` / `tags.cpp:619` | `declaresIndependentElements(const GraphDraft &)` | **不收**：同上 |
| 6 | `core/tags.h:281` / `tags.cpp:637` | `markIndependentElements(GraphDraft &, attr)` | **不收**：同上 |
| 7 | `passes/opt/generic/algebra.cpp:154` | `constant<T>(const GraphDraft &, ...)` | **能**（`simplify` 已收事务）→ **转** |
| 8 | `passes/opt/generic/rewrite.h:26` | `using camel::runtime::GraphDraft;` | 第 9 条转完即删 → **删** |
| 9 | `passes/opt/generic/rewrite.h:34` | `isComputedAfter(const GraphDraft &, ...)` | **能**（`replaceNode`、`pruneBranch` 都有事务）→ **转** |
| 10 | `passes/opt/inline/engine.cpp:118` | **注释**里的 `GraphDraft::calleeGraphOf` | → **改写成"草稿的 `calleeGraphOf`"** |
| 11 | `passes/opt/inline/engine.cpp:298` | `upstreamOf(const GraphDraft &, ...)` | **能**（内联轮次有事务）→ **转** |
| 12 | `passes/opt/inline/engine.cpp:324` | `isDraftBranchArmHead(const GraphDraft &, ...)` | **能** → **转** |
| 13 | `passes/test/control.cpp:19` | `node(GraphDraft &, {...})`（37 个调用点） | **能**（每个用例都有 `owner`）→ **转** |
| 14 | `passes/test/fault.cpp:70`、`:85` | 两个 lambda `(GraphDraft &draft, brch, join)` | **能**（`breakFirstBranchOfMain` 里 `session.edit(owner)` 就是事务）→ **转** |
| 15 | `passes/test/resources.cpp:151`、`:157`、`:162`、`:168` | 三个 lambda + `make_unique<GraphDraft>()` | **留**：这一族自己**造**草稿（构造出来的不是事务给的），要动它得先回答"谁能造草稿"——那不是这一片的判据 |
| 16 | `passes/test/writings.cpp:216` | `GraphDraft::WritingScope scope(draft)` | **留**：它是**类型的嵌套作用域守卫**，不是参数 |
| 17 | `passes/test/anchor.h:24`、`:36` | `anchorNode(GraphDraft &)`、`blankNode(...)` | **留**：33 个调用点散在 7 个测试文件里，是**另一片**（测试 pass 一族） |

**"为什么 `core/tags` 不该转"——这一条是这张表里最重要的**：

`camel/core/tags.h` **已经把同一个函数写了两遍**：一遍收 `const GraphDraft &`，一遍收 `const GCGraph &`。
**因为它们是两个不同的类**，访问器不同；**而事务交出来的正是草稿**——所以收草稿的那一遍**恰好就是事务的调用者需要的那一遍**。
**再加一个收事务的重载，只会多一个拼写，不会多一条保证。** 这里剩下的是**"草稿上的只读 API"**——**是边界，不是漏网。**

**这一片的结果**：`files 56 → 51`，`references 408 → 399`。

**三个数**：**消失的参数 9 个**（第 7、9、11、12、13、14 六项 + `using` 一行 + 注释一处不计数，实为 9 个参数）；
**删掉的行数 73**；**保留 13 个**（表里标"留"与"不收"的）。

**四个数：51 / 399 / 3 / 4。**

## 42. `compile/gir` 收口：**造草稿的地方只剩一处，而它收的是事务**（`49716f337`）

**每个点先回答"造还是用"**：

| 位置 | 造还是用 | 改成了什么 |
|---|---|---|
| `builder_control.cpp:132` | **用**（读一张已有的草稿） | `const auto &draft = *nodeDraftOf(node);` |
| `builder_helpers.h:509` | **用** | `const auto *draft = nodeDraftOf(node);` |
| `builder_operators.cpp:4021` | **用** | `auto *draft = nodeDraftOf(a);` |
| `builder.cpp:1424` `InsertRegionScope` | **用它，而且要活过语句** | **改收 `runtime::GraphTransaction *transaction`**——它自己的析构里用 `transaction->edit()` |

**那句区别（下一人会问的）**：**这个 builder 不造草稿，它用一张**——唯一造草稿的是 `DraftGraphBuilder` 的构造函数，
而它**本来就收 `std::shared_ptr<GraphTransaction>`**。**所以"造草稿的地方也能收事务"的答案是：造的时候收事务（那是所有者），
用的时候收它借来的那张草稿；而一个要活过语句的作用域，收的必须是所有者，不是被它借出去的东西。**

**`compile/gir` 现在剩下的**：`nodeDraftOf` 这个访问器本身与两处 `unordered_map<const GraphDraft *, ...>`（`draftOwners` 的键）
——**它们是"草稿就是我的键"这一类，不是参数**。（`nodeDraftOf` 的 66 个调用点仍用它，那是下一步的事。）

**三个数**：**消失的拼写 4 处（参数 0 个——这四处本来就不是参数，是借用）**；**删掉的行数 22**；**保留 0 个**。

**四个数：47 / 395 / 3 / 4。**

### ⚠ `writings` 那 1 个：**撞车了，没动**

`origin/develop` **比本车道领先 121 个提交**，其中**两笔正好在 `writings` 上**：
* `1e70b99ac`（02:14）"let the writings open the transaction a draft alone has no owner for"——改了 `writings.h`／`writings.cpp`／`graph_txn.*`
* `01ea89d6f`（02:15）"let the tests hand the writings a draft, as they did before"——改了 `test/writings.cpp` 等四个文件

**所以"`writings` 那 1 个"正被另一条车道在做。按规矩：报，不自己合。** 要先把这个车道快进到 develop 再决定。

## 43. `test/` 收口：**测试里再没有一处自己造的草稿**（`29ee885c6`）

**先回答那一问："测试里谁能造草稿？"** 答案取 **(a)**：**没有谁**。
* **理由一**：b6 的验收要是"`GraphDraft` 的构造只出现在事务里"，**测试里留一个 `make_unique<GraphDraft>` 就等于验收永远差一步**。
* **理由二**：`GraphTransaction::open(id, type)` **本来就是为"没有所有者的草稿"开的门**（`graph_txn.h:27`），测试正是它的用户；不借这个门，测试就得自己造一个**连任何转换过的 API 都递不进去**的草稿——那已经是一座孤岛。
* **理由三**：测试是"唯一不受产品约束的调用者"这句话**不成立**——它约束的正是"谁能造"。**约束一条，就不该给它开例外。**

**做了什么**：
* `resources.cpp`：`makeDraft` 从 `make_unique<rt::GraphDraft>()` 改成 `rt::GraphTransaction::open(id, type)`；三个 helper（`addPort`／`finish`／`encode`）改收事务，草稿在函数体内借。
* `anchor.h`：`anchorNode`／`blankNode` 改收事务；**33 个调用点散在 7 个文件，一片做完**（`edges` 17、`chunks` 1、`keepfn` 2、`graph_release` 1、`fault` 1、`facts` 1）。每个调用点手上本来就有事务——`session.edit(...)` 或一个绑定好的 owner。

**三个数**：**消失的参数 6 个**（`resources` 4、`anchor` 2）；**删掉的行数 156**；**保留 1 个**（`test/writings.cpp:216` 的 `GraphDraft::WritingScope`——**类型自己的作用域守卫，不是参数**）。

**四个数：45 / 389 / 2 / 4。**（**`constructions` 3 → 2**：测试里那一处构造没了，只剩事务内部的两处。）

---

## 44. b6 的开工清单（**最终表**，`29ee885c6` 之后）

**余 389 处 / 45 个文件**（`git grep -o -w GraphDraft`；**另有事务自己那 10 处不计入判据**）。

| 类 | 处数 | 在哪 | 下一步 |
|---|---|---|---|
| **墙：类型自己的家** | **296** | `src/runtime/draft.cpp` 129、`draft_construct.cpp` 39、`draft_inline.cpp` 25、`draft_session.cpp` 23、`draft_regions.cpp` 20、`include/camel/runtime/draft.h` 20、`draft_clone.cpp` 13、`draft_types.cpp` 7、`draft_session.h` 7、`draft_clone.h` 4、`draft_writes.*` 6、`draft_inline.h` 2、`draft_types.h` 1 | **不收**：这是类型自己的定义与成员实现 |
| **墙：图的两层** | **25** | `graph.cpp` 11、`graph.h` 5、`graph_build.h` 4、`node_ref.h` 4、`definitions.h` 1 | **不收**：`DraftNodeRef` 拿草稿当键、`encode` 的入参 |
| **边界一：`LoopDraftView`** | **6** | `loop.h` 4、`loop.cpp` 2 | **不收**（第 1 条） |
| **边界二：`fallBackToCopyingAppend`** | 含在上行 | `loop.cpp:657` | **不收**（第 2 条） |
| **边界三：真的复制一份时** | — | — | **不收**（第 3 条 `ungated`／`fallBackToCopyingAppend`／`LoopDraftView` 三者合为三条） |
| **边界四：`tags` 的草稿重载** | **11** | `core/tags.h` 6、`tags.cpp` 5 | **不收**（第 4 条，§41 的原话） |
| **待做：`writings`** | **19** | `writings.cpp` 13、`writings.h` 5、`passes/test/writings.cpp` 1 | **另一条车道正在做**（`1e70b99ac`／`01ea89d6f`），**本车道不碰** |
| **待做：`compile/gir`** | **16** | `draft_graph_builder.h` 5、`draft_graph_builder.cpp` 4、`graph_encode.cpp` 3、`builder_support.h` 1、`debug_json.cpp` 1、`gir/graph.h` 1、`nodes.h` 1 | **访问器本身**（`nodeDraftOf` 66 个调用点）与 `draftOwners` 的键——"草稿就是我的键"那一类 |
| **待做：`tiles`** | **10** | `region_extract.cpp` 3、`graph_values.*` 4、`regions.h` 1、`region_outline.cpp` 1、`region_test_pass.cpp` 1 | **下一片** |
| **待做：其它** | **5** | `operator_instance.*` 2、`composite/func.h` 1、`modules/graph/txn.cpp` 1、`proof.h` 1 | **下一片** |
| **待做：`onnx`** | **1** | `exporter.cpp:4815` | **最后一片** |

**合计：296 + 25 + 17（四条边界）+ 51（待做）= 389。**

## 45. `writings` 那 19 处：**哪一种"拿草稿"能被删掉**（`3232bf888`）

**判据一句话**：**一处能删掉的 `GraphDraft` 参数，等于删掉一次"谁有权力写这张图"的含糊。**

**分类表（19 处 → 改 10、留 9）**：

| 处 | 位置 | 判定 |
|---|---|---|
| 1 | `writings.h:116` `CostContext::draft` | **改**：成本模型是**读者**，收事务、自己问 `.graph()` |
| 2 | `writings.h:426` `DraftWritings::draft()` | **删**：调用者说 `transaction().edit()` 更诚实 |
| 3 | `writings.h:423` 注释里的 `GraphDraft::setWritings` | **改**：写成 `` `setWritings` `` |
| 4 | `writings.cpp:90` `nodeText` | **改**：收事务（17 个调用点） |
| 5 | `writings.cpp:451` `readsVariable` | **改**：收事务 |
| 6 | `writings.cpp:473` `shapeTakesPart` | **改**：收事务 |
| 7 | `writings.cpp:613` `makeKey` | **改**：收事务（2 个调用点） |
| 8 | `writings.cpp:41/65` `EngineRootScope` 的形参与成员 | **改**：收事务；`relocateStaticValues` 走 `edit()`，`debugDraftRootOpened` 走 `graph()` |
| 9 | `writings.cpp:864` `const GraphDraft &draft = context.draft;` | **改**：`context.transaction.graph()` |
| 10 | `writings.cpp:921` `CostContext{draft_, context}` | **改**：`{*transaction_, context}` |
| 11 | `writings.h:411` + `writings.cpp:434` **只收草稿的构造函数** | **留**：**这是另一条车道 4 小时前才立的门**（"a draft alone has no owner"），删它等于替他们改决定；要删得先问 |
| 12 | `writings.h:557` 成员 `GraphDraft &draft_` | **留**：**草稿就是这份 writings 的研究对象**，不是伸手要 |
| 13–17 | `writings.cpp:824/1003/1435/1982/2449` 五处 `GraphDraft::WritingScope` | **留**：**类型自己的作用域守卫**，同 §41 判过的那条 |
| 18–19 | `test/writings.cpp:216` | **留**：同上（上一片已判） |

**三个数**：**消失的参数 6 个**（第 1、4、5、6、7、8 项）+ **一个访问器**（第 2 项）；**删掉的行数 55**；**保留 9 个**。

**四个数：45 / 379 / 2 / 4。**（`references` 389 → 379。）

**一个插曲**：这一片第一次跑整闸时 `runner.backend_matrix` 红了（嵌套自检 36 条里 7 条失败）。**A/B 一度指向这一片，但那是假象**：同一条用例单独再跑就绿（11.64 s vs 22.73 s），**它自己带一条按 CPU 时间判的余量规则，整闸刚跑完时的负载会把它顶红**。**记在这里，免得下一次有人把它算到 writings 头上。**

### 一条判据：**时间类断言的红，先单跑复核再归因**

> **"按 CPU/时间判的余量规则，在整闸刚跑完的负载下载荷最高；所以时间类断言的红必须先单跑复核，才能归因到某笔改动。"**

**它同时适用于 `writings` 与 `startup.cost` 两处**：
* `runner.backend_matrix`（今晚，`writings` 这一片）：整闸里 22.73 s 红、单独跑 11.64 s 绿。
* `startup.cost`（更早）：wall/CPU 两种读数给出相反的结论。

**做法**：红了先 `-Plan <那一条>` 单跑；单跑还红才做 `git stash` + 重建的 A/B；**单跑绿就不是那一笔**，别为一个负载造的假红去改实现。**这条判据的价值不在结论，在于它把"我改了它所以它红了"这个最自然的错觉挡在门外。**

### 一条判据：**散文里的数与代码里的数，不是同一种数**

> **"散文里的引用可以改，代码里的引用一动就换了一层意思；所以同一个数（`references`）在两种改法下含义不同，报数时必须说清是哪一种。"**

`compile/gir` 那 7 处是散文（`dbab87327`）：`references` 379 → 372，但**代码里的依赖一处没少**，那 7 **不是 b6 的进度**。`writings` 那 10 处是代码（`3232bf888`），那个 10 才是。**以后报这个数，先说"散文降了几、代码降了几"。**

## 46. `tiles` 那 10 处：**7 个通行参数卡在"3 处自己造"后面，而那 3 处卡在一处没有 id**

| 处 | 位置 | 判定 |
|---|---|---|
| 1–4 | `graph_values.h:14/18` + `graph_values.cpp:69/100`（`packGraphAggregate`、`packGraphTuple`） | **通行参数**：收草稿只为往里写 |
| 5–6 | `region_extract.cpp:66/118`（`extractRegion` 的两个内部步骤） | **通行参数** |
| 7 | `region_outline.cpp:65` | **通行参数** |
| 8 | `regions.h:53` `std::unique_ptr<GraphDraft> graph`（`RegionDraft` 的家） | **tiles 自己造** |
| 9 | `region_extract.cpp:148` `make_unique<GraphDraft>()` | **tiles 自己造** |
| 10 | `region_test_pass.cpp:96` 栈上的 `GraphDraft draft;` | **tiles 自己造** |

**为什么 1–7 不能先改**：`packGraphTuple(draft, …)` 的三个调用点（`region_extract.cpp:241`、`region_outline.cpp:465`、`region_test_pass.cpp:102`）手上都只有一张草稿，而这两张草稿来自第 8、9 项。**先改通行参数，就得在调用点 `borrow()` 一次——拿一个拼写换另一个拼写，正是判据禁的那件事。**

**为什么连 8、9 项也卡住**：`GraphTransaction::open` 收一个 `GraphId`，而 region 的 id 是 **attach 的时候才分配**的（`region_outline.cpp:390`、`region_test_pass.cpp:110` 的 `setId(context.graphIds().allocate())`）。**extract 的那一刻根本没有 id 可开**。要改得先回答"region 的 id 什么时候分配"——那是另一问。

**这一片实际动的是第 10 项**：`checkBindingKinds` 手上有一个 `Context`，**它的 id 现在就能分配**，于是它开一个事务、从事务里拿草稿，`setId` 那行随之消失（id 已在 `open` 里给了）。**这是这一层里唯一不需要先回答 id 问题的一处。**

**四个数：41 / 371 / 2 / 4。**（`files` 42 → 41、`references` 372 → 371，**都是代码**。）

**b6 的最后一块已写成问题**：`questions-2026-10-31-gir-handle-names-a-transaction-or-a-draft.md`。

## 47. `其它` 那几处，和**"事务能不能在 attach 之前开"的答案**（`94018cbff`）

### 先答那个更值的问题：`modules/graph/txn` 不是答案所在，它是"事务不必等于 id"的证据

`GraphTransaction` 有两个入口（`graph_txn.h`）：

| 入口 | 造还是名字 | 要不要 id |
|---|---|---|
| `open(id, funcType)` | **自己造**一张草稿 | **要**，当场命名（`:12` 的 `setId`） |
| `borrow(draft)` | **只名字**一张已有草稿 | **不要**；`id()` 从草稿读（`:48`） |

**所以"事务能不能在 attach 之前开"——能，但只有 `borrow` 这一种**；而 `borrow` 的语义是"这张草稿的所有权在别人手里"，**region 要的恰恰不是这个**。region 要的是第三种：**先造、id 后给**（`open` 不带 id）。**今天没有这个入口。**

**这就是那句话的答案**：卡住 `tiles` 的不是引用，是**没有人定"region 的 id 什么时候分配"**。要不要给 `open` 一个"id 待定"的形态（或者让 region 在 extract 时就分配 id），是要作者定的事——**和 §46 的问题页同源**，所以没另开一页。

### 分类（原清单 5 处，实际查到 4 处 + 1 处已经没有了）

| 处 | 位置 | 判定 |
|---|---|---|
| 1 | `modules/graph/txn.cpp:52` `using camel::runtime::GraphDraft;` | **删**：这个 using **一次都没被用到**——点名一个自己不碰的类型，等于宣告一个不存在的主人 |
| 2 | `modules/onnx/exporter.cpp:4815` `bindCaptures(GraphDraft &, …)` | **改**：`session.rootDraft()` 本来就返回 `GraphTransaction&`，收下它、问 `.edit()` |
| 3 | `src/runtime/operator_instance.h:40` + `.cpp:92` `kernelArgumentsOf(const GraphDraft &, node)` | **等**：5 个调用点（`generic.cpp:1112`、`definitions.cpp:508`、`module.cpp:2140`、`writings.cpp:1530/2364`）**手上都只有一张草稿**——**与 `tiles` 那 7 个通行参数同型，同一个 id 问题堵着** |
| 4 | `modules/rewrite/proof.h:51` | **留**：它点的是 `GraphDraft::decode` 这个 API。**改它会把 `decode` 那格也用散文降下来**（试过：`decode` 4 → 3），**那是拿散文改代码格，不做** |
| 5 | `include/camel/core/data/composite/func.h` | **已经没有了**（今天 0 处），清单可能旧了 |

**四个数：39 / 369 / 2 / 4。**（`files` 41 → 39、`references` 371 → 369，**都是代码**；`decode` 4 不动，正是第 4 项的克制。）

### 一条判据：**改不动的时候，问题页比十处引用更有进展**

> **"一个层里的引用改不动，往往不是引用的问题，是那句'id 什么时候分配'没人回答；这时候写问题页，比改十处引用更有进展。"**

`tiles` 是这样（10 处只动得了 1 处，但"7 卡在 3 后面、3 卡在 id 上"这个结论比那 1 处值钱），`其它` 也是这样（第 3 项同样是 id 问题）。**两次都指向同一个没人回答的句子——这才是这一整轮真正的发现。**

**同源的第二问已并进同一页**（不另开页）：`questions-2026-10-31-gir-handle-names-a-transaction-or-a-draft.md` 现在有两问——①handle 名字事务还是草稿；②**region 的 id 能不能在 extract 时就分配**。第二问里给了第三种形态的草稿（`openPending()` + `assignId(GraphId)`）和**两条必须一起答的风险**：**pending 且从未 `assignId` 的事务丢草稿时，它登记过的静态值与槽位要不要显式退**；**两张 pending 同时存在时，`GraphId` 的分配时刻一变，图编号顺序就会变**（`gir` 黄金与"同一输入同一输出"都看得见）。**只写，不实现。**

### 一条记录：**试改过，回退在案**

`modules/rewrite/proof.h:51` 的 `GraphDraft::decode` 我一度改成 `` `decode` ``，整闸没红，但账本的 `decode` 那一格从 **4 掉到 3**——**散文降了代码格**。已原样回退（`94018cbff` 里这一处没有动）。**记下来，是因为"数要对得上意思"比"数变小"重要；一次试改加一次回退，比一个漂亮的小数字更能说明账本在被怎么用。**

## 48. 资源账那条用例：撤掉，不是改写

**结论（作者 2026-10-31 `f24411f32` 裁定 (c)）**：**草稿不在 GC 空间分配**——`extractRegion` 只做 `GraphTransaction::open`（普通堆上的 `make_unique<GraphDraft>`）与往草稿自己的向量里 `addNode`/`appendStaticSlot`（`src/runtime/draft.cpp:2131-2145`），**整条抽出路径一次 `autoSpace()` 分配都没有**。实测三行：`during / before / after = allocations 2 / 2 / 2，live 0 / 0 / 0`。

所以**"丢弃前后 GC 读数相同"在丢弃路径上恒真且无信息**。

**"事务拥有它造出的一切"由类型保证**——`RegionDraft::graph` 就是那个 `unique_ptr<GraphTransaction>`，丢弃即析构——**不由读数证明**。作者自认 00:50/02:50 要这条用例时没看清草稿不在 GC 空间里。

**留下的两句**：**"一条账恒真时，要么改题、要么撤题；把恒真的账留在验收里，等于给自己发一张永远通过的通行证。"** **"一条账恒真时，不是账写对了，是它测的那条路根本不分配——先去找分配发生在哪一步。"**

## 49. `graph_encode.cpp` 那两处：**编码器不再点名存储**（本笔）

**为什么是它、而且是现在**：§46／§47 之后 `compile/gir` 剩 9 处，其中 8 处——`nodeDraftOf`、`DraftGraphBuilder::draft()`、`draftOwners` 的键、`fromDraft`——都在 `questions-2026-10-31-gir-handle-names-a-transaction-or-a-draft.md` 的冻结里（"在裁定之前，GIR 这一层不动"）。**不在冻结里的只有编码器这 2 处，而它的调用者只有 1 个**（`EncodeSession::prepareGraph`）。依判据"先挑调用者最少的"，这一片就是它。

| 处 | 位置 | 判定 |
|---|---|---|
| 1 | `graph_encode.cpp` 的 `using camel::runtime::GraphDraft;` | **删**：它只服务第 2 项，第 2 项改完它就是死的 |
| 2 | `graph_encode.cpp` 的 `collectLiveDraftNodes(const GraphDraft &)` | **改**：收事务，体首一行 `const auto &draft = transaction.graph();`（与 b5a–b5d 同一个写法）；唯一调用点把 `graph->view()` 换成 `graph->transaction()` |

**四个数：37 / 365 / 1 / 4。**（`files` 38 → 37、`references` 367 → 365，**都是代码，散文一格没动**。）

**三种收益**：① 删行 1（`using` 那一行；净行数 +2，是新增的体首行与两行注释）；② 消点 2（一处 `using`、一处"收草稿"的参数）；③ **一个文件从此整片不提这个类型**——`git grep -ow GraphDraft -- src/compile/gir/graph_encode.cpp` 答 0。

**为什么不借此动别的**：它不碰 handle、不碰 `draft()`、不碰 region 的 id——**这三件是冻结的全部内容**；而 `tags` 的五个草稿重载是 §41 判过的边界（"事务交出来的正是草稿"），不是漏网。

**逐字节证明**：`test/cases` 3235 + `examples` 29 = **3264 个 `.cml` 各跑一次 `std::jgir`**，改动前后各一遍，**stdout / stderr / 退出码三者 3264 / 3264 逐字节相同**；同一二进制连跑两遍也是 3264 / 3264 相同（这一次没有出现 §1 那处行序抖动，所以不需要豁免任何文件）。

## 50. 死的前向声明：**六个里有两个**（本笔）

**先核那个结论**："全树 `class GraphDraft;` 只剩一处"——**核过了，不对**。全树有 **6 处** `class GraphDraft;`，另有 1 处 `friend class GraphDraft;`；本笔删 **2 处**，剩 4 处，都是承重的。

| 处 | 位置 | 判定 |
|---|---|---|
| 1 | `include/camel/runtime/definitions.h` | **删**：那个头文件里再没有第二处点这个名字，声明是死的 |
| 2 | `include/camel/runtime/graph.h` 命名空间开头那句 | **删**：同一个头文件的 `friend class GraphDraft;`（`GCGraph` 的私有区）本身就在 `camel::runtime` 里声明了这个名字，所以开头这句是重复的；它前面也没有任何一处用到（另外三处都是注释） |
| 3 | `include/camel/core/tags.h` | **留**：五处限定名（`camel::runtime::GraphDraft`）靠它 |
| 4 | `include/camel/runtime/loop.h` | **留**：`LoopDraftView` 的形参与成员靠它 |
| 5 | `include/camel/runtime/node_ref.h` | **留**：`DraftNodeRef` 的成员与比较靠它 |
| 6 | `src/runtime/graph_build.h` | **留**：编码器的三个形参靠它 |
| — | `include/camel/runtime/graph.h` 的 `friend class GraphDraft;` | **不动**：它给的是**访问权**，不是名字；撤它要先答"`GraphDraft` 今天还用不用 `GCGraph` 的私有区"，那是另一问 |

**四个数：36 / 363 / 1 / 4。**（`files` 37 → 36、`references` 365 → 363；**两行删除、零行新增**，这一笔的 diff 就是两行减法。）

**一条判据（协调者给的一句）**：**"降层率的分子是站点数，分母是证明：没有逐字节证明的那一处不许记进账。"**

**逐字节证明**：与上一笔同一套——3264 个 `.cml` 各跑一次 `std::jgir`，改动前后 stdout / stderr / 退出码三者 3264 / 3264 逐字节相同。

## 51. 存储头文件那 31 处 API 面：逐处判过，今天只换得动 2 处（本笔）

**这是一次"逐处点名"，不是一次批量替换。** 31 个代码站点（`draft.h` 17、`draft_session.h` 6、`draft_clone.h` 3、`draft_writes.h` 3、`draft_inline.h` 1、`draft_types.h` 1；另有 4 行注释不在内），逐处问同一个问题：**这一处的调用者手上有没有事务？**

| 判决 | 处数 | 是哪些 | 为什么 |
|---|---|---|---|
| **本笔换掉** | **2** | `draft.h` 的 `draftRuntimeNodeRefs`、`draft_writes.h` 的 `isWrittenArgument` | 前者唯一的调用者（`draft_session.cpp` 的 `committedNodeRefs`）手上正是 `found->second->transaction`；后者**唯一的调用者就在自己那个 `.cpp` 里**，它根本不是 API 面 |
| 备好、没换 | 3 | `draft_session.h` 的 `debugDraftRootOpened`/`debugDraftRootClosed`、`draft_writes.h` 的 `WrittenSlots` 构造 | 前两者唯一的调用者在 `writings.cpp`（另一条车道的文件），而且它们是 `#ifndef NDEBUG`：Release 闸门根本看不见，换它得连带跑一次 Debug 构建；后者两个调用者（`modules/graph/read.cpp` 的 `copy->edit()`、`generic/algebra.cpp`）手上都有事务，下笔可换 |
| 换不动 | 26 | 见下 | 调用者全在存储内部，**手上没有事务可以递** |

那 26 处的构成：**类型自己的家 8**（`decode` 工厂、构造/析构、拷贝删除 ×2、赋值删除 ×2、`const_cast<GraphDraft *>(this)`）、**类定义 2**（`draft.h` 里 `DraftNodeHeader::owner` 那句、以及类头那句）、**类型自己的守卫 2**（`WritingScope` 的形参与成员，§44"边界三"）、**读者的声明 12**（`controlRegionOf`／`sameSyncBlock`／`nodeIsFreelySchedulable`／`carriesOrdering`／`isUnanchoredPure`／`ungated`／`resolveTupleProjection`／`reinferDraftTypes`／`isWrittenInPlace`／clone 两个）、**会话自己的门 4**（`DraftRootsDebug::Entry::draft`、`tryDraft` ×2、`draft(GraphId)`）。

**结论（这一片真正的话）**：**存储的头文件不是"31 处都有事务可递"，而是"31 处里有 26 处的调用者就是类型自己"**——`controlRegionOf` 在 `draft_inline.cpp` 里被调 5 次、`sameSyncBlock` 在 `draft.cpp` 里被调 3 次，那些地方手上的东西就是 `*this`。**把这种形参换成事务，只能在调用处先 `GraphTransaction::borrow(*this)`，那是拿事务去包一个自己就拥有草稿的对象——不是降层，是加一层。** 所以这一片的上限就是 5 处（2 + 3），剩下的要等类型改名那一笔。

**收益（协调者要的三个数）**：**形参 0 个**（`draftRuntimeNodeRefs` 的形参是换了类型，不是删了）、**`using` 0 个**、**删掉公开声明 1 条 + 它 6 行说明**（`isWrittenArgument` 从 `draft_writes.h` 收进 `draft_writes.cpp` 的匿名命名空间，与"删一个 using"同类：一个名字离开公开面），另加 2 行说明（迁移理由与"这个答案属于一次提交，所以要问会做这次提交的事务"）。**没有一项收益为负，但这一笔的收益也就是这 1 条声明。**

**四个数：36 / 360 / 1 / 4。**（`references` 363 → 360：`draft.h` 的声明 −1、`graph.cpp` 的定义 −1（`const auto &draft = transaction.graph();` 之后它不再点这个名字）、`draft_writes.h` 的声明 −1；`files` 仍是 36。）

**一条判据（协调者给的一句）**：**"前向声明是名字的记忆；删掉它之前先问这个头文件里还有谁记得这个名字。"**

**逐字节证明**：同一套 3264 个 `.cml` × `std::jgir`，与 base1 比 stdout / stderr / 退出码 **3264 / 3264 全同**。

## 52. 头文件那 31 处里备好的 3 处（本笔）

| 处 | 换成 | 代价 |
|---|---|---|
| `draft_writes.h` 的 `WrittenSlots` 构造 | 事务：`written_(transaction.graph().nodeSlotCount(), false)` | 两个调用者都有事务——`modules/graph/read.cpp` 的 `copy`（`unique_ptr<GraphTransaction>`）与 `src/passes/opt/generic/algebra.cpp` 的 `transaction` |
| `draft_session.h` 的 `debugDraftRootOpened` / `debugDraftRootClosed` | 事务：体首 `const GraphDraft &draft = transaction.graph();` | 唯一调用者是 `writings.cpp` 的 `EngineRootScope`，手上有 `transaction_`；**两处都在 `#ifndef NDEBUG` 里，Release 闸门看不见，这一笔另跑一次 Debug 闸门** |

**四个数：36 / 356 / 1 / 4。**（`references` 360 → 356：`draft_session.h` −2、`draft_writes.h` −1、`draft_writes.cpp` 构造的定义 −1；`files` 仍 36。）

**两套闸门（本笔）**：Release `8103 / 8098 / 1 / 2 / 2`，Debug `8103 / 8097 / 1 / 3 / 2`；两套同一红 `gpu_preference.named_device`（本机 8 张卡被别的进程占着）。Debug 那一跑抓到两处 Release 同样会红的编译错（`WrittenSlots` 体里的 `draft`、`modules/graph/read.cpp` 的第二处 `WrittenSlots(node.draft())`），改完重跑。

**逐字节证明**：3264 个 `.cml` × `std::jgir`，与 base1 比 stdout / stderr / 退出码 3264 / 3264 全同。

## 53. b6 的真实天花板：**188 处**

口径：`GraphDraft` 这个名字在"**不改名、只降引用**"下的下界；按站点（occurrence）算，`include/camel/runtime/graph_txn.h` 与 `src/runtime/graph_txn.cpp` 不进账（§0）。今日读数 **356**。

| 档 | 处数 | 构成 |
|---|---|---|
| ① 今日已降（**不在**下面的 356 之内，是差额） | **13** | 369 → 356：`95de7ab86` −4、`5e95f465f` −2、`d417ae6fc` −3、本笔 −4 |
| ② 能降、有代价 | **154** | 见下八行 |
| ③ 降不掉 | **188** | 类外成员定义 **175**、`draft.h` 里类型自己的声明 **12**（类头、`decode` 工厂、构造、析构、拷贝删除 ×2、赋值删除 ×2、`const_cast<GraphDraft *>(this)`、`DraftNodeHeader::owner`、`WritingScope` ×2）、`graph.h` 的 `friend class GraphDraft;` **1** |
| 另：注释 | 14 | 可改写，但按 §45 不计进度 |

**188 + 154 + 14 = 356**，三档加注释正好是今日读数。

**② 的逐条（154）**

| 处数 | 是什么 | 代价 |
|---|---|---|
| 59 | 存储自己的读者（`draft_inline.cpp` 24、`draft.cpp` 11、`draft_clone.cpp` 11、`draft_types.cpp` 7、`draft_writes.cpp` 2、`draft_construct.cpp` 1、另 3 处头文件声明） | 调用者在类型自己体内，要换只能在调用处先 `GraphTransaction::borrow(*this)`——加一层，不是降层 |
| 27 | 会话的门（`tryDraft` 返回 ×2、`draft(GraphId)`、`DraftRootsDebug::Entry::draft`、`noteDraftOpened/Closed`、`encode`、`commit`、`edit` 等） | 会话本来就是草稿的持有者；要换得先改"会话交给谁"这一问 |
| 10 | `graph.cpp` 的读者定义与 debug 记录 | 同上，随 59 一起动 |
| 6 | GIR 层（`builder_support.h` 1、`draft_graph_builder.cpp` 3、`draft_graph_builder.h` 2） | **冻结**：handle 那一问等作者裁定 |
| 7 | tiles（`graph_values.h` 2、`graph_values.cpp` 2、`region_extract.cpp` 2、`region_outline.cpp` 1） | **卡在区域 id 那一问** |
| 11 | tags（`tags.h` 6、`tags.cpp` 5） | §41 边界，本层不碰 |
| 25 | 其它边界（`node_ref.h` 4、`loop.h` 4、`loop.cpp` 2、`graph_build.h` 4、`operator_instance` 2、`writings` 8、`test/writings` 1） | b3／§44 边界三／编码器形参／`kernelArgumentsOf` 仍阻塞／另一条车道 |
| 9 | `draft.h` 剩的读者声明 6 + `draft_clone.h` 3 | 随 59 一起动 |

**结论（一句）**：**按"不改名、只降引用"的口径，b6 的终点是 188 处，不是 0 处。** 要 0 处只剩"改名或删类型"那一条路，而那条路是另一问——`GraphDraft` 本体不是 find/replace 的对象（§44）。

**一条判据（协调者给的一句）**：**"一层降不动的时候，正确答案是报出它的天花板和理由，不是继续报进度条。"**
