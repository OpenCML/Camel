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