# graph-store 第 2 步 b：直连账（逐步填）

b 的落点是"可编辑图归图仓的事务所有、前端一开始就在里面建图"，最后一笔删掉 `GraphDraft`。
本页只做一件事：把"还有多少处直接连到 `GraphDraft`"记成三个数，**每一笔提交填一行**。叙述可以说"删净了"，这三个数不会。

## 0. 三个数怎么数

**口径一条：事务自己的两份文件不计**——`include/camel/runtime/graph_txn.h`、`src/runtime/graph_txn.cpp`
引用 `GraphDraft` 是事务的墙，不是旧住户。全部命令都在仓库根执行，读的是当前树：

```bash
# 1. 事务外引用：文件数 / 引用数
git grep -l GraphDraft -- src include modules | grep -v -e graph_txn.h -e graph_txn.cpp | wc -l
git grep -o GraphDraft -- src include modules | grep -v -e graph_txn.h -e graph_txn.cpp | wc -l
# 2. 构造点：全树新建一张可编辑图的每一处
git grep -o 'make_unique<[^>]*GraphDraft>' -- src include modules | grep -v -e graph_txn.h -e graph_txn.cpp | wc -l
# 3. decode 点：`GraphDraft::decode` 的出现处（含 `src/runtime/draft.cpp` 的定义一句、
#    `modules/rewrite/proof.h` 注释里的一句）
git grep -o 'GraphDraft::decode' -- src include modules | grep -v -e graph_txn.h -e graph_txn.cpp | wc -l
```

**三个数只能单调降，b6 必须全 0。涨了就是那一笔在新建直连，停下解释。**
（本页开页时按"调用点"数 `decode` 是 14；这里取"出现处"16，把定义与注释也数进去，省得口径随人变。
上一轮口头报的 15 是漏了 `rewrite/proof.h:51` 那一句注释，以本页为准。）

## 1. 表

| 提交 | 这一步做了什么 | 事务外引用（文件 / 引用） | 构造点 | `decode` 点 |
|---|---|---|---|---|
| `3d8cee417`（基点 `origin/develop`） | — | 95 / 885 | 9 | 16 |
| `2cdd65681` | 前端改由 `GraphTransaction` 拥图：`createCompileGraph` 开事务并在此取号，`DraftGraphBuilder` 不再 `make_unique<GraphDraft>`、`draft()` 答自 `transaction_->edit()` | 95 / 883 | 8 | 16 |
| b2.1（本笔，sha 见下一行） | 会话改由事务拥图：`RuntimeGraphDraftSession::DraftEntry` 持一份 `GraphTransaction`（不再持 `unique_ptr<GraphDraft>`），`tryDraft`/`ensureDraft`/`edit`/`rootDraft` 从 `transaction->edit()`、`transaction->graph()` 回答，**30+ 调用点一个字没动**；`GraphTransaction` 增加 `decode`；`open` 不再替前端决定区域继承，改回 `DraftGraphBuilder` 自己说 | 95 / 882 | 8 | 15 |

b1 的 −2 是从哪掉的：`src/compile/gir/draft_graph_builder.cpp` 的 `make_unique<runtime::GraphDraft>`
与 `include/camel/compile/gir/draft_graph_builder.h` 的 `std::unique_ptr<runtime::GraphDraft> draft_`
各掉 1 处。原始口径（含事务两份文件）从 95/885 变成 97/888——多的 2 文件 5 引用就是墙本身。

## 2. 还没填的行

b2 起逐笔往下填；b2 的每一批（最小转发面、再批量搬）都单独一行。
**b6 那一行必须是 `0 / 0`、0、0**；不是 0 就说明 `GraphDraft` 还有住户，不能删名。