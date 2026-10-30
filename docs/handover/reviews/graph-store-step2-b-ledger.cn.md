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

| b2.4a | `passes/test/control.cpp` 的 15 处 `GraphDraft draft;` 换成真事务：`auto owner = GraphTransaction::open(static_cast<GraphId>(1)); auto &draft = owner->edit();`（既有 public 入口，无新构造路径，事务活过整个测试函数）。**它能单独编译通过，所以不挂在 b2.3e 上**；剩下的 `node(GraphDraft &draft, …)` 这个局部助手随 b5 | 80 / 642 | 8 | 15 |

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
