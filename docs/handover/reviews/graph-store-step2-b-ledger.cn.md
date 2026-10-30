# graph-store 第 2 步 b：直连账（逐步填）

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
